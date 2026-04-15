#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <random>

#include "boost/coroutine2/all.hpp"

#include "gflags/gflags.h"

#include "log/log.hpp"
#include "measure/measure.hpp"
#include "backtrace/backtrace.hpp"
#include "rdma/rdma.hpp"
#include "ycsb/ycsb.hpp"
#include "prheart/prheart.hpp"

// some factors here
// #define LEVEL_COUNT
// #define KEY_SIZE 8
// #define PRINT_TREE

// #define DYNAMIC
#define SKIP_TABLE
#ifdef SKIP_TABLE
#include "race/race.h"
#include "race/common.h"
#endif

// ---- Phase 3: skip table construction policy ----------------------------
// Exactly ONE of the following should be defined.
//
//   POLICY_STATIC      — original DART behaviour: DFS structural analysis
//                        (build skip entries for nodes at fixed prefix depths).
//                        Does NOT require ENABLE_ACCESS_TRACKING.
//
//   POLICY_HOTNESS     — insert skip entries for the top-K nodes ranked by
//                        raw RDMA read frequency during the load phase.
//                        Requires ENABLE_ACCESS_TRACKING in art-node.cc.
//
//   POLICY_CRITICALITY — insert skip entries for the top-K nodes ranked by
//                        cumulative traversal depth (depth_sum), i.e. the
//                        nodes whose skip entry saves the most RTTs in total.
//                        Requires ENABLE_ACCESS_TRACKING in art-node.cc.
//
//   POLICY_HYBRID      — weighted mix: 0.5*hotness + 0.5*criticality.
//                        Requires ENABLE_ACCESS_TRACKING in art-node.cc.
//
// To run a Phase 3 experiment: comment out POLICY_STATIC and uncomment one
// of the other three, recompile, and compare throughput / latency.
// #define POLICY_STATIC
// #define POLICY_HOTNESS
#define POLICY_CRITICALITY
// #define POLICY_HYBRID

// Maximum skip-table entries for policy-based construction.
// Tuning this controls cache pressure experiments.
static constexpr uint64_t POLICY_MAX_ENTRIES = 5000;
// -------------------------------------------------------------------------

const char* ips[] = {"10.30.1.9"};  // memory node IP

template<typename T>
using coro = boost::coroutines2::coroutine<T>;

// connection
DEFINE_string(monitor_addr, "0.0.0.0:9898", "host:port");

// hardware
DEFINE_uint64(nic_index, 0, "index of nic");
DEFINE_uint64(ib_port, 1, "port of ib");
DEFINE_uint64(numa_node_total_num, 2, "total groups of numa node");
DEFINE_uint64(numa_node_group, 0, "group of numa node");

bool is_email = false;

extern __thread uint64_t rtt;
extern __thread uint64_t access_size;
extern __thread uint64_t wrong;
extern __thread uint64_t fail;
extern __thread uint64_t duplicate;
#define MAX_LEVEL 32
extern __thread uint64_t start_level[MAX_LEVEL];
extern __thread uint64_t fail_level[MAX_LEVEL];
extern __thread uint64_t retry_level[MAX_LEVEL];
extern uint64_t node_num[MAX_LEVEL];
extern uint64_t leaf_num[MAX_LEVEL];
extern uint64_t node_prefix_num[MAX_LEVEL];
extern uint64_t leaf_prefix_num[MAX_LEVEL];
extern uint64_t shortcut_num[MAX_LEVEL];
extern int costs[MAX_LEVEL];
extern uint64_t node_type_num[8];

// different thread, so __thread is useless
#define MAX_TRHEAD_NUM 128
bool alloced_before[MAX_TRHEAD_NUM];
uint64_t alloc_now_pos[MAX_TRHEAD_NUM];

// test functions
typedef void(fnType)(
    uint32_t compute_machine_num,
    uint32_t memory_machine_num,
    uint32_t total_thread_num,
    uint32_t used_thread_num,
    uint32_t coro_num,
    uint32_t compute_index,
    uint32_t thread_index,
    uint32_t payload_byte,  // different functions have different meanings
    uint32_t epoch_num,  // different functions have different meanings
    uint32_t percent_num,  // different functions have different meanings
    uint32_t bucket_num,  // different functions have different meanings
    RDMA::RDMAConnection* memory_connections,
    counter::TimeCounter& time_counter,
    YCSB::FileLoader& file_loader_load,
    YCSB::FileLoader& file_loader_run,
    std::vector<RACE::Client*> race_cli,
    std::vector<RACE::rdma_client*> rdma_cli
);

fnType test_ycsb_load;
fnType test_ycsb_run;

void test_ycsb_load(
    uint32_t memory_machine_num,
    uint32_t compute_machine_num,
    uint32_t total_thread_num,
    uint32_t used_thread_num,
    uint32_t coro_num,
    uint32_t compute_index,
    uint32_t thread_index,
    uint32_t payload_byte,
    uint32_t epoch_num,
    uint32_t percent_num,
    uint32_t bucket_num,
    RDMA::RDMAConnection* memory_connections,
    counter::TimeCounter& time_counter,
    YCSB::FileLoader& file_loader_load,
    YCSB::FileLoader& file_loader_run,
    std::vector<RACE::Client*> race_cli,
    std::vector<RACE::rdma_client*> rdma_cli
) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(thread_index * FLAGS_numa_node_total_num + FLAGS_numa_node_group, &mask);
    sched_setaffinity(0, sizeof(mask), &mask);

    DM::DisaggregatedMemoryController dmc(
        memory_connections,
        memory_machine_num, compute_machine_num, total_thread_num,
        compute_index, thread_index
    );

    if (!dmc.check_local_memory()) {
        log_error << "local memory check error" << std::endl;
        return;
    }

    // common coroutine implementation
    prheart::PrheartTree prheart_tree(
        &dmc,
        dmc.get_root_start_fptr(),
        dmc.get_alloc_start_fptr(), dmc.get_alloc_end_fptr(),
        dmc.get_local_start_ptr(),
        dmc.get_local_start_ptr() + dmc.get_local_size(),
        bucket_num, NULL, 
        race_cli,
        rdma_cli,
        memory_machine_num
    );

    rtt = access_size = 0;

    if (alloced_before[thread_index]) {
        prheart_tree.alloc_now_fptr = alloc_now_pos[thread_index];
        // log_purple << "alloc_now_pos: " << hex_str(alloc_now_pos[thread_index]) << std::endl;
    }

    auto search_it = [&](span key, str& result) -> void {
        auto flag = prheart_tree.search(key);
        if (flag == false) {
            #ifdef PRINT_OUTPUT
            log_error << "search key " << key_type::to_string(key) << " false" << std::dec << std::endl;
            #endif
        }
    };
    auto insert_it = [&](span key, span value) -> void {
        auto flag = prheart_tree.insert(key, value);
        if (flag == false) {
            #ifdef PRINT_OUTPUT
            log_error << "insert key " << key_type::to_string(key) << " false" << std::dec << std::endl;
            #endif
        }
    };
    auto update_it = [&](span key, span value) -> void {
        auto flag = prheart_tree.update(key, value);
        if (flag == false) {
            #ifdef PRINT_OUTPUT
            log_error << "update key " << key_type::to_string(key) << " false" << std::dec << std::endl;
            #endif
        }
    };
    auto scan_it = [&](span start_key, span end_key, vec<str>& result_vec) -> void {
        auto flag = prheart_tree.scan(start_key, end_key, result_vec);
        if (flag == false) {
            #ifdef PRINT_OUTPUT
            log_error << "scan key " << key_type::to_string(start_key) << " false" << std::dec << std::endl;
            #endif
        }
    };
    auto remove_it = [&](span key) -> void {
        auto flag = prheart_tree.remove(key);
        if (flag == false) {
            #ifdef PRINT_OUTPUT
            log_error << "remove key " << key_type::to_string(key) << " false" << std::dec << std::endl;
            #endif
        }
    };

    YCSB::Benchmark ycsb;
    ycsb.register_read(search_it);
    ycsb.register_insert(insert_it);
    ycsb.register_update(update_it);
    ycsb.register_scan(scan_it);
    ycsb.register_remove(remove_it);
    ycsb.prepare_workload_file(
        file_loader_load,
        thread_index,
        used_thread_num
    );

    if (rdma_cli.size() == memory_machine_num) {
        for (int i = 0; i < memory_machine_num; ++i) {
            if (rdma_cli[i]) {
                rdma_cli[i]->run(race_cli[i]->start(used_thread_num));
                // log_info << "thread " << thread_index  << " start race" << std::endl;
            }       
        }      
    }

    time_counter.reset_time_counter();
    time_counter.start();

    ycsb.start_benchmark();

    alloc_now_pos[thread_index] = prheart_tree.alloc_now_fptr;
    alloced_before[thread_index] = true;

    time_counter.stop();
    time_counter.add_event_count(ycsb.get_event_count());
    // time_counter.add_event_count(ycsb.get_event_count_scan_use_multi());
    time_counter.set_rtt_count(rtt);
    time_counter.set_band_count(access_size);

    if (rdma_cli.size() == memory_machine_num) {
        for (int i = 0; i < memory_machine_num; ++i) {
            if (rdma_cli[i])
                rdma_cli[i]->run(race_cli[i]->stop());
        }
    }

    if (thread_index == 0) {
        #ifdef DYNAMIC
        for (uint32_t i=0; i<MAX_LEVEL ;++i) {
            log_info << "level " << i << " start: " << start_level[i] << " fail: " << fail_level[i] << " retry: " << retry_level[i] << std::endl;
        }
        #endif

        RDMA::Usage total_usage;
        for (int i = 0; i < memory_machine_num; ++i) {
            for (const auto& j : dmc(i).usage.read_size_times)
                total_usage.read_size_times[j.first] += j.second;
            for (const auto& j : dmc(i).usage.read_fail_size_times)
                total_usage.read_fail_size_times[j.first] += j.second;
            for (const auto& j : dmc(i).usage.write_size_times)
                total_usage.write_size_times[j.first] += j.second;
            for (const auto& j : dmc(i).usage.write_fail_size_times)
                total_usage.write_fail_size_times[j.first] += j.second;
            for (const auto& j : dmc(i).usage.cas_size_times)
                total_usage.cas_size_times[j.first] += j.second;
            for (const auto& j : dmc(i).usage.cas_fail_size_times)
                total_usage.cas_fail_size_times[j.first] += j.second;
        }
        total_usage.print();

        #ifdef PRINT_TREE
        log_purple << "waiting 5s to print tree..." << std::endl;
        sleep(5);
        prheart_tree.print_tree();
        #endif
    }

    return;
}


void test_ycsb_run(
    uint32_t memory_machine_num,
    uint32_t compute_machine_num,
    uint32_t total_thread_num,
    uint32_t used_thread_num,
    uint32_t coro_num,
    uint32_t compute_index,
    uint32_t thread_index,
    uint32_t payload_byte,
    uint32_t epoch_num,
    uint32_t percent_num,
    uint32_t bucket_num,
    RDMA::RDMAConnection* memory_connections,
    counter::TimeCounter& time_counter,
    YCSB::FileLoader& file_loader_load,
    YCSB::FileLoader& file_loader_run,
    std::vector<RACE::Client*> race_cli,
    std::vector<RACE::rdma_client*> rdma_cli
) {
    test_ycsb_load(
        memory_machine_num,
        compute_machine_num,
        total_thread_num,
        used_thread_num,
        coro_num,
        compute_index,
        thread_index,
        payload_byte,
        epoch_num,
        percent_num,
        bucket_num,
        memory_connections,
        time_counter,
        file_loader_run, // use run instead
        file_loader_run,
        race_cli,
        rdma_cli
    );

}

void create_skip_table(
    uint32_t memory_machine_num,
    uint32_t compute_machine_num,
    uint32_t total_thread_num,
    uint32_t used_thread_num,
    uint32_t coro_num,
    uint32_t compute_index,
    uint32_t thread_index,
    uint32_t bucket_num,
    RDMA::RDMAConnection* memory_connections,
    std::vector<RACE::Client*> race_cli,
    std::vector<RACE::rdma_client*> rdma_cli
) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(thread_index * FLAGS_numa_node_total_num + FLAGS_numa_node_group, &mask);
    // CPU_SET(thread_index, &mask);
    sched_setaffinity(0, sizeof(mask), &mask);

    DM::DisaggregatedMemoryController dmc(
        memory_connections,
        memory_machine_num, compute_machine_num, total_thread_num,
        compute_index, thread_index
    );

    if (!dmc.check_local_memory()) {
        log_error << "local memory check error" << std::endl;
        return;
    }
    prheart::PrheartTree prheart_tree(
        &dmc,
        dmc.get_root_start_fptr(),
        dmc.get_alloc_start_fptr(), dmc.get_alloc_end_fptr(),
        dmc.get_local_start_ptr(),
        dmc.get_local_start_ptr() + dmc.get_local_size(),
        bucket_num, NULL, 
        race_cli, rdma_cli,
        memory_machine_num
    );

    for (uint64_t i=0; i<memory_machine_num; ++i) {
        rdma_cli[i]->run(race_cli[i]->start(1));
    }

    // --- Phase 2: print load-phase access patterns before building skip table
    // This gives visibility into which nodes were hot / on the critical path
    // during the load workload, before any policy decision is made.
#ifdef ENABLE_ACCESS_TRACKING
    log_purple << "[Phase 2] Load-phase AccessTracker report:" << std::endl;
    prheart::AccessTracker::instance().print_summary(15, prheart::CachePolicy::HOTNESS);
    prheart::AccessTracker::instance().print_summary(15, prheart::CachePolicy::CRITICALITY);
    prheart::AccessTracker::instance().print_policy_comparison(100);
#endif

    // --- Phase 3: build skip table using the selected policy
#if defined(POLICY_STATIC) || (!defined(POLICY_HOTNESS) && !defined(POLICY_CRITICALITY) && !defined(POLICY_HYBRID))
    // Original DART DFS-based static analysis.
    for (uint64_t i = 0; i < MAX_LEVEL; ++i)
        shortcut_num[i] = 0;
    uint64_t shortcuts = prheart_tree.create_skip_table();

#elif defined(POLICY_HOTNESS)
    // Top-K nodes by raw read frequency during the load phase.
    uint64_t shortcuts = prheart_tree.create_skip_table_policy(
        prheart::CachePolicy::HOTNESS, POLICY_MAX_ENTRIES
    );

#elif defined(POLICY_CRITICALITY)
    // Top-K nodes by cumulative traversal depth (total RTT savings potential).
    uint64_t shortcuts = prheart_tree.create_skip_table_policy(
        prheart::CachePolicy::CRITICALITY, POLICY_MAX_ENTRIES
    );

#elif defined(POLICY_HYBRID)
    // Weighted mix of hotness and criticality (alpha = 0.5).
    uint64_t shortcuts = prheart_tree.create_skip_table_policy(
        prheart::CachePolicy::HYBRID, POLICY_MAX_ENTRIES
    );
#endif

    log_info << "num shortcuts:" << shortcuts << std::endl;
    for (uint64_t i = 0; i < MAX_LEVEL; ++i) {
        if (shortcut_num[i] > 0)
            log_info << "[depth " << i << "] shortcut num: " << shortcut_num[i] << std::endl;
    }
    
    for (uint64_t i=0; i<memory_machine_num; ++i) {
        rdma_cli[i]->run(race_cli[i]->stop());
    }
    
    return;
}

void dfs(
    uint32_t memory_machine_num,
    uint32_t compute_machine_num,
    uint32_t total_thread_num,
    uint32_t used_thread_num,
    uint32_t coro_num,
    uint32_t compute_index,
    uint32_t thread_index,
    uint32_t bucket_num,
    RDMA::RDMAConnection* memory_connections,
    RACE::Client* race_cli,
    RACE::rdma_client* rdma_cli
) {

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(thread_index * FLAGS_numa_node_total_num + FLAGS_numa_node_group, &mask);
    // CPU_SET(thread_index, &mask);
    sched_setaffinity(0, sizeof(mask), &mask);

    DM::DisaggregatedMemoryController dmc(
        memory_connections,
        memory_machine_num, compute_machine_num, total_thread_num,
        compute_index, thread_index
    );

    if (!dmc.check_local_memory()) {
        log_error << "local memory check error" << std::endl;
        return;
    }

    prheart::PrheartTree prheart_tree(
        &dmc,
        dmc.get_root_start_fptr(),
        dmc.get_alloc_start_fptr(), dmc.get_alloc_end_fptr(),
        dmc.get_local_start_ptr(),
        dmc.get_local_start_ptr() + dmc.get_local_size(),
        bucket_num, NULL
    );

    prheart_tree.cal_cost(is_email);
}

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

    // get permanent compute machine index
    uint32_t memory_machine_num, compute_machine_num, load_thread_num, run_thread_num, thread_num_per_compute, coro_num, run_max_request, thread_size_byte, com_ind;
    uint32_t test_func_num, payload_byte, epoch_num, percent_num, bucket_num;
    str workload_load_string, workload_run_string;
    result = sock_conn.sock_send_u32(1);
    result = sock_conn.sock_read_u32(memory_machine_num);
    result = sock_conn.sock_read_u32(compute_machine_num);
    result = sock_conn.sock_read_u32(load_thread_num);
    result = sock_conn.sock_read_u32(run_thread_num);
    result = sock_conn.sock_read_u32(coro_num);
    result = sock_conn.sock_read_u32(run_max_request);
    result = sock_conn.sock_read_u32(thread_size_byte);
    result = sock_conn.sock_read_u32(test_func_num);
    result = sock_conn.sock_read_u32(payload_byte);
    result = sock_conn.sock_read_u32(epoch_num);
    result = sock_conn.sock_read_u32(percent_num);
    result = sock_conn.sock_read_u32(bucket_num);
    thread_num_per_compute = std::max(load_thread_num, run_thread_num);

    uint32_t size_1, size_2, size_3;
    result = sock_conn.sock_read_u32(size_1) & result;
    char workload_prefix[size_1 + 1];
    result = sock_conn.sock_read_data(size_1, workload_prefix) & result;
    workload_prefix[size_1] = '\0';
    result = sock_conn.sock_read_u32(size_2) & result;
    char workload_load[size_2 + 1];
    result = sock_conn.sock_read_data(size_2, workload_load) & result;
    workload_load[size_2] = '\0';
    result = sock_conn.sock_read_u32(size_3) & result;
    char workload_run[size_3 + 1];
    result = sock_conn.sock_read_data(size_3, workload_run) & result;
    workload_run[size_3] = '\0';
    str workload_prefix_str(workload_prefix);
    str workload_load_str(workload_load);
    str workload_run_str(workload_run);
    workload_load_string = workload_prefix_str + workload_load_str;
    workload_run_string = workload_prefix_str + workload_run_str;
    std::cout << "workload_load_string: " << workload_load_string << std::endl;
    std::cout << "workload_run_string: " << workload_run_string << std::endl;
    
    result = sock_conn.sock_read_u32(com_ind);
    log_info << "this compute node is No." << com_ind + 1 << std::endl;

    vec<viw> file_path_result;
    YCSB::split(workload_load_string, file_path_result, '/');
    is_email = (file_path_result.back()[0] == 'm');
    log_info << "is_email = " << is_email << std::endl;

    // create connection for skip table
#ifdef SKIP_TABLE
    RACE::Config config;
    config.num_machine = compute_machine_num;
    config.num_cli = thread_num_per_compute;
    config.num_coro = coro_num;
    config.machine_id = com_ind;
    std::mutex dir_lock;
    std::thread ths[112];
    RACE::rdma_dev dev(FLAGS_nic_index, 1, 1);
    uint64_t cbuf_size = (1ul << 20) * 32;
    std::vector<std::vector<RACE::rdma_client *>> rdma_clis(thread_num_per_compute + 1, std::vector<RACE::rdma_client*>(memory_machine_num, nullptr));
    std::vector<std::vector<RACE::Client *>> clis;
    std::vector<std::vector<ibv_mr *>> lmrs(thread_num_per_compute * coro_num + 1, std::vector<ibv_mr *>(memory_machine_num, nullptr));
    std::vector<std::vector<RACE::rdma_conn *>> rdma_conns(thread_num_per_compute + 1, std::vector<RACE::rdma_conn *>(memory_machine_num, nullptr));
    std::vector<std::vector<RACE::rdma_conn *>> rdma_wowait_conns(thread_num_per_compute + 1, std::vector<RACE::rdma_conn *>(memory_machine_num, nullptr));
    char* mem_buf= (char *)malloc(cbuf_size * memory_machine_num * (thread_num_per_compute * coro_num + 1));

    for (uint64_t i = 0; i < thread_num_per_compute; i++)
    {
        for (uint64_t m = 0; m < memory_machine_num; m++) {
            rdma_clis[i][m] = new RACE::rdma_client(dev, RACE::so_qp_cap, RACE::rdma_default_tempmp_size, 256, 64);
            rdma_conns[i][m] = rdma_clis[i][m]->connect(ips[m]);
            assert(rdma_conns[i][m] != nullptr);
            rdma_wowait_conns[i][m] = rdma_clis[i][m]->connect(ips[m]);
            assert(rdma_wowait_conns[i][m] != nullptr);
        }
        for (uint64_t j = 0; j < coro_num; j++)
        {
            std::vector<RACE::Client*> cli;
            for (uint64_t m = 0; m < memory_machine_num; m++) {
                lmrs[i * coro_num + j][m] =
                    dev.create_mr(cbuf_size, mem_buf + cbuf_size * ((i * coro_num + j) * memory_machine_num + m));
                RACE::Client *c;
                c = new RACE::Client(config, lmrs[i * coro_num + j][m], rdma_clis[i][m], rdma_conns[i][m],
                                        rdma_wowait_conns[i][m], config.machine_id, i, j);
                cli.push_back(c);
            }
            clis.push_back(cli);
        }
    }
    log_info << "Establish the connection of skip table." << std::endl;        
#endif
    
    // choose the test function
    fnType* test_func_list[] = {
    test_ycsb_run
    };
    fnType* test_func = test_func_list[test_func_num];

    // file loader
    YCSB::FileLoader file_loader_load, file_loader_run;
    if (
        test_func == test_ycsb_run
    ) {
        file_loader_load.load_from_file(workload_load_string);
        file_loader_run.load_from_file(workload_run_string, run_max_request);
        log_info << "workload_load_string: " << workload_load_string << ", record_len = " << file_loader_load.get_record_len() << std::endl;
        log_info << "workload_run_string: " << workload_run_string << ", record_len = " << file_loader_run.get_record_len() << std::endl;
    }

    // timer (inside the test_func and outside it)
    auto* tc_list = new counter::TimeCounter[thread_num_per_compute];
    counter::TimeCounter tc;
    tc.reset_time_counter();
    tc.set_thread_num(run_thread_num);

    // all mrs, qps and rcds
    RDMA::RDMAMemoryRegion mrs[thread_num_per_compute];
    RDMA::RDMAQueuePair qps[memory_machine_num * thread_num_per_compute];
    RDMA::rdma_connect_data rcds[memory_machine_num * thread_num_per_compute];

    #define mem_thre_di(mem, thread) ((mem) + (thread) * memory_machine_num)

    // all mrs, qps and rcds create
    for (int i = 0; i < thread_num_per_compute; ++i) {
        result = mrs[i].create(thread_size_byte, FLAGS_nic_index, FLAGS_ib_port);
        if (!result) {
            log_error << "server create mr error" << std::endl;
            return 0;
        }
    }
    for (int i = 0; i < memory_machine_num; ++i) {
    for (int j = 0; j < thread_num_per_compute; ++j) {
        result = qps[mem_thre_di(i, j)].create(mrs[j]);
        if (!result) {
            log_error << "server create qp error" << std::endl;
            return 0;
        }
        rcds[mem_thre_di(i, j)] = qps[mem_thre_di(i, j)].generate_connect_data();
    }}

    // send the connect data to monitor
    for (int mem_ind = 0; mem_ind < memory_machine_num; ++mem_ind) {
    for (int thread_ind = 0; thread_ind < thread_num_per_compute; ++thread_ind) {
        result = RDMA::tcp_send_rdma_connect_data(
            sock_conn, mem_ind, com_ind,
            thread_ind, rcds[mem_thre_di(mem_ind, thread_ind)]
        );
        if (!result) {
            log_error << "tcp_send_rdma_connect_data error" << std::endl;
            sock_conn.disconnect();
            return 0;
        }
    }}

    // get all memory connection metadata
    RDMA::RDMAConnectionMetadata mem_metadata(
        memory_machine_num, compute_machine_num, thread_num_per_compute
    );
    for (uint32_t _ = 0; _ < memory_machine_num * thread_num_per_compute; ++_) {
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
        if (com_ind_read != com_ind) {
            log_cyan << mem_ind_read << " " << com_ind_read << " " << thread_ind_read << std::endl;
            log_error << "compute index error: " << com_ind_read << " != " << com_ind << std::endl;
            sock_conn.disconnect();
            return 0;
        }
        mem_metadata(mem_ind_read, com_ind_read, thread_ind_read) = tmp_conn;
    }

    // create RDMA connections
    bool flag = true;
    RDMA::RDMAConnection* rdma_conn_list = new RDMA::RDMAConnection [memory_machine_num * thread_num_per_compute];

    for (uint64_t mem_ind = 0; mem_ind < memory_machine_num; ++mem_ind) {
    for (uint64_t thread_ind = 0; thread_ind < thread_num_per_compute; ++thread_ind) {
        result = rdma_conn_list[mem_thre_di(mem_ind, thread_ind)].connect(
            qps[mem_thre_di(mem_ind, thread_ind)], mem_metadata(mem_ind, com_ind, thread_ind)
        );
        if (!result) {
            flag = false;
            log_error << "thread " << thread_ind + 1 << " to memory " << mem_ind + 1 << " connection error" << std::endl;
        } else {
            log_info << "thread " << thread_ind + 1 << " to memory " << mem_ind + 1 << " connected" << std::endl;
        }
    }}

    // if RDMA connection failed, return 0
    // this must be done after the disconnection of socket, since socket did not go wrong
    if (!flag) {
        sock_conn.disconnect();
        delete[] rdma_conn_list;
        delete[] tc_list;
        return 0;
    }

    // ready and start
    log_purple << "Compute node No." << com_ind + 1 << " ready." << std::endl;
    result = sock_conn.sock_send_u32(200);
    if (!result) {
        log_error << "send 200 error" << std::endl;
        sock_conn.disconnect();
        delete[] rdma_conn_list;
        delete[] tc_list;
        return 0;
    }

    log_info << "Waiting for other compute nodes..." << std::endl;

    uint32_t go = 0;
    result = sock_conn.sock_read_u32(go);
    if (go != 200) {
        log_error << "cannot start, monitor sent go = " << go << std::endl;
        sock_conn.disconnect();
        delete[] rdma_conn_list;
        delete[] tc_list;
        return 0;
    }
    log_purple << "Compute node No." << com_ind + 1 << " start." << std::endl;

    // if test run
    if (test_func == test_ycsb_run) {
        // load first
        log_warn << "load start" << std::endl;
        std::vector<std::thread> threads;
        for (uint32_t thread_ind = 0; thread_ind < load_thread_num; ++thread_ind) {
            threads.emplace_back(
                test_ycsb_load,
                memory_machine_num,
                compute_machine_num,
                thread_num_per_compute,
                load_thread_num,
                1, // load to be 1
                com_ind,
                thread_ind,
                payload_byte, epoch_num, percent_num, bucket_num,
                &rdma_conn_list[mem_thre_di(0, thread_ind)],
                std::ref(tc_list[thread_ind]),
                std::ref(file_loader_load),
                std::ref(file_loader_run),
                std::vector<RACE::Client*>(),
                std::vector<RACE::rdma_client*>()
            );
        }
        for (auto& i : threads)
            i.join();
        for (uint32_t i = 0; i < thread_num_per_compute; ++i) {
            for (uint32_t j = 0; j < memory_machine_num; ++j)
                rdma_conn_list[mem_thre_di(j, i)].reset_usage();
            tc_list[i].reset_time_counter();
        }
        result = sock_conn.sock_send_u32(600);
        if (!result) {
            log_error << "send 600 error" << std::endl;
            sock_conn.disconnect();
            delete[] rdma_conn_list;
            delete[] tc_list;
            return 0;
        }
        uint32_t run = 0;
        result = sock_conn.sock_read_u32(run);
        if (run != 700) {
            log_error << "cannot start prepare, monitor sent run = " << run << std::endl;
            sock_conn.disconnect();
            delete[] rdma_conn_list;
            delete[] tc_list;
            return 0;
        }
        log_warn << "load done, start to prepare" << std::endl;

#ifdef ENABLE_ACCESS_TRACKING
        // Print ART tree height and per-level node distribution.
        prheart::AccessTracker::instance().print_tree_stats();
#endif

        #ifdef DYNAMIC
        dfs(
            memory_machine_num,
            compute_machine_num,
            thread_num_per_compute,
            load_thread_num,
            1,
            com_ind,
            0,
            bucket_num,
            &rdma_conn_list[mem_thre_di(0, 0)],
            nullptr,
            nullptr
        );
        #endif

        #ifdef SKIP_TABLE
        if (com_ind == 0) {
            create_skip_table(
                memory_machine_num,
                compute_machine_num,
                thread_num_per_compute,
                load_thread_num,
                1,
                com_ind,
                0,
                bucket_num,
                &rdma_conn_list[mem_thre_di(0, 0)],
                (std::vector<RACE::Client*>)clis[0],
                rdma_clis[0]
            );      
            log_info << "skip list: load done!" << std::endl;
        }
        #endif

        // --- Phase 2/3: reset tracker so run phase has clean data ----------
        // The load phase and skip-table DFS reads have populated the tracker.
        // We printed the load-phase report inside create_skip_table() above.
        // Now reset so the run phase captures only runtime access patterns,
        // which are what we want to analyse for policy evaluation.
#ifdef ENABLE_ACCESS_TRACKING
        prheart::AccessTracker::instance().reset();
#endif
        // -------------------------------------------------------------------

        result = sock_conn.sock_send_u32(800);
        if (!result) {
            log_error << "send 600 error" << std::endl;
            sock_conn.disconnect();
            delete[] rdma_conn_list;
            delete[] tc_list;
            return 0;
        }
        run = 0;
        result = sock_conn.sock_read_u32(run);
        if (run != 900) {
            log_error << "cannot start run, monitor sent run = " << run << std::endl;
            sock_conn.disconnect();
            delete[] rdma_conn_list;
            delete[] tc_list;
            return 0;
        }
        log_warn << "prepare done, start to run" << std::endl;
    }

    // create threads
    std::vector<std::thread> threads;
    for (uint32_t thread_ind = 0; thread_ind < run_thread_num; ++thread_ind) {
        threads.emplace_back(
            test_ycsb_run,
            memory_machine_num,
            compute_machine_num,
            thread_num_per_compute,
            run_thread_num,
            coro_num,
            com_ind,
            thread_ind,
            payload_byte, epoch_num, percent_num, bucket_num,
            &rdma_conn_list[mem_thre_di(0, thread_ind)],
            std::ref(tc_list[thread_ind]),
            std::ref(file_loader_load),
            std::ref(file_loader_run),
        #ifdef SKIP_TABLE
            // clis[0],
            // rdma_clis[0]
            clis[thread_ind],
            rdma_clis[thread_ind]
        #else
            std::vector<RACE::Client*>(),
            std::vector<RACE::rdma_client*>()
        #endif
        );
    }

    // join threads
    for (auto& i : threads)
        i.join();

    // --- Phase 2/3: run-phase access analysis ----------------------------
    // The run workload has just completed.  Print what the tracker captured
    // during execution so we can compare policy predictions (built from the
    // load phase) against actual runtime access patterns.
    //
    // Key questions answered here:
    //   • Did the nodes chosen by HOTNESS vs CRITICALITY during load actually
    //     turn out to be the hot / critical nodes at runtime?
    //   • Does the overlap between policies change between load and run
    //     (workload distribution shift)?
#ifdef ENABLE_ACCESS_TRACKING
    log_purple << "======================================================" << std::endl;
    log_purple << "[Phase 2] Run-phase AccessTracker report:" << std::endl;
    log_purple << "======================================================" << std::endl;
    prheart::AccessTracker::instance().print_summary(20, prheart::CachePolicy::HOTNESS);
    prheart::AccessTracker::instance().print_summary(20, prheart::CachePolicy::CRITICALITY);
    prheart::AccessTracker::instance().print_policy_comparison(100);
    log_purple << "======================================================" << std::endl;
#endif
    // -------------------------------------------------------------------

    // collect and show results
    if (test_func == test_ycsb_run) {
        uint64_t all_count = 0;
        for (uint32_t i = 0; i < run_thread_num; ++i) {
            tc.add_event_count(tc_list[i].get_event_count());
            tc.add_all_time_cost(tc_list[i].get_all_time());
            log_info << "thread " << i + 1 << ": " << tc_list[i].result_str() << std::endl;
        }
        log_info << tc.time_event_str() << std::endl;
        log_info << tc.throughput_latency_str() << std::endl;
    }


    if (test_func == test_ycsb_run) {
        log_info << "ALL: throughput = " << tc.get_throughput_MOps() << " MOps" << std::endl;
        log_info << "ALL: latency = " << tc.get_latency_us() << " us" << std::endl;
        sock_conn.sock_send_double(tc.get_throughput_MOps());
        sock_conn.sock_send_double(tc.get_latency_us());
        sock_conn.sock_send_double(0);
    }
    else {
        log_info << "??? function done" << std::endl;
        sock_conn.sock_send_double(0);
        sock_conn.sock_send_double(0);
        sock_conn.sock_send_double(0);
    }

    // wait for the end
    log_warn << "waiting the end..." << std::endl;

    // disconnect
    uint32_t end = 0;
    sock_conn.sock_read_u32(end);
    if (end != 999) {
        log_error << "end error, monitor sent end = " << end << std::endl;
    }
    sock_conn.disconnect();

    // free the RDMA connections
    delete[] rdma_conn_list;
    delete[] tc_list;

#ifdef SKIP_TABLE
    free(mem_buf);
    for (uint64_t i = 0; i < thread_num_per_compute; i++) {
        for (uint64_t j = 0; j < coro_num; j++) {
            for (uint64_t m = 0; m < memory_machine_num; m++) {
                RACE::rdma_free_mr(lmrs[i * coro_num + j][m], false);
                delete clis[i * coro_num + j][m];
            }
        }
        for (uint64_t m = 0; m < memory_machine_num; m++) {
            delete rdma_wowait_conns[i][m];
            delete rdma_conns[i][m];
            delete rdma_clis[i][m];            
        }
    }
#endif

    return 0;
}

