#include "log/log.hpp"

#include "rdma/rdma-basic.hpp"

namespace RDMA {

// init device, pd, ctx, port, gid
bool init_dev_pd_ctx(rdma_normal_data& normal_data, uint8_t dev_index, uint8_t ib_port, int gid_index) {

    ibv_device* dev = nullptr;
    ibv_context* ctx = nullptr;
    ibv_pd* pd = nullptr;
    ibv_port_attr portAttr;

    // get device names in the system
    int devicesNum;
    struct ibv_device** deviceList = ibv_get_device_list(&devicesNum);
    if (!deviceList) {
        log_error << "failed to get IB devices list" << std::endl;
        goto CreateResourcesExit;
    }

    // if there isn't any IB device in host
    if (!devicesNum) {
        log_info << "found " << devicesNum << " device(s)" << std::endl;
        goto CreateResourcesExit;
    }
    // log_info << "Open IB Device" << std::endl;

    // for (int i = 0; i < devicesNum; ++i) {
    //     const char* name = ibv_get_device_name(deviceList[i]);
    //     log_debug << "Device index " << i << " is " << name << std::endl;
    // }

    if (dev_index >= devicesNum || dev_index < 0) {
        log_error << "ib device wasn't found" << std::endl;
        goto CreateResourcesExit;
    }

    dev = deviceList[dev_index];

    // get device handle
    ctx = ibv_open_device(dev);
    if (!ctx) {
        log_error << "failed to open device" << std::endl;
        goto CreateResourcesExit;
    }
    /* We are now done with device list, free it */
    ibv_free_device_list(deviceList);
    deviceList = nullptr;

    // query port properties
    if (ibv_query_port(ctx, ib_port, &portAttr)) {
        log_error << "ibv_query_port failed" << std::endl;
        goto CreateResourcesExit;
    }

    // allocate Protection Domain
    // log_info << "Allocate Protection Domain" << std::endl;
    pd = ibv_alloc_pd(ctx);
    if (!pd) {
        log_error << "ibv_alloc_pd failed" << std::endl;
        goto CreateResourcesExit;
    }

    if (gid_index >= 0) {
        if (ibv_query_gid(ctx, ib_port, gid_index, &normal_data.gid)) {
            log_error << "could not get gid for ib_port: " << ib_port << ", gid_index: " << gid_index << std::endl;
            goto CreateResourcesExit;
        }
    }

    // Success :)
    normal_data.ctx = ctx;
    normal_data.pd = pd;
    normal_data.lid = portAttr.lid;

    // check device memory support
    if (k_max_device_memory_size == 0) {
        check_DM_supported(ctx);
    }

    return true;

/* Error encountered, cleanup */
CreateResourcesExit:
    log_error << "error encountered, cleanup ..." << std::endl;

    if (pd) {
        ibv_dealloc_pd(pd);
        pd = nullptr;
    }
    if (ctx) {
        ibv_close_device(ctx);
        ctx = nullptr;
    }
    if (deviceList) {
        ibv_free_device_list(deviceList);
        deviceList = nullptr;
    }

    return false;
}

bool destory_dev_pd_ctx(rdma_normal_data& normal_data) {
    bool rc = true;
    if (normal_data.pd) {
        if (ibv_dealloc_pd(normal_data.pd)) {
            log_error << "failed to deallocate PD" << std::endl;
            rc = false;
        }
    }
    if (normal_data.ctx) {
        if (ibv_close_device(normal_data.ctx)) {
            log_error << "failed to close device context" << std::endl;
            rc = false;
        }
    }

    return rc;
}

// mr flags: all flags except IBV_ACCESS_MW_BIND
bool create_memory_region(struct ibv_mr*& mr, void* mm, uint64_t mm_size, rdma_normal_data& normal_data) {

    mr = ibv_reg_mr(
        normal_data.pd, mm, mm_size,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC
    );

    if (mr == nullptr) {
        log_error << "memory registration failed: " << errno << std::endl;
        return false;
    }

    return true;
}

// bool create_memory_region_on_chip(struct ibv_mr*& mr, void* mm, uint64_t mm_size, rdma_normal_data& normal_data) {

//     mr = nullptr;

//     // for device do not support on-chip memory...
//     if (!k_max_device_memory_size) {
//         return create_memory_region(mr, mm, mm_size, normal_data);
//     }

//     /* Device memory allocation request */
//     struct ibv_alloc_dm_attr dm_attr;
//     memset(&dm_attr, 0, sizeof(dm_attr));
//     dm_attr.length = mm_size;
//     struct ibv_dm* dm = ibv_alloc_dm(normal_data.ctx, &dm_attr);
//     if (!dm) {
//         log_error << "allocate on-chip memory failed" << std::endl;
//         return false;
//     }

//     /* Device memory registration as memory region */
//     unsigned int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
//                           IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC |
//                           IBV_ACCESS_ZERO_BASED;
//     mr = ibv_reg_dm_mr(normal_data.pd, dm, 0, mm_size, access);
//     if (mr == nullptr) {
//         log_error << "memory registration failed" << std::endl;
//         return false;
//     }

//     // init zero
//     char* buffer = (char*)malloc(mm_size);
//     memset(buffer, 0, mm_size);

//     ibv_memcpy_to_dm(dm, 0, (const void*)buffer, mm_size);

//     free(buffer);

//     return true;
// }

bool create_queue_pair(
    ibv_qp*& qp, ibv_qp_type mode, ibv_cq* send_cq, ibv_cq* recv_cq, rdma_normal_data& normal_data,
    uint32_t qps_max_depth, uint32_t max_inline_data
) {

    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_type = mode;
    attr.sq_sig_all = 0;
    attr.send_cq = send_cq;
    attr.recv_cq = recv_cq;
    attr.qp_context = normal_data.ctx;

    // if (mode == IBV_QPT_RC) {
    //     attr.comp_mask = IBV_QP_INIT_ATTR_CREATE_FLAGS | IBV_QP_INIT_ATTR_PD | IBV_EXP_QP_INIT_ATTR_ATOMICS_ARG;
    //     attr.max_atomic_arg = 32;
    // } else {
    //     attr.comp_mask = IBV_QP_INIT_ATTR_PD;
    // }

    attr.cap.max_send_wr = qps_max_depth;
    attr.cap.max_recv_wr = qps_max_depth;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.cap.max_inline_data = max_inline_data;

    qp = ibv_create_qp(normal_data.pd, &attr);
    if (!qp) {
        log_error << "failed to create QP" << std::endl;
        return false;
    }

    // log_info << "Create Queue Pair with Num = " << qp->qp_num << std::endl;

    return true;
}

bool create_queue_pair(
    ibv_qp*& qp, ibv_qp_type mode, struct ibv_cq* cq, rdma_normal_data& normal_data,
    uint32_t qps_max_depth, uint32_t max_inline_data
) {
    return create_queue_pair(qp, mode, cq, cq, normal_data, qps_max_depth, max_inline_data);
}

void fill_ah_attr(
    ibv_ah_attr* attr, uint32_t remote_lid, uint8_t* remote_gid, uint8_t ib_port, int gid_index
) {

    (void)remote_gid;

    memset(attr, 0, sizeof(ibv_ah_attr));
    attr->dlid = remote_lid;
    attr->sl = 0;
    attr->src_path_bits = 0;
    attr->port_num = ib_port;

    // attr->is_global = 0;

    // fill ah_attr with GRH
    if (gid_index >= 0) {
        attr->is_global = 1;
        memcpy(&attr->grh.dgid, remote_gid, 16);
        attr->grh.flow_label = 0;
        attr->grh.hop_limit = 1;
        attr->grh.sgid_index = gid_index;
        attr->grh.traffic_class = 0;
    }
}

}