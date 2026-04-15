#include "log/log.hpp"

#include "rdma/rdma-basic.hpp"

namespace RDMA {

int k_max_device_memory_size = 0;

void rdma_query_queue_pair(ibv_qp* qp) {
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr);
    switch (attr.qp_state) {
    case IBV_QPS_RESET:
        log_debug << "QP state: IBV_QPS_RESET" << std::endl;
        break;
    case IBV_QPS_INIT:
        log_debug << "QP state: IBV_QPS_INIT" << std::endl;
        break;
    case IBV_QPS_RTR:
        log_debug << "QP state: IBV_QPS_RTR" << std::endl;
        break;
    case IBV_QPS_RTS:
        log_debug << "QP state: IBV_QPS_RTS" << std::endl;
        break;
    case IBV_QPS_SQD:
        log_debug << "QP state: IBV_QPS_SQD" << std::endl;
        break;
    case IBV_QPS_SQE:
        log_debug << "QP state: IBV_QPS_SQE" << std::endl;
        break;
    case IBV_QPS_ERR:
        log_debug << "QP state: IBV_QPS_ERR" << std::endl;
        break;
    case IBV_QPS_UNKNOWN:
        log_debug << "QP state: IBV_QPS_UNKNOWN" << std::endl;
        break;
    }
}

void check_DM_supported(struct ibv_context* ctx) {
    struct ibv_device_attr_ex attrs;

    struct ibv_query_device_ex_input input;
    memset(&input, 0, sizeof(struct ibv_query_device_ex_input));
    memset(&attrs, 0, sizeof(struct ibv_device_attr_ex));

    attrs.comp_mask |= (1 << 29);
    attrs.comp_mask |= (1 << 9);
    input.comp_mask |= (1 << 29);
    input.comp_mask |= (1 << 9);
    attrs.comp_mask = 0xffffffff;
    input.comp_mask = 0xffffffff;
    if (ibv_query_device_ex(ctx, nullptr, &attrs)) {
        log_warn << "couldn't query device attributes" << std::endl;
    }

    // if (attrs.max_dm_size == 0) {
    //     log_error << "can not support device memory" << std::endl;
    //     k_max_device_memory_size = 0;
    //     log_error << "simulate on-chip memory with DRAM" << std::endl;
    //     // exit(-1);
    // } else if (!(attrs.max_dm_size)) {
    // } else {
    //     k_max_device_memory_size = attrs.max_dm_size;
    //     log_debug << "RNIC has " << k_max_device_memory_size / 1024 << "KiB device memory" << std::endl;
    // }
}

}