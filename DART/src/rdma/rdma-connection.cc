#include <cstdint>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include "log/log.hpp"
#include "measure/measure.hpp"
#include "socket/socket.hpp"

#include "rdma/rdma-basic.hpp"
#include "rdma/rdma-connection.hpp"


bool huge_page_alloc(uintptr_t& addr, uint64_t size) {
    addr = (uintptr_t)mmap(
        NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0
    );
    if ((void*)addr == MAP_FAILED) {
        log_error << "failed: " << errno << std::endl;
        return false;
    }
    memset((void*)addr, 0, size);
    return true;
}

bool huge_page_free(uintptr_t addr, uint64_t size) {

    // will fail if not 2MiB align, seems a bug of linux kernel
    // ref: https://groups.google.com/g/linux.kernel/c/9vNoH55w3JE/m/bM1zsa0oBNAJ
    // so skip munmap here (since it will automatically free it)
    if (size % 2_MiB != 0) size = ALIGN_MiB(size, 2);

    int res = munmap((void*)addr, size);
    if (res == -1) {
        log_debug << "addr = " << hex_str(addr) << ", size = " << readable_byte(size) << std::endl;
        log_error << "failed: " << errno << std::endl;
        return false;
    }
    return true;
}

namespace RDMA {

bool RDMAMemoryRegion::create(uint64_t mr_size_byte, uint8_t ib_dev, uint8_t ib_port, int gid_idx) {

    int iresult;
    bool result;

    uintptr_t mr_addr = 0;
    result = huge_page_alloc(mr_addr, mr_size_byte);
    if (!result) {
        log_error << "server huge_page_alloc error" << std::endl;
        return false;
    }
    this->gid_idx = gid_idx;
    this->ib_dev = ib_dev;
    this->ib_port = ib_port;
    result = RDMA::init_dev_pd_ctx(normal_data, ib_dev, ib_port, gid_idx);
    if (!result) {
        log_error << "server init_dev_pd_ctx error" << std::endl;
        return false;
    }
    result = RDMA::create_memory_region(mr, (void*)mr_addr, mr_size_byte, normal_data);
    if (!result) {
        log_error << "server create_memory_region error" << std::endl;
        return false;
    }

    return true;
}

RDMAMemoryRegion::~RDMAMemoryRegion() {

    int iresult;
    bool result;

    uintptr_t mr_addr = (uintptr_t)mr->addr;
    uintptr_t mr_size = mr->length;

    // this may destory the mr's data! so save them above
    iresult = ibv_dereg_mr(mr);
    if (iresult != 0) log_error << "ibv_dereg_mr() error: " << iresult << std::endl;

    result = RDMA::destory_dev_pd_ctx(normal_data);
    if (!result) log_error << "destory_dev_pd_ctx() error" << std::endl;

    result = huge_page_free(mr_addr, mr_size);
    if (!result) log_error << "huge_page_free() error" << std::endl;
}

bool RDMAQueuePair::create(RDMAMemoryRegion& rdma_mr) {

    int iresult;
    bool result;

    this->rdma_mr = &rdma_mr;

    this->send_cq = ibv_create_cq(this->rdma_mr->normal_data.ctx, 128, nullptr, nullptr, 0);
    this->recv_cq = ibv_create_cq(this->rdma_mr->normal_data.ctx, 128, nullptr, nullptr, 0);

    // RC / UC / UD
    result = create_queue_pair(qp, IBV_QPT_RC, this->send_cq, this->recv_cq, this->rdma_mr->normal_data);
    if (!result) {
        log_error << "RDMAQueuePair create_queue_pair() error" << std::endl;
        return false;
    }

    return true;
}

rdma_connect_data RDMAQueuePair::generate_connect_data() const {

    rdma_connect_data local_conn_data;

    local_conn_data.addr = (uintptr_t)this->rdma_mr->mr->addr;
    local_conn_data.size = (uint64_t)this->rdma_mr->mr->length;
    local_conn_data.rkey = this->rdma_mr->mr->rkey;
    local_conn_data.qp_num = this->qp->qp_num;
    local_conn_data.lid = this->rdma_mr->normal_data.lid;
    memcpy(local_conn_data.gid, &this->rdma_mr->normal_data.gid, 16);

    return local_conn_data;
}

RDMAQueuePair::~RDMAQueuePair() {

    int iresult;
    bool result;

    iresult = ibv_destroy_qp(this->qp);
    if (iresult != 0) log_error << "ibv_destroy_qp() error: " << iresult << std::endl;

    iresult = ibv_destroy_cq(this->recv_cq);
    if (iresult != 0) log_error << "ibv_destroy_cq() error: " << iresult << std::endl;

    iresult = ibv_destroy_cq(this->send_cq);
    if (iresult != 0) log_error << "ibv_destroy_cq() error: " << iresult << std::endl;
}

bool tcp_send_rdma_connect_data(sockets::SocketConnection& sock, const uint32_t mem_id_num, const uint32_t compute_num, const uint32_t thread_num, const rdma_connect_data& send_connect_data) {

    int iresult;
    bool result;

    char send_buff[sizeof(mem_id_num) + sizeof(compute_num) + sizeof(thread_num) + sizeof(rdma_connect_data) + 1] = {0};
    uint32_t* tcp_send_mem_id_num = (uint32_t*)send_buff;
    uint32_t* tcp_send_compute_num = (uint32_t*)((char*)send_buff + sizeof(mem_id_num));
    uint32_t* tcp_send_thread_num = (uint32_t*)((char*)send_buff + sizeof(mem_id_num) + sizeof(compute_num));
    rdma_connect_data* tcp_send_data = (rdma_connect_data*)((char*)send_buff + sizeof(mem_id_num) + sizeof(compute_num) + sizeof(thread_num));

    /* exchange using TCP sockets info required to connect QPs */
    *tcp_send_mem_id_num = htonl(mem_id_num);
    *tcp_send_compute_num = htonl(compute_num);
    *tcp_send_thread_num = htonl(thread_num);
    tcp_send_data->addr = htonll(send_connect_data.addr);
    tcp_send_data->size = htonll(send_connect_data.size);
    tcp_send_data->rkey = htonl(send_connect_data.rkey);
    tcp_send_data->qp_num = htonl(send_connect_data.qp_num);
    tcp_send_data->lid = htons(send_connect_data.lid);
    memcpy(tcp_send_data->gid, &send_connect_data.gid, 16);

    result = sock.sock_send_data(sizeof(mem_id_num) + sizeof(compute_num) + sizeof(thread_num) + sizeof(rdma_connect_data), send_buff);
    if (!result) {
        log_error << "failed to send id_num and rdma_connect_data" << std::endl;
        return false;
    }

    return true;
}

bool tcp_receive_rdma_connect_data(sockets::SocketConnection& sock, uint32_t& mem_id_num, uint32_t& compute_num, uint32_t& thread_num, rdma_connect_data& result_connect_data) {

    int iresult;
    bool result;

    char read_buff[sizeof(mem_id_num) + sizeof(compute_num) + sizeof(thread_num) + sizeof(rdma_connect_data) + 1] = {0};
    uint32_t* tcp_read_mem_id_num = (uint32_t*)read_buff;
    uint32_t* tcp_read_compute_num = (uint32_t*)((char*)read_buff + sizeof(mem_id_num));
    uint32_t* tcp_read_thread_num = (uint32_t*)((char*)read_buff + sizeof(mem_id_num) + sizeof(compute_num));
    rdma_connect_data* tcp_read_data = (rdma_connect_data*)((char*)read_buff + sizeof(mem_id_num) + sizeof(compute_num) + sizeof(thread_num));

    result = sock.sock_read_data(sizeof(mem_id_num) + sizeof(compute_num) + sizeof(thread_num) + sizeof(rdma_connect_data), read_buff);
    if (!result) {
        log_error << "failed to read id_num and rdma_connect_data" << std::endl;
        return false;
    }

    mem_id_num = ntohl(*tcp_read_mem_id_num);
    compute_num = ntohl(*tcp_read_compute_num);
    thread_num = ntohl(*tcp_read_thread_num);
    result_connect_data.addr = ntohll(tcp_read_data->addr);
    result_connect_data.size = ntohll(tcp_read_data->size);
    result_connect_data.rkey = ntohl(tcp_read_data->rkey);
    result_connect_data.qp_num = ntohl(tcp_read_data->qp_num);
    result_connect_data.lid = ntohs(tcp_read_data->lid);
    memcpy(result_connect_data.gid, tcp_read_data->gid, 16);

    return true;
}

bool RDMAConnection::connect(uint64_t mr_size_byte, rdma_connect_data& remote_connect_data, uint8_t ib_dev, uint8_t ib_port, int gid_idx) {

    int iresult;
    bool result;

    this->remote_connect_data = remote_connect_data;
    this->inner_create_new_mr = true;
    this->rdma_mr = new RDMAMemoryRegion();
    this->rdma_mr->create(mr_size_byte, ib_dev, ib_port);
    this->inner_create_new_qp = true;
    this->rdma_qp = new RDMAQueuePair();
    this->rdma_qp->create(*this->rdma_mr);

    result = modify_QP();
    if (!result) return false;

    return true;
}

bool RDMAConnection::connect(RDMAMemoryRegion& rdma_mr, rdma_connect_data& remote_connect_data) {

    int iresult;
    bool result;

    this->remote_connect_data = remote_connect_data;
    this->rdma_mr = &rdma_mr;
    this->inner_create_new_qp = true;
    this->rdma_qp = new RDMAQueuePair();
    this->rdma_qp->create(rdma_mr);

    result = modify_QP();
    if (!result) return false;

    return true;
}

bool RDMAConnection::connect(RDMAQueuePair& rdma_qp, rdma_connect_data& remote_connect_data) {

    int iresult;
    bool result;

    this->remote_connect_data = remote_connect_data;
    this->rdma_mr = rdma_qp.rdma_mr;
    this->rdma_qp = &rdma_qp;

    result = modify_QP();
    if (!result) return false;

    return true;
}

RDMAConnection::~RDMAConnection() {

    if (this->inner_create_new_qp && this->rdma_qp)
        delete this->rdma_qp;

    if (this->inner_create_new_mr && this->rdma_mr)
        delete this->rdma_mr;

}

uintptr_t RDMAConnection::get_remote_mr_addr() const noexcept {
    return this->remote_connect_data.addr;
}

uint64_t RDMAConnection::get_remote_mr_size() const noexcept {
    return this->remote_connect_data.size;
}

uintptr_t RDMAConnection::get_local_mr_addr() const noexcept {
    return (uintptr_t)this->rdma_mr->mr->addr;
}

uint64_t RDMAConnection::get_local_mr_size() const noexcept {
    return this->rdma_mr->mr->length;
}

void RDMAConnection::read_from_local_mr_offset(uintptr_t dest, uintptr_t local_mr_offset, uint64_t size) {
    memcpy((void*)dest, (void*)((uintptr_t)this->rdma_mr->mr->addr + local_mr_offset), size);
}

void RDMAConnection::read_from_local_mr_addr(uintptr_t dest, uintptr_t local_mr_addr, uint64_t size) {
    memcpy((void*)dest, (void*)(local_mr_addr), size);
}

void RDMAConnection::write_to_local_mr_offset(uintptr_t local_mr_offset, uintptr_t src, uint64_t size) {
    memcpy((void*)((uintptr_t)this->rdma_mr->mr->addr + local_mr_offset), (void*)src, size);
}

void RDMAConnection::write_to_local_mr_addr(uintptr_t local_mr_addr, uintptr_t src, uint64_t size) {
    memcpy((void*)(local_mr_addr), (void*)src, size);
}

bool RDMAConnection::read_from_remote_offset(uintptr_t local_mr_offset, uintptr_t remote_mr_offset, uint64_t size, bool poll) {

    int iresult;
    bool result;

    // his <rdma_read>'s dest and source are wrong, so swap them
    result = rdma_read(
        this->rdma_qp->qp,
        (uintptr_t)this->rdma_mr->mr->addr + local_mr_offset,
        this->remote_connect_data.addr + remote_mr_offset,
        size, this->rdma_mr->mr->lkey, remote_connect_data.rkey
    );
    struct ibv_wc wc;
    if (poll) {
        iresult = poll_with_CQ(this->rdma_qp->send_cq, 1, &wc);
        if (iresult < 0) {
            usage.read_fail_size_times[size]++;
            return false;
        }
    }
    if (result)
        usage.read_size_times[size]++;
    else
        usage.read_fail_size_times[size]++;
    return result;
}

bool RDMAConnection::read_from_remote_addr(uintptr_t local_mr_addr, uintptr_t remote_mr_addr, uint64_t size, bool poll) {

    int iresult;
    bool result;

    // his <rdma_read>'s dest and source are wrong, so swap them
    result = rdma_read(
        this->rdma_qp->qp,
        local_mr_addr,
        remote_mr_addr,
        size, this->rdma_mr->mr->lkey, remote_connect_data.rkey
    );
    struct ibv_wc wc;
    if (poll) {
        iresult = poll_with_CQ(this->rdma_qp->send_cq, 1, &wc);
        if (iresult < 0) {
            usage.read_fail_size_times[size]++;
            return false;
        }
    }
    if (result)
        usage.read_size_times[size]++;
    else
        usage.read_fail_size_times[size]++;
    return result;
}

bool RDMAConnection::write_to_remote_offset(uintptr_t remote_mr_offset, uintptr_t local_mr_offset, uint64_t size, bool poll) {

    int iresult;
    bool result;

    result = rdma_write(
        this->rdma_qp->qp,
        (uintptr_t)this->rdma_mr->mr->addr + local_mr_offset,
        this->remote_connect_data.addr + remote_mr_offset,
        size, this->rdma_mr->mr->lkey, remote_connect_data.rkey
    );
    struct ibv_wc wc;
    if (poll) {
        iresult = poll_with_CQ(this->rdma_qp->send_cq, 1, &wc);
        if (iresult < 0) {
            usage.write_fail_size_times[size]++;
            return false;
        }
    }
    if (result)
        usage.write_size_times[size]++;
    else
        usage.write_fail_size_times[size]++;
    return result;
}

bool RDMAConnection::write_to_remote_addr(uintptr_t remote_mr_addr, uintptr_t local_mr_addr, uint64_t size, bool poll) {

    int iresult;
    bool result;

    result = rdma_write(
        this->rdma_qp->qp,
        local_mr_addr,
        remote_mr_addr,
        size, this->rdma_mr->mr->lkey, remote_connect_data.rkey
    );
    struct ibv_wc wc;
    if (poll) {
        iresult = poll_with_CQ(this->rdma_qp->send_cq, 1, &wc);
        if (iresult < 0) {
            usage.write_fail_size_times[size]++;
            return false;
        }
    }
    if (result)
        usage.write_size_times[size]++;
    else
        usage.write_fail_size_times[size]++;
    return result;
}

__attribute__((noinline, optimize("O0")))
bool RDMAConnection::cas_8_to_remote_offset(
    uintptr_t remote_mr_offset, uintptr_t local_save_old_mr_offset,
    uint64_t swap_data, uint64_t compare_data
) {

    int iresult;
    bool result;

    result = rdma_CAS(
        this->rdma_qp->qp,
        (uintptr_t)this->rdma_mr->mr->addr + local_save_old_mr_offset,
        this->remote_connect_data.addr + remote_mr_offset,
        compare_data, swap_data,
        this->rdma_mr->mr->lkey, remote_connect_data.rkey
    );
    struct ibv_wc wc;
    iresult = poll_with_CQ(this->rdma_qp->send_cq, 1, &wc);
    if (iresult < 0) {
        usage.cas_fail_size_times[8]++;
        return false;
    }
    if (result) {
        if (compare_data != *(uint64_t*)((uintptr_t)this->rdma_mr->mr->addr + local_save_old_mr_offset)) {
            usage.cas_fail_size_times[8]++;
            // log_error << "compare_data != last_return_data: " << compare_data << " != " << last_return_data << std::endl;
            return false;
        } else {
            usage.cas_size_times[8]++;
            // log_purple << "succeed. compare_data == last_return_data: " << compare_data << " == " << last_return_data << std::endl;
            return true;
        }
    } else {
        usage.cas_fail_size_times[8]++;
        return false;
    }
}

__attribute__((noinline, optimize("O0")))
bool RDMAConnection::cas_8_to_remote_addr(
    uintptr_t remote_mr_addr, uintptr_t local_save_old_mr_addr,
    uint64_t swap_data, uint64_t compare_data
) {

    int iresult;
    bool result;

    result = rdma_CAS(
        this->rdma_qp->qp,
        local_save_old_mr_addr,
        remote_mr_addr,
        compare_data, swap_data,
        this->rdma_mr->mr->lkey, remote_connect_data.rkey
    );
    struct ibv_wc wc;
    iresult = poll_with_CQ(this->rdma_qp->send_cq, 1, &wc);
    if (iresult < 0) {
        usage.cas_fail_size_times[8]++;
        return false;
    }
    if (result) {
        if (compare_data != *(uint64_t*)(local_save_old_mr_addr)) {
            usage.cas_fail_size_times[8]++;
            // log_error << "compare_data != last_return_data: " << compare_data << " != " << last_return_data << std::endl;
            return false;
        } else {
            usage.cas_size_times[8]++;
            // log_purple << "succeed. compare_data == last_return_data: " << compare_data << " == " << last_return_data << std::endl;
            return true;
        }
    } else {
        usage.cas_fail_size_times[8]++;
        return false;
    }
}

bool RDMAConnection::poll_cq(uint32_t num) {
    int iresult;
    struct ibv_wc wc;
    iresult = poll_with_CQ(this->rdma_qp->send_cq, num, &wc);
    if (iresult < 0) return false;
    return true;
}


bool RDMAConnection::modify_QP() {

    int iresult;
    bool result;

    result = modify_QP_to_init(this->rdma_qp->qp, this->rdma_mr->ib_port);
    if (!result) return result;

    result = modify_QP_to_RTR(
        this->rdma_qp->qp,
        remote_connect_data.qp_num, remote_connect_data.lid, remote_connect_data.gid,
        this->rdma_mr->ib_port, this->rdma_mr->gid_idx
    );
    if (!result) return false;

    result = modify_QP_to_RTS(this->rdma_qp->qp);
    if (!result) return false;

    return true;
}

}