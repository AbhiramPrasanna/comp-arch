#include "log/log.hpp"

#include "rdma/rdma-basic.hpp"

namespace RDMA {

bool modify_QP_to_init(struct ibv_qp* qp, uint8_t ib_port) {

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = ib_port;
    attr.pkey_index = 0;

    switch (qp->qp_type) {
    case IBV_QPT_RC:
        attr.qp_access_flags =
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
        break;

    case IBV_QPT_UC:
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
        break;

    default:
        log_error << "implement me :)" << std::endl;
        return false;
    }

    if (ibv_modify_qp(
            qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS
        )) {
        log_error << "failed to modify QP state to INIT" << std::endl;
        return false;
    }
    return true;
}

bool modify_QP_to_RTR(
    struct ibv_qp* qp, uint32_t remote_QPN, uint16_t remote_lid, uint8_t* remote_gid, uint8_t ib_port, int gid_index
) {

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;

    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = remote_QPN;
    attr.rq_psn = PSN;

    fill_ah_attr(&attr.ah_attr, remote_lid, remote_gid, ib_port, gid_index);

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN;

    if (qp->qp_type == IBV_QPT_RC) {
        attr.max_dest_rd_atomic = 16;
        attr.min_rnr_timer = 12;
        flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    }
    auto res = ibv_modify_qp(qp, &attr, flags);
    if (res) {
        log_error << "failed to modify QP state to RTR: " << res << std::endl;
        return false;
    }
    return true;
}

bool modify_QP_to_RTS(struct ibv_qp* qp) {
    struct ibv_qp_attr attr;
    int flags;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = PSN;
    flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

    if (qp->qp_type == IBV_QPT_RC) {
        attr.timeout = 14;
        attr.retry_cnt = 7;
        attr.rnr_retry = 7;
        attr.max_rd_atomic = 16;
        flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;
    }
    auto res = ibv_modify_qp(qp, &attr, flags);
    if (res) {
        log_error << "failed to modify QP state to RTS: " << res << std::endl;
        return false;
    }
    return true;
}

bool modify_UD_to_RTS(struct ibv_qp* qp, uint8_t ib_port) {

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = ib_port;
    attr.qkey = UD_PKEY;

    if (qp->qp_type == IBV_QPT_UD) {
        if (ibv_modify_qp(
                qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY
            )) {
            log_info << "failed to modify QP state to INIT" << std::endl;
            return false;
        }
    } else {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PORT)) {
            log_info << "failed to modify QP state to INIT" << std::endl;
            return false;
        }
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
        log_info << "failed to modify QP state to RTR" << std::endl;
        return false;
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = PSN;

    if (qp->qp_type == IBV_QPT_UD) {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            log_info << "failed to modify QP state to RTS" << std::endl;
            return false;
        }
    } else {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
            log_info << "failed to modify QP state to RTS" << std::endl;
            return false;
        }
    }
    return true;
}

}