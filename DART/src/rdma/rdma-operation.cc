#include "log/log.hpp"

#include "rdma/rdma-basic.hpp"

namespace RDMA {

int poll_with_CQ(ibv_cq* cq, int pollNumber, struct ibv_wc* wc) {
    int count = 0;

    do {
        int new_count = ibv_poll_cq(cq, 1, wc);
        count += new_count;
    } while (count < pollNumber);

    if (count < 0) {
        log_error << "Poll Completion failed." << std::endl;
        return -1;
    }

    if (wc->status != IBV_WC_SUCCESS) {
        log_error << "Failed status " << ibv_wc_status_str(wc->status) << " (" << wc->status << ") for wr_id " << (int)wc->wr_id << std::endl;
        return -1;
    }

    return count;
}

int poll_once(ibv_cq* cq, int pollNumber, struct ibv_wc* wc) {
    int count = ibv_poll_cq(cq, pollNumber, wc);
    if (count <= 0) {
        return 0;
    }
    if (wc->status != IBV_WC_SUCCESS) {
        log_error << "Failed status " << ibv_wc_status_str(wc->status) << " (" << wc->status << ") for wr_id " << (int)wc->wr_id << std::endl;
        return -1;
    } else {
        return count;
    }
}

// recv
void fill_sge_wr(
    ibv_sge& sg, ibv_recv_wr& wr, uint64_t source, uint64_t size, uint32_t lkey
) {
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source;
    sg.length = size;
    sg.lkey = lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
}

// send
void fill_sge_wr(
    ibv_sge& sg, ibv_send_wr& wr, uint64_t source, uint64_t size, uint32_t lkey
) {
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source;
    sg.length = size;
    sg.lkey = lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
}

// for UD and DC
bool rdma_send(
    ibv_qp* qp, uint64_t source, uint64_t size, uint32_t lkey, ibv_ah* ah,
    uint32_t remote_QPN /* remote dct_number */, bool signaled
) {

    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg, wr, source, size, lkey);

    wr.opcode = IBV_WR_SEND;

    wr.wr.ud.ah = ah;
    wr.wr.ud.remote_qpn = remote_QPN;
    wr.wr.ud.remote_qkey = UD_PKEY;

    if (signaled)
        wr.send_flags = IBV_SEND_SIGNALED;

    // RdmaStatistics::get_instance().accumulate_send();
    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr, &wrBad))) {
        log_error << "Send with RDMA_SEND failed: " << errro_num << std::endl;
        return false;
    }
    return true;
}

// for RC & UC
bool rdma_send(ibv_qp* qp, uint64_t source, uint64_t size, uint32_t lkey, int32_t imm) {

    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg, wr, source, size, lkey);

    if (imm != -1) {
        wr.imm_data = imm;
        wr.opcode = IBV_WR_SEND_WITH_IMM;
    } else {
        wr.opcode = IBV_WR_SEND;
    }

    wr.send_flags = IBV_SEND_SIGNALED;
    // RdmaStatistics::get_instance().accumulate_send();
    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr, &wrBad))) {
        log_error << "Send with RDMA_SEND failed: " << errro_num << std::endl;
        return false;
    }
    return true;
}

bool rdma_receive(ibv_qp* qp, uint64_t source, uint64_t size, uint32_t lkey, uint64_t wr_id) {
    struct ibv_sge sg;
    struct ibv_recv_wr wr;
    struct ibv_recv_wr* wrBad;

    fill_sge_wr(sg, wr, source, size, lkey);

    wr.wr_id = wr_id;

    // RdmaStatistics::get_instance().accumulate_recv();
    int errro_num = 0;
    if ((errro_num = ibv_post_recv(qp, &wr, &wrBad))) {
        log_error << "Receive with RDMA_RECV failed: " << errro_num << std::endl;
        return false;
    }
    return true;
}

bool rdma_receive(ibv_srq* srq, uint64_t source, uint64_t size, uint32_t lkey) {

    struct ibv_sge sg;
    struct ibv_recv_wr wr;
    struct ibv_recv_wr* wrBad;

    fill_sge_wr(sg, wr, source, size, lkey);

    // RdmaStatistics::get_instance().accumulate_recv();
    int errro_num = 0;
    if ((errro_num = ibv_post_srq_recv(srq, &wr, &wrBad))) {
        log_error << "Receive with RDMA_RECV failed: " << errro_num << std::endl;
        return false;
    }
    return true;
}

// for RC & UC
bool rdma_read(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey,
    uint32_t remote_rkey, bool signaled, uint64_t wr_id
) {
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg, wr, source, size, lkey);

    wr.opcode = IBV_WR_RDMA_READ;

    if (signaled) {
        wr.send_flags = IBV_SEND_SIGNALED;
    }

    wr.wr.rdma.remote_addr = dest;
    wr.wr.rdma.rkey = remote_rkey;
    wr.wr_id = wr_id;

    // RdmaStatistics::get_instance().accumulate_read();
    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr, &wrBad))) {
        log_error << "Send with RDMA_READ failed: " << errro_num << std::endl;
        return false;
    }
    return true;
}

// for RC & UC
bool rdma_write(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t size, uint32_t lkey,
    uint32_t remote_rkey, int32_t imm, bool signaled, uint64_t wr_id
) {

    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg, wr, source, size, lkey);

    if (imm == -1) {
        wr.opcode = IBV_WR_RDMA_WRITE;
    } else {
        wr.imm_data = imm;
        wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    }

    if (signaled) {
        wr.send_flags = IBV_SEND_SIGNALED;
    }

    wr.wr.rdma.remote_addr = dest;
    wr.wr.rdma.rkey = remote_rkey;
    wr.wr_id = wr_id;
    // RdmaStatistics::get_instance().accumulate_write();
    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr, &wrBad))) {
        log_error << "Send with RDMA_WRITE(WITH_IMM) failed: " << errro_num << std::endl;
        sleep(10);
        return false;
    }
    return true;
}

// RC & UC
bool rdma_fetch_and_add(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t add, uint32_t lkey, uint32_t remote_rkey
) {
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg, wr, source, 8, lkey);

    wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
    wr.send_flags = IBV_SEND_SIGNALED;

    wr.wr.atomic.remote_addr = dest;
    wr.wr.atomic.rkey = remote_rkey;
    wr.wr.atomic.compare_add = add;
    // RdmaStatistics::get_instance().accumulate_FAA();
    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr, &wrBad))) {
        log_error << "Send with ATOMIC_FETCH_AND_ADD failed: " << errro_num << std::endl;
        return false;
    }
    return true;
}

bool rdma_fetch_and_add_boundary(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t add, uint32_t lkey,
    uint32_t remote_rkey, uint64_t boundary, bool signaled, uint64_t wr_id
) {
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg, wr, source, 8, lkey);

    wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
    wr.send_flags = IBV_SEND_INLINE;
    wr.wr_id = wr_id;

    if (signaled) {
        wr.send_flags |= IBV_SEND_SIGNALED;
    }
    wr.wr.atomic.remote_addr = dest;
    wr.wr.atomic.rkey = remote_rkey;
    wr.wr.atomic.compare_add = add;

    // RdmaStatistics::get_instance().accumulate_FAA();
    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr, &wrBad))) {
        log_error << "Send with MASK FETCH_AND_ADD failed: " << errro_num << std::endl;
        return false;
    }
    return true;
}

// for RC & UC
bool rdma_CAS(
    ibv_qp* qp, uint64_t source, uint64_t dest, uint64_t compare, uint64_t swap, uint32_t lkey,
    uint32_t remote_rkey, bool signaled, uint64_t wr_id
) {
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg, wr, source, 8, lkey);

    wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;

    if (signaled) {
        wr.send_flags = IBV_SEND_SIGNALED;
    }

    wr.wr.atomic.remote_addr = dest;
    wr.wr.atomic.rkey = remote_rkey;
    wr.wr.atomic.compare_add = compare;
    wr.wr.atomic.swap = swap;
    wr.wr_id = wr_id;
    // RdmaStatistics::get_instance().accumulate_CAS();
    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr, &wrBad))) {
        log_error << "Send with ATOMIC_CMP_AND_SWP failed: " << errro_num << std::endl;
        // sleep(5);
        return false;
    }
    return true;
}

bool rdma_write_batch(ibv_qp* qp, RdmaOpRegion* ror, int k, bool signaled, uint64_t wr_id) {

    struct ibv_sge sg[k_oro_max];
    struct ibv_send_wr wr[k_oro_max];
    struct ibv_send_wr* wrBad;

    for (int i = 0; i < k; ++i) {
        fill_sge_wr(sg[i], wr[i], ror[i].source, ror[i].size, ror[i].lkey);

        wr[i].next = (i == k - 1) ? nullptr : &wr[i + 1];

        wr[i].opcode = IBV_WR_RDMA_WRITE;

        if (i == k - 1 && signaled) {
            wr[i].send_flags = IBV_SEND_SIGNALED;
        }

        wr[i].wr.rdma.remote_addr = ror[i].dest;
        wr[i].wr.rdma.rkey = ror[i].remote_rkey;
        wr[i].wr_id = wr_id;
        // RdmaStatistics::get_instance().accumulate_write(i == 0 ? 1 : 0);
    }

    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr[0], &wrBad))) {
        log_error << "Send with RDMA_WRITE(WITH_IMM) failed: " << errro_num << std::endl;
        // sleep(10);
        return false;
    }
    return true;
}

bool rdma_CAS_read(
    ibv_qp* qp, const RdmaOpRegion& cas_ror, const RdmaOpRegion& read_ror, uint64_t compare,
    uint64_t swap, bool signaled, uint64_t wr_id
) {

    struct ibv_sge sg[2];
    struct ibv_send_wr wr[2];
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg[0], wr[0], cas_ror.source, 8, cas_ror.lkey);
    wr[0].opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    wr[0].wr.atomic.remote_addr = cas_ror.dest;
    wr[0].wr.atomic.rkey = cas_ror.remote_rkey;
    wr[0].wr.atomic.compare_add = compare;
    wr[0].wr.atomic.swap = swap;
    wr[0].next = &wr[1];

    fill_sge_wr(sg[1], wr[1], read_ror.source, read_ror.size, read_ror.lkey);
    wr[1].opcode = IBV_WR_RDMA_READ;
    wr[1].wr.rdma.remote_addr = read_ror.dest;
    wr[1].wr.rdma.rkey = read_ror.remote_rkey;
    wr[1].wr_id = wr_id;
    wr[1].send_flags |= IBV_SEND_FENCE;
    if (signaled) {
        wr[1].send_flags |= IBV_SEND_SIGNALED;
    }

    // auto& s = RdmaStatistics::get_instance();
    // s.accumulate_CAS(1);
    // s.accumulate_read(0);

    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr[0], &wrBad))) {
        log_error << "Send with CAS_READs failed: " << errro_num << std::endl;
        // sleep(10);
        return false;
    }
    return true;
}

bool rdma_write_FAA(
    ibv_qp* qp, const RdmaOpRegion& write_ror, const RdmaOpRegion& faa_ror, uint64_t add_val,
    bool signaled, uint64_t wr_id
) {

    struct ibv_sge sg[2];
    struct ibv_send_wr wr[2];
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg[0], wr[0], write_ror.source, write_ror.size, write_ror.lkey);
    wr[0].opcode = IBV_WR_RDMA_WRITE;
    wr[0].wr.rdma.remote_addr = write_ror.dest;
    wr[0].wr.rdma.rkey = write_ror.remote_rkey;
    wr[0].next = &wr[1];

    fill_sge_wr(sg[1], wr[1], faa_ror.source, 8, faa_ror.lkey);
    wr[1].opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
    wr[1].wr.atomic.remote_addr = faa_ror.dest;
    wr[1].wr.atomic.rkey = faa_ror.remote_rkey;
    wr[1].wr.atomic.compare_add = add_val;
    wr[1].wr_id = wr_id;

    if (signaled) {
        wr[1].send_flags |= IBV_SEND_SIGNALED;
    }

    // auto& s = RdmaStatistics::get_instance();
    // s.accumulate_FAA(1);
    // s.accumulate_write(0);

    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr[0], &wrBad))) {
        log_error << "Send with Write Faa failed: " << errro_num << std::endl;
        // sleep(10);
        return false;
    }
    return true;
}

bool rdma_write_CAS(
    ibv_qp* qp, const RdmaOpRegion& write_ror, const RdmaOpRegion& cas_ror, uint64_t compare,
    uint64_t swap, bool signaled, uint64_t wr_id
) {

    struct ibv_sge sg[2];
    struct ibv_send_wr wr[2];
    struct ibv_send_wr* wrBad;

    fill_sge_wr(sg[0], wr[0], write_ror.source, write_ror.size, write_ror.lkey);
    wr[0].opcode = IBV_WR_RDMA_WRITE;
    wr[0].wr.rdma.remote_addr = write_ror.dest;
    wr[0].wr.rdma.rkey = write_ror.remote_rkey;
    wr[0].next = &wr[1];

    fill_sge_wr(sg[1], wr[1], cas_ror.source, 8, cas_ror.lkey);
    wr[1].opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    wr[1].wr.atomic.remote_addr = cas_ror.dest;
    wr[1].wr.atomic.rkey = cas_ror.remote_rkey;
    wr[1].wr.atomic.compare_add = compare;
    wr[1].wr.atomic.swap = swap;
    wr[1].wr_id = wr_id;

    if (signaled) {
        wr[1].send_flags |= IBV_SEND_SIGNALED;
    }

    // auto& s = RdmaStatistics::get_instance();
    // s.accumulate_CAS(1);
    // s.accumulate_write(0);

    int errro_num = 0;
    if ((errro_num = ibv_post_send(qp, &wr[0], &wrBad))) {
        log_error << "Send with Write Cas failed: " << errro_num << std::endl;
        // sleep(10);
        return false;
    }
    return true;
}

}