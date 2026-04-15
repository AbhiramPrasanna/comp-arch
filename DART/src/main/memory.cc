#include <cstdint>

#include "gflags/gflags.h"

#include "log/log.hpp"
#include "measure/measure.hpp"
#include "backtrace/backtrace.hpp"
#include "rdma/rdma.hpp"
#include "ycsb/ycsb.hpp"
#include "prheart/prheart.hpp"

// some factors here
#define SKIP_TABLE
#ifdef SKIP_TABLE
#include "race/race.h"
#endif

// connection
DEFINE_string(monitor_addr, "0.0.0.0:9898", "host:port");

// hardware
DEFINE_uint64(nic_index, 1, "index of nic");
DEFINE_uint64(ib_port, 1, "port of ib");



int main(int argc, char** argv) {

    InstallSignalHandlers();

    bool result;
    int iresult;


    // gflags
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    vec<gflags::CommandLineFlagInfo> all_flags;
    gflags::GetAllFlags(&all_flags);
    for (const auto& f : all_flags)
        if (f.current_value.size())
            log_debug << "gflag: " << f.name << " = " << f.current_value << std::endl;


    // connect to the monitor
    sockets::SocketConnection sock_conn;
    result = sock_conn.client_connect(FLAGS_monitor_addr);
    if (!result) {
        log_error << "connect to monitor error" << std::endl;
        sock_conn.disconnect();
        return 0;
    }

    // get permanent memory machine index
    uint32_t memory_machine_num, compute_machine_num, thread_num_per_compute, memory_size_mb, mem_ind;
    result = sock_conn.sock_send_u32(0);
    result = sock_conn.sock_read_u32(memory_machine_num);
    result = sock_conn.sock_read_u32(compute_machine_num);
    result = sock_conn.sock_read_u32(thread_num_per_compute);
    result = sock_conn.sock_read_u32(memory_size_mb);
    result = sock_conn.sock_read_u32(mem_ind);
    log_info << "this memory node is No." << mem_ind + 1 << std::endl;

#ifdef SKIP_TABLE
    log_info << "start skip table (begin)!" << std::endl;
    RACE::Config config;
    RACE::Server ser(FLAGS_nic_index, config);
    log_info << "start skip table! (end)" << std::endl;
#endif

    // all qps and rcds
    RDMA::RDMAMemoryRegion mr;
    RDMA::RDMAQueuePair qps[compute_machine_num * thread_num_per_compute];
    RDMA::rdma_connect_data rcds[compute_machine_num * thread_num_per_compute];
    #define com_thre_di(com, thread) ((com) + (thread) * compute_machine_num)

    // pre-allocate the memory region for RDMA connections
    // all RDMA connections will use the same mr
    result = mr.create(memory_size_mb * 1_MiB, FLAGS_nic_index, FLAGS_ib_port);
    if (!result) {
        log_error << "server create mr error" << std::endl;
        return 0;
    }

    // all qps and rcds create
    for (int i = 0; i < compute_machine_num * thread_num_per_compute; ++i) {
        result = qps[i].create(mr);
        if (!result) {
            log_error << "server create qp error" << std::endl;
            return 0;
        }
        rcds[i] = qps[i].generate_connect_data();
    }

    // send the connect data to monitor
    for (int com_ind = 0; com_ind < compute_machine_num; ++com_ind) {
    for (int thread_ind = 0; thread_ind < thread_num_per_compute; ++thread_ind) {
        result = RDMA::tcp_send_rdma_connect_data(
            sock_conn, mem_ind, com_ind,
            thread_ind, rcds[com_thre_di(com_ind, thread_ind)]
        );
        if (!result) {
            log_error << "tcp_send_rdma_connect_data error" << std::endl;
            sock_conn.disconnect();
            return 0;
        }
    }}

    // get all compute connection metadata
    RDMA::RDMAConnectionMetadata com_metadata(
        memory_machine_num, compute_machine_num, thread_num_per_compute
    );
    for (uint32_t _ = 0; _ < compute_machine_num * thread_num_per_compute; ++_) {
        uint32_t mem_ind_read, com_ind_read, thread_ind_read;
        RDMA::rdma_connect_data tmp_conn;
        result = RDMA::tcp_receive_rdma_connect_data(
            sock_conn,
            mem_ind_read, com_ind_read, thread_ind_read,
            tmp_conn
        );
        if (!result) {
            log_error << "tcp_receive_rdma_connect_data error" << std::endl;
            sock_conn.disconnect();
            return 0;
        }
        if (mem_ind_read != mem_ind) {
            log_cyan << mem_ind_read << " " << com_ind_read << " " << thread_ind_read << std::endl;
            log_error << "memory index error: " << mem_ind_read << " != " << mem_ind << std::endl;
            sock_conn.disconnect();
            return 0;
        }
        com_metadata(mem_ind_read, com_ind_read, thread_ind_read) = tmp_conn;
    }

    // create RDMA connections
    bool flag = true;
    RDMA::RDMAConnection* rdma_conn_list = new RDMA::RDMAConnection [compute_machine_num * thread_num_per_compute];

    for (uint64_t com_ind = 0; com_ind < compute_machine_num; ++com_ind) {
    for (uint64_t thread_ind = 0; thread_ind < thread_num_per_compute; ++thread_ind) {
        result = rdma_conn_list[com_thre_di(com_ind, thread_ind)].connect(
            qps[com_thre_di(com_ind, thread_ind)], com_metadata(mem_ind, com_ind, thread_ind)
        );
        if (!result) {
            flag = false;
            log_error << "to compute " << com_ind + 1 << " thread " << thread_ind + 1 << " connection error" << std::endl;
        } else {
            log_info << "to compute " << com_ind + 1 << " thread " << thread_ind + 1 << " connected" << std::endl;
        }
    }}

    // if RDMA connection failed, return 0
    // this must be done after the disconnection of socket, since socket did not go wrong
    if (!flag) {
        sock_conn.disconnect();
        delete[] rdma_conn_list;
        return 0;
    }

    // set root to 0
    if (mem_ind == 0)
        memset((void*)mr.mr->addr, 0, type_to_size(prheart::PrheartNodeType::Node256));

    // ready
    log_purple << "Memory node No." << mem_ind + 1 << " ready." << std::endl;
    result = sock_conn.sock_send_u32(200);
    if (!result) {
        log_error << "send ready error" << std::endl;
        sock_conn.disconnect();
        delete[] rdma_conn_list;
        return 0;
    }

    // just do nothing (waiting the end)
    log_warn << "waiting the end..." << std::endl;

    // disconnect
    uint32_t end = 0;
    sock_conn.sock_read_u32(end);
    if (end != 999) {
        log_error << "end error, monitor sent end = " << end << std::endl;
    }
    sock_conn.disconnect();

    // free all
    delete[] rdma_conn_list;
    return 0;
}
