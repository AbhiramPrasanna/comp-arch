#include <cstdint>
#include <thread>

#include "gflags/gflags.h"

#include "log/log.hpp"
#include "measure/measure.hpp"
#include "backtrace/backtrace.hpp"
#include "rdma/rdma.hpp"

#define MEMORY_TYPE 0
#define COMPUTE_TYPE 1

DEFINE_string(monitor_addr, "0.0.0.0:9898", "host:port");
DEFINE_uint64(memory_num, 1, "total memory machine number");
DEFINE_uint64(compute_num, 1, "total compute machine number");
DEFINE_uint64(load_thread_num, 2, "load thread, only for run (preload) (FLAGS_load_thread = max_thread_num * compute_machine)");
DEFINE_uint64(run_thread_num, 2, "thread per compute_machine (FLAGS_load_thread = max_thread_num * compute_machine)");
DEFINE_uint64(coro_num, 2, "all thread same, so add it here");
DEFINE_uint64(run_max_request, uint64_t(-1), "max number of run requests");

DEFINE_uint64(test_func, 1, "test function index");
DEFINE_uint64(payload_byte, 256, "payload (different usage in different test_func)");
DEFINE_uint64(epoch, 1, "epoch (different usage in different test_func)");
DEFINE_uint64(percent, 50, "percent (different usage in different test_func)");
DEFINE_uint64(bucket, 256, "bucket (different usage in different test_func)");
DEFINE_string(workload_prefix, "./workload/split/", "path of workload dir");
DEFINE_string(workload_load, "please_specify", "name of load-workload file");
DEFINE_string(workload_run, "please_specify", "name of run-workload file");

DEFINE_uint64(mem_mb, 1024, "MiB of memory buff size");
DEFINE_uint64(th_mb, 0, "MiB of maximum memory per thread");
DEFINE_uint64(th_kb, 0, "KiB of maximum memory per thread");
DEFINE_uint64(th_b, 0, "Byte of maximum memory per thread");
uint64_t NEW_FLAGS_th_b;
constexpr uint64_t NEW_FLAGS_bytes_default = 2_KiB;

int main(int argc, char** argv) {

    InstallSignalHandlers();

    bool result;
    int iresult;
    bool error_occurred = false;


    // gflags
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    vec<gflags::CommandLineFlagInfo> all_flags;
    gflags::GetAllFlags(&all_flags);
    for (const auto& f : all_flags)
        if (f.current_value.size())
            log_debug << "gflag: " << f.name << " = " << f.current_value << std::endl;
    NEW_FLAGS_th_b = FLAGS_th_mb * 1_MiB + FLAGS_th_kb * 1_KiB + FLAGS_th_b;
    if (NEW_FLAGS_th_b == 0) NEW_FLAGS_th_b = NEW_FLAGS_bytes_default;
    log_debug << "gflag (NEW): thread bytes size = " << readable_byte(NEW_FLAGS_th_b) << std::endl;

    // check FLAGS
    if (FLAGS_memory_num <= 0 || FLAGS_compute_num <= 0 || FLAGS_load_thread_num <= 0 || FLAGS_run_thread_num <= 0) {
        log_error << "FLAGS_memory_num, FLAGS_compute_num, FLAGS_load/run_thread_num should be positive" << std::endl;
        error_occurred = true;
        return 0;
    }
    if (FLAGS_memory_num >= DM::MAX_MEMORY_NUM) {
        log_error << "FLAGS_memory_num should be less than " << DM::MAX_MEMORY_NUM << std::endl;
        error_occurred = true;
        return 0;
    }

    u32 max_thread_num = std::max(FLAGS_load_thread_num, FLAGS_run_thread_num);

    // start server
    sockets::SocketServerConnection monitor_server;
    result = monitor_server.server_init(FLAGS_monitor_addr);
    if (!result) {
        log_error << "server SocketConnection error" << std::endl;
        error_occurred = true;
        return 0;
    }

    log_purple << "OK. monitor_server started." << std::endl;

    // save all memory/compute nodes' indices
    uint32_t compute_index = 0, memory_index = 0;
    struct {
        uint32_t type;
        uint32_t index;
    } all_list[FLAGS_memory_num + FLAGS_compute_num];

    // connect to all machines
    for (uint32_t c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {

        // listen to client's tcp sockets
        result = monitor_server.server_listen();
        if (!result) {
            log_error << "server SocketConnection error" << std::endl;
            error_occurred = true;
        }
        auto& sock = monitor_server.get_latest_socket_connetion();
        uint32_t type;
        result = sock.sock_read_u32(type);
        if (!result) {
            log_error << "tcp_receive_id_data error" << std::endl;
            error_occurred = true;
        }
        all_list[c].type = type;
        if (type == MEMORY_TYPE) {
            // send to memory
            result = sock.sock_send_u32(FLAGS_memory_num) & result;
            result = sock.sock_send_u32(FLAGS_compute_num) & result;
            result = sock.sock_send_u32(max_thread_num) & result;
            result = sock.sock_send_u32(FLAGS_mem_mb) & result;
            result = sock.sock_send_u32(memory_index) & result;
            if (!result) {
                log_error << "memory_index send error" << std::endl;
                error_occurred = true;
            }
            all_list[c].index = memory_index;
            memory_index++;
            log_info << "Monitor listen to memory node " << all_list[c].index + 1 << "/" << FLAGS_memory_num << " done, Total: " << c + 1 << "/" << FLAGS_memory_num + FLAGS_compute_num << std::endl;
        } else {
            // send to compute
            result = sock.sock_send_u32(FLAGS_memory_num);
            result = sock.sock_send_u32(FLAGS_compute_num) & result;
            result = sock.sock_send_u32(FLAGS_load_thread_num) & result;
            result = sock.sock_send_u32(FLAGS_run_thread_num) & result;
            result = sock.sock_send_u32(FLAGS_coro_num) & result;
            result = sock.sock_send_u32(FLAGS_run_max_request) & result;
            result = sock.sock_send_u32(NEW_FLAGS_th_b) & result;
            result = sock.sock_send_u32(FLAGS_test_func) & result;
            result = sock.sock_send_u32(FLAGS_payload_byte) & result;
            result = sock.sock_send_u32(FLAGS_epoch) & result;
            result = sock.sock_send_u32(FLAGS_percent) & result;
            result = sock.sock_send_u32(FLAGS_bucket) & result;

            uint32_t size_1 = FLAGS_workload_prefix.size(), size_2 = FLAGS_workload_load.size(), size_3 = FLAGS_workload_run.size();
            result = sock.sock_send_u32(size_1) & result;
            result = sock.sock_send_data(size_1, (char*)FLAGS_workload_prefix.c_str()) & result;
            result = sock.sock_send_u32(size_2) & result;
            result = sock.sock_send_data(size_2, (char*)FLAGS_workload_load.c_str()) & result;
            result = sock.sock_send_u32(size_3) & result;
            result = sock.sock_send_data(size_3, (char*)FLAGS_workload_run.c_str()) & result;
            
            result = sock.sock_send_u32(compute_index) & result;
            if (!result) {
                log_error << "compute_index send error" << std::endl;
                error_occurred = true;
            }
            all_list[c].index = compute_index;
            compute_index++;
            log_info << "Monitor listen to compute node " << all_list[c].index + 1 << "/" << FLAGS_compute_num << " done, Total: " << c + 1 << "/" << FLAGS_memory_num + FLAGS_compute_num << std::endl;
        }

    }

    log_purple << "OK. Listen to all memory/compute nodes and send indices done." << std::endl;

    // save all metadata
    RDMA::RDMAConnectionMetadata
        mem_metas(FLAGS_memory_num, FLAGS_compute_num, max_thread_num),
        com_metas(FLAGS_memory_num, FLAGS_compute_num, max_thread_num);

    // receive all memory nodes' connection data (memory_num * compute_num * thread_num)
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != MEMORY_TYPE) continue;
        auto& sock = monitor_server.get_socket_connetion(c);
        uint32_t men_ind = all_list[c].index;
        for (int _ = 0; _ < FLAGS_compute_num * max_thread_num; ++_) {
            uint32_t mem_ind_read, com_ind_read, thread_ind_read;
            RDMA::rdma_connect_data conn;
            result = RDMA::tcp_receive_rdma_connect_data(sock, mem_ind_read, com_ind_read, thread_ind_read, conn);
            if (!result) {
                log_error << "tcp_receive_rdma_connect_data error" << std::endl;
                error_occurred = true;
            }
            if (mem_ind_read != men_ind) {
                log_error << "memory_index check error" << std::endl;
                error_occurred = true;
            }
            mem_metas(mem_ind_read, com_ind_read, thread_ind_read) = conn;
            log_debug << "conn.addr = " << hex_str(conn.addr) << ", " << readable_byte(conn.size) << ", (m, c, t) = " << mem_ind_read + 1 << ", " << com_ind_read + 1 << ", " << thread_ind_read + 1 << std::endl;
        }
        log_info << "Memory node " << men_ind + 1 << "/" << FLAGS_memory_num << " connection data (MR & QP) received." << std::endl;
    }

    log_purple << "OK. Read all memory nodes' rdma_connect_data done." << std::endl;

    // receive all compute nodes' connection data (memory_num * compute_num * thread_num)
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != COMPUTE_TYPE) continue;
        auto& sock = monitor_server.get_socket_connetion(c);
        uint32_t com_ind = all_list[c].index;
        for (int _ = 0; _ < FLAGS_memory_num * max_thread_num; ++_) {
            uint32_t mem_ind_read, com_ind_read, thread_ind_read;
            RDMA::rdma_connect_data conn;
            result = RDMA::tcp_receive_rdma_connect_data(sock, mem_ind_read, com_ind_read, thread_ind_read, conn);
            if (!result) {
                log_error << "tcp_receive_rdma_connect_data error" << std::endl;
                error_occurred = true;
            }
            if (com_ind_read != com_ind) {
                log_error << "memory_index check error" << std::endl;
                error_occurred = true;
            }
            com_metas(mem_ind_read, com_ind_read, thread_ind_read) = conn;
            log_debug << "conn.addr = " << hex_str(conn.addr) << ", " << readable_byte(conn.size) << ", (m, c, t) = " << mem_ind_read + 1 << ", " << com_ind_read + 1 << ", " << thread_ind_read + 1 << std::endl;
        }
        log_info << "Compute node " << com_ind + 1 << "/" << FLAGS_memory_num << " connection data (MR & QP) received." << std::endl;
    }

    log_purple << "OK. Read all compute nodes' rdma_connect_data done." << std::endl;

    // send to all memory nodes
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != MEMORY_TYPE) continue;
        auto& sock = monitor_server.get_socket_connetion(c);
        uint32_t mem_ind = all_list[c].index;
        log_cyan << "sending to " << mem_ind << std::endl;
        for (uint32_t com_ind = 0; com_ind < FLAGS_compute_num; ++com_ind) {
        for (uint32_t thread_ind = 0; thread_ind < max_thread_num; ++thread_ind) {
            log_debug << mem_ind << " " << com_ind << " " << thread_ind << std::endl;
            result = RDMA::tcp_send_rdma_connect_data(
                sock,
                mem_ind, com_ind, thread_ind,
                com_metas(mem_ind, com_ind, thread_ind)
            );
            std::this_thread::sleep_for(1ms);  // sometimes data send error, ... why?
            if (!result) {
                log_error << "tcp_send_rdma_connect_data error" << std::endl;
                error_occurred = true;
            }
        }}
        log_info << "Memory node " << mem_ind + 1 << "/" << FLAGS_compute_num << " connection data (MR & QP) sent." << std::endl;
    }

    log_purple << "OK. Send metadata to all memory nodes done." << std::endl;

    // send to all compute nodes
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != COMPUTE_TYPE) continue;
        auto& sock = monitor_server.get_socket_connetion(c);
        uint32_t com_ind = all_list[c].index;
        log_cyan << "sending to " << com_ind << std::endl;
        for (uint32_t mem_ind = 0; mem_ind < FLAGS_memory_num; ++mem_ind) {
        for (uint32_t thread_ind = 0; thread_ind < max_thread_num; ++thread_ind) {
            // if not add this, the data will be changed... (only between remote machines) why?
            log_debug << mem_ind << " " << com_ind << " " << thread_ind << std::endl;
            result = RDMA::tcp_send_rdma_connect_data(
                sock,
                mem_ind, com_ind, thread_ind,
                mem_metas(mem_ind, com_ind, thread_ind)
            );
            std::this_thread::sleep_for(1ms);  // sometimes data send error, ... why?
            if (!result) {
                log_error << "tcp_send_rdma_connect_data error" << std::endl;
                error_occurred = true;
            }
        }}
        log_info << "Compute node " << com_ind + 1 << "/" << FLAGS_compute_num << " connection data (MR & QP) sent." << std::endl;
    }

    log_purple << "OK. Send metadata to all compute nodes done." << std::endl;

    // till all nodes' connections done
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        uint32_t ind = all_list[c].index;
        auto& sock = monitor_server.get_socket_connetion(c);
        uint32_t n = 0;
        result = sock.sock_read_u32(n);
        if (!result) {
            log_error << "sock_read_u32 error" << std::endl;
            error_occurred = true;
        }
        if (all_list[c].type == MEMORY_TYPE) {
            if (n != 200) {
                log_error << "memory node No." << ind + 1 << " start error" << std::endl;
                error_occurred = true;
            }
            log_info << "Memory node " << ind + 1 << "/" << FLAGS_compute_num << " ready." << std::endl;
        } else {
            if (n != 200) {
                log_error << "compute node No." << ind + 1 << " start error" << std::endl;
                error_occurred = true;
            }
            log_info << "Compute node " << ind + 1 << "/" << FLAGS_compute_num << " ready." << std::endl;
        }
    }

    log_purple << "OK. Read all nodes' readiness done." << std::endl;

    // send 200 to start all compute nodes
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != COMPUTE_TYPE) continue;
        auto& sock = monitor_server.get_socket_connetion(c);
        if (error_occurred)
            result = sock.sock_send_u32(404);  // error!
        else
            result = sock.sock_send_u32(200);  // go!
        if (!result) {
            log_error << "sock_send_u32 error" << std::endl;
            error_occurred = true;
        }
    }

    // error then end
    if (error_occurred) {
        log_error << "error_occurred, please check previous log" << std::endl;
        monitor_server.disconnect();
        return 0;
    } else {
        log_purple << "OK. Send \"200\"(Start) to all compute nodes done." << std::endl;
    }

    // if run, first load & prepare (600 700 800 900)
    if (FLAGS_test_func == 0) {
        for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
            if (all_list[c].type != COMPUTE_TYPE) continue;
            auto& sock = monitor_server.get_socket_connetion(c);
            uint32_t n = 0;
            result = sock.sock_read_u32(n);  // error!
            if (!result) {
                log_error << "sock_send_u32 error" << std::endl;
                error_occurred = true;
            }
            if (n != 600) {
                log_error << "compute node No." << all_list[c].index + 1 << " load error" << std::endl;
                error_occurred = true;
            }
        }

        log_warn << "load done" << std::endl;

        for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != COMPUTE_TYPE) continue;
            auto& sock = monitor_server.get_socket_connetion(c);
            if (error_occurred)
                result = sock.sock_send_u32(404);  // error!
            else
                result = sock.sock_send_u32(700);  // go!
            if (!result) {
                log_error << "sock_send_u32 error" << std::endl;
                error_occurred = true;
            }
        }

        if (error_occurred) {
            log_error << "error_occurred, please check previous log" << std::endl;
            monitor_server.disconnect();
            return 0;
        } else {
            log_warn << "start to prepare" << std::endl;
        }

        for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
            if (all_list[c].type != COMPUTE_TYPE) continue;
            auto& sock = monitor_server.get_socket_connetion(c);
            uint32_t n = 0;
            result = sock.sock_read_u32(n);  // error!
            if (!result) {
                log_error << "sock_send_u32 error" << std::endl;
                error_occurred = true;
            }
            if (n != 800) {
                log_error << "compute node No." << all_list[c].index + 1 << " prepare error" << std::endl;
                error_occurred = true;
            }
        }

        log_warn << "prepare done" << std::endl;

        for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != COMPUTE_TYPE) continue;
            auto& sock = monitor_server.get_socket_connetion(c);
            if (error_occurred)
                result = sock.sock_send_u32(404);  // error!
            else
                result = sock.sock_send_u32(900);  // go!
            if (!result) {
                log_error << "sock_send_u32 error" << std::endl;
                error_occurred = true;
            }
        }

        // error then end
        if (error_occurred) {
            log_error << "error_occurred, please check previous log" << std::endl;
            monitor_server.disconnect();
            return 0;
        } else {
            log_warn << "start to run" << std::endl;
        }
    }


    // waiting for results
    log_warn << "waiting the for the results..." << std::endl;

    // get result
    double thps[FLAGS_compute_num], lats[FLAGS_compute_num], bans[FLAGS_compute_num];
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        if (all_list[c].type != COMPUTE_TYPE) continue;
        uint32_t ind = all_list[c].index;
        auto& sock = monitor_server.get_socket_connetion(c);
        double thp, lat, ban;
        result = sock.sock_read_double(thp);
        result = sock.sock_read_double(lat) & result;
        result = sock.sock_read_double(ban) & result;
        if (!result) {
            log_error << "Compute node " << ind + 1 << "/" << FLAGS_compute_num << "sock_read_double error" << std::endl;
        }
        thps[ind] = thp;
        lats[ind] = lat;
        bans[ind] = ban;
    }

    // print result
    double total_thp = 0, total_lat = 0, total_ban = 0;
    for (int c = 0; c < FLAGS_compute_num; ++c) {
        log_info << "Compute node " << c + 1 << "/" << FLAGS_compute_num << " thp = " << thps[c] << " MOps, lat = " << lats[c] << " us, ban = " << bans[c] << " Gbps" << std::endl;
        total_thp += thps[c];
        total_lat += lats[c];
        total_ban += bans[c];
    }
    log_info << "Total throughput = " << total_thp << " MOps" << std::endl;
    log_info << "Average latency = " << total_lat / FLAGS_compute_num << " us" << std::endl;
    log_info << "Total bandwidth = " << total_ban << " Gbps" << std::endl;

    // send end signal
    for (int c = 0; c < FLAGS_memory_num + FLAGS_compute_num; ++c) {
        auto& sock = monitor_server.get_socket_connetion(c);
        result = sock.sock_send_u32(999);
        if (!result) {
            log_error << "sock_send_u32 error" << std::endl;
        }
    }
    log_purple << "OK. Send \"999\"(End) to all nodes done." << std::endl;

    // end
    std::this_thread::sleep_for(1s);
    monitor_server.disconnect();
    return 0;
}