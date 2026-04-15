#pragma once

#include <atomic>
#include <cstdlib>
#include <cstring>

#include <infiniband/verbs.h>

#include "measure/measure.hpp"

namespace RDMA {

#define UD_PKEY 0x11111111
#define PSN 3185
// #define DCT_ACCESS_KEY 3185

constexpr uint64_t minimum_size = 256_B;

// normal struct

extern int k_max_device_memory_size;

// local data
struct rdma_normal_data {
    uint16_t lid;
    struct ibv_context* ctx = nullptr;
    struct ibv_pd* pd = nullptr;
    union ibv_gid gid;
};

// remote data essential for local<->remote connection
struct rdma_connect_data {
    uint64_t addr;      /* Buffer address */        // (uintptr_t)RDMAMemoryRegion.mr->addr
    uint64_t size;      /* Buffer size */           // (uint64_t)RDMAMemoryRegion.mr->length
    uint32_t rkey;      /* Remote key */            // RDMAMemoryRegion.mr->rkey
    uint32_t qp_num;    /* QP number */             // rdma_normal_data.pd->qp_num
    uint16_t lid;	    /* LID of the IB port */    // rdma_normal_data.lid
    uint8_t gid[16];    /* gid */                   // rdma_normal_data.gid (memcpy)
}__attribute__((packed));


// rdma-initialize.cc

bool init_dev_pd_ctx(struct rdma_normal_data& normal_data, uint8_t dev_index, uint8_t ib_port, int gid_index);

bool destory_dev_pd_ctx(struct rdma_normal_data& normal_data);

bool create_memory_region(
    struct ibv_mr*& mr, void* mm, uint64_t mm_size, struct rdma_normal_data& normal_data
);
// bool create_memory_region_on_chip(
//     struct ibv_mr*& mr, void* mm, uint64_t mm_size, struct rdma_normal_data& normal_data
// );

bool create_queue_pair(
    struct ibv_qp*& qp, ibv_qp_type mode, struct ibv_cq* cq, struct rdma_normal_data& normal_data,
    uint32_t qps_max_depth = 128, uint32_t max_inline_data = 0
);

bool create_queue_pair(
    struct ibv_qp*& qp, ibv_qp_type mode, struct ibv_cq* send_cq, struct ibv_cq* recv_cq,
    struct rdma_normal_data& normal_data, uint32_t qps_max_depth = 128, uint32_t max_inline_data = 0
);

void fill_ah_attr(
    struct ibv_ah_attr* attr, uint32_t remote_lid, uint8_t* remote_gid, uint8_t ib_port, int gid_index
);


// rdma-state-modify.cc

bool modify_QP_to_init(struct ibv_qp* qp, uint8_t ib_port);
bool modify_QP_to_RTR(struct ibv_qp* qp, uint32_t remote_QPN, uint16_t remote_lid, uint8_t* gid, uint8_t ib_port, int gid_index);
bool modify_QP_to_RTS(struct ibv_qp* qp);
bool modify_UD_to_RTS(struct ibv_qp* qp, uint8_t ib_port);

class RdmaStatistics {
  public:
    std::atomic<uint64_t> send;
    std::atomic<uint64_t> receive;
    std::atomic<uint64_t> read;
    std::atomic<uint64_t> write;
    std::atomic<uint64_t> faa;
    std::atomic<uint64_t> cas;
    std::atomic<uint64_t> net;

    std::atomic<uint64_t> pre_send;
    std::atomic<uint64_t> pre_receive;
    std::atomic<uint64_t> pre_read;
    std::atomic<uint64_t> pre_write;
    std::atomic<uint64_t> pre_faa;
    std::atomic<uint64_t> pre_cas;
    std::atomic<uint64_t> pre_net;
    bool enable;
    static RdmaStatistics& get_instance() {
        static RdmaStatistics instance;
        return instance;
    }
    void accumulate_send(int n = 1) {
        if (enable) {
            send++;
            net += n;
        }
    }
    void accumulate_recv(int n = 1) {
        if (enable) {
            receive++;
            net += n;
        }
    }
    void accumulate_read(int n = 1) {
        if (enable) {
            read++;
            net += n;
        }
    }
    void accumulate_write(int n = 1) {
        if (enable) {
            write++;
            net += n;
        }
    }
    void accumulate_FAA(int n = 1) {
        if (enable) {
            faa++;
            net += n;
        }
    }
    void accumulate_CAS(int n = 1) {
        if (enable) {
            cas++;
            net += n;
        }
    }
    void clear() {
        send = 0;
        receive = 0;
        read = 0;
        write = 0;
        faa = 0;
        cas = 0;
        net = 0;
        pre_send = 0;
        pre_receive = 0;
        pre_read = 0;
        pre_write = 0;
        pre_faa = 0;
        pre_cas = 0;
        pre_net = 0;
        enable = true;
    }
    void record_pre() {
        pre_send = send.load();
        pre_receive = receive.load();
        pre_read = read.load();
        pre_write = write.load();
        pre_faa = faa.load();
        pre_cas = cas.load();
        pre_net = net.load();
    }
    void set_enable(bool e) { enable = e; }

  private:
    RdmaStatistics() { clear(); }
};


// rdma-operation.cc

void fill_sge_wr(ibv_sge& sg, ibv_send_wr& wr, uint64_t source, uint64_t size, uint32_t lkey);
void fill_sge_wr(ibv_sge& sg, ibv_recv_wr& wr, uint64_t source, uint64_t size, uint32_t lkey);

int poll_with_CQ(ibv_cq* cq, int pollNumber, struct ibv_wc* wc);
int poll_once(ibv_cq* cq, int pollNumber, struct ibv_wc* wc);

bool rdma_send(ibv_qp* qp, uint64_t source, uint64_t size, uint32_t lkey, ibv_ah* ah, uint32_t remote_QPN, bool signaled = false);
bool rdma_send(ibv_qp* qp, uint64_t source, uint64_t size, uint32_t lkey, int32_t imm = -1);

bool rdma_receive(ibv_qp* qp, uint64_t source, uint64_t size, uint32_t lkey, uint64_t wr_id = 0);
bool rdma_receive(ibv_srq* srq, uint64_t source, uint64_t size, uint32_t lkey);

bool rdma_read(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey,
    uint32_t remote_rkey, bool signaled = true, uint64_t wr_id = 0
);

bool rdma_write(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey,
    uint32_t remote_rkey, int32_t imm = -1, bool signaled = true, uint64_t wr_id = 0
);

bool rdma_fetch_and_add(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t add, uint32_t lkey,
    uint32_t remote_rkey
);
bool rdma_fetch_and_add_boundary(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t add, uint32_t lkey,
    uint32_t remote_rkey, uint64_t boundary = 63, bool signaled = true, uint64_t wr_id = 0
);

bool rdma_CAS(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t compare, uint64_t swap, uint32_t lkey,
    uint32_t remote_rkey, bool signaled = true, uint64_t wr_id = 0
);
// bool rdma_CAS_mask(
//     ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t compare, uint64_t swap, uint32_t lkey,
//     uint32_t remote_rkey, uint64_t mask = ~(0ull), bool signaled = true
// );

// struct for batch

constexpr int k_oro_max = 3;

struct RdmaOpRegion {
    uint64_t source;
    uint64_t dest;
    uint64_t size;
    uint32_t lkey;
    union {
        uint32_t remote_rkey;
        bool is_on_chip;
    };
};

struct Region {
    uint64_t source;
    uint32_t size;
    uint64_t dest;
};

bool rdma_write_batch(ibv_qp* qp, RdmaOpRegion* ror, int k, bool signaled, uint64_t wr_id = 0);

bool rdma_CAS_read(
    ibv_qp* qp, const RdmaOpRegion& cas_ror, const RdmaOpRegion& read_ror, uint64_t compare,
    uint64_t swap, bool signaled, uint64_t wr_id = 0
);
bool rdma_write_FAA(
    ibv_qp* qp, const RdmaOpRegion& write_ror, const RdmaOpRegion& faa_ror, uint64_t add_val,
    bool signaled, uint64_t wr_id = 0
);
bool rdma_write_CAS(
    ibv_qp* qp, const RdmaOpRegion& write_ror, const RdmaOpRegion& cas_ror, uint64_t compare,
    uint64_t swap, bool signaled, uint64_t wr_id = 0
);


// rdma-utility.cc

void rdma_query_queue_pair(ibv_qp* qp);
void check_DM_supported(struct ibv_context* ctx);

}