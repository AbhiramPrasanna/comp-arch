#pragma once

#include <unordered_set>   // for add_shortcut_policy parameter type

#include "boost/coroutine2/all.hpp"

#include "log/log.hpp"
#include "measure/measure.hpp"
#include "rdma/rdma.hpp"

#include "prheart/art-head.hpp"
#include "prheart/art-data.hpp"
#include "race/race.h"


template<typename T>
using coro = boost::coroutines2::coroutine<T>;

namespace prheart {

enum search_local_result {
    found,
    notfound_with_none, // not found, but there's none node in the slot
    notfound_with_full, // not found, and the node is full
};

enum uncompact_till_leaf_local_result {
    found_use_seems_same_key,
    found_use_different_key,
    notfound_try_again,
};

enum uncompact_till_leaf_result {
    same_key,
    not_same_key,
    error,
};

enum get_diff_node_result {
    head_diff,
    inner_diff,
    diff_error,
};

// optimistic means scaning all the subtree since compressed node met.
// lower_bound means the inter node is at the lower bound of the scale
// (i.e. compare the now_pos char with start key)
// upper_bound means the opposition of lower_bound.
// unknown refers to the start state.
// certain means scanning without varification.
enum scan_choice {
    optimistic,
    lower_bound,
    upper_bound,
    unknown,
    certain
};

struct PrheartTree;

struct PrheartNode {

    PrheartNode(
        PrheartTree* tree, PrheartNodeType type, uintptr_t fptr, uint32_t now_pos
    ):
        tree(tree), type(type), fptr(fptr), now_pos(now_pos) {}
    ~PrheartNode() = default;

    [[nodiscard]] uint32_t get_size();

    void rdma_reset();
    bool rdma_read_real_data(uint32_t size, uintptr_t fptr);
    bool rdma_read_real_data();
    bool rdma_write_real_data(uint32_t size, uintptr_t fptr);
    bool rdma_write_real_data();
    bool rdma_cas_8_byte(
        uintptr_t nfptr, uintptr_t local_save_old_mr_addr, uint64_t swap_data, uint64_t compare_data
    );
    bool rdma_read_bucket_data(uint8_t key_byte, uint32_t& bucket_index);
    bool rdma_read_bucket_data(uint32_t bucket_index);
    bool rdma_write_bucket_data(uint8_t key_byte);
    bool rdma_write_bucket_data(uint32_t bucket_index);

    // meta
    PrheartTree* tree;
    // assign before use
    uintptr_t fptr;
    PrheartNodeType type;
    uint32_t now_pos;

    // assign after dmc->read
    uint32_t local_len = 0;

    // Must use after `rdma_read_...()`.
    search_local_result search_local(
        span key, PrheartSlotData& searched_slot, uint32_t& slot_num, uint32_t& none_index
    );

    bool scan_local(span start_key, span end_key, vec<str> &buffer, scan_choice choice);

    // Must use after `rdma_read_...()`.
    uncompact_till_leaf_local_result uncompact_till_leaf_local(span try_key, PrheartSlotData& searched_slot, uint32_t& slot_num);

    // If use `uncompact_till_leaf()`,
    // it must guarantee to have a leaf node (to get this compacted node's full key),
    // unless the leaf is doing with a concurrent thread, which will return ::error.
    uncompact_till_leaf_result uncompact_till_leaf(
        span try_key,
        vec<PrheartSlotData>& slot_status, vec<uint32_t>& total_pos_status,
        vec<uintptr_t>& parent_slot_fptr_status,
        vec<u8>& real_key
    );

    // Must use after `uncompact_till_leaf()`.
    get_diff_node_result get_diff_node(
        span try_key,
        vec<PrheartSlotData>& slot_status, vec<uint32_t>& total_pos_status,
        vec<uintptr_t>& parent_slot_fptr_status,
        span real_key,
        uint32_t& get_index, uint32_t& get_same_len, uint32_t& not_same_pos
    );

    void print_tree(uint32_t now_level);

    uint64_t dfs(uint64_t level, uint64_t prev_pos);
    std::pair<uint64_t, uint64_t> cal_cost(uint64_t level, uint64_t prev_pos);
    span get_prefix(uint64_t len);
    span get_one_leaf();
    uint64_t add_shortcut(uint64_t level);

    // Policy-aware skip table builder (Phase 3).
    // Walks the tree via DFS and inserts a RACE entry for every inner node
    // whose fptr appears in `target_fptrs` (the policy-ranked top-K set).
    // Unlike add_shortcut(), there is no depth threshold: any tracked node
    // at any depth is eligible.  Always recurses into all children.
    uint64_t add_shortcut_policy(const std::unordered_set<uintptr_t>& target_fptrs);

    bool is_shortcut(uint64_t prev_pos);


    bool search(span key);
    bool insert(span key, span value, uintptr_t parent_slot_fptr);

    bool update(span key, span value, uintptr_t parent_slot_fptr);
    bool scan(span start_key, span end_key, vec<std::string> &buffer, scan_choice choice);

    bool remove(span key);

};

struct PrheartTree {

    PrheartTree(
        DM::DisaggregatedMemoryController* dmc,
        uintptr_t root_fptr,
        uintptr_t alloc_start_fptr, uintptr_t alloc_end_fptr,
        uintptr_t local_start_ptr, uintptr_t local_end_ptr,
        uint32_t hash_bucket_size, coro<void>::push_type* yield_ptr = nullptr, 
        std::vector<RACE::Client*> race_cli = std::vector<RACE::Client*>(), 
        std::vector<RACE::rdma_client*> rdma_cli = std::vector<RACE::rdma_client*>(),
        uint64_t memory_machine_num = 1
    ):
        dmc(dmc),
        root_fptr(root_fptr),
        alloc_start_fptr(alloc_start_fptr), alloc_end_fptr(alloc_end_fptr), alloc_now_fptr(alloc_start_fptr),
        local_start_ptr(local_start_ptr), local_end_ptr(local_end_ptr),
        hash_bucket_size(hash_bucket_size), yield_ptr(yield_ptr), 
        race_cli(race_cli), rdma_cli(rdma_cli),
        memory_machine_num(memory_machine_num)
    {
        if (alloc_start_fptr == root_fptr) {
            alloc_start_fptr = alloc_now_fptr = root_fptr + type_to_size(PrheartNodeType::Node256);
        }
        // do not reset here...
        // memset((void*)local_start_ptr, 0, type_to_size(PrheartNodeType::Node256));
        // dmc->write_from_local_addr_to_remote_fptr(
        //     root_fptr, local_start_ptr, type_to_size(PrheartNodeType::Node256), true
        // );
    };

    ~PrheartTree() = default;

    PrheartNode get_root();
    PrheartNode get_root(span key);

    bool malloc(uint32_t size, uintptr_t& fptr);


    bool search(span key);
    bool insert(span key, span value);
    bool update(span key, span value);
    bool scan(span start_key, span end_key, vec<str>& result_vec);
    bool remove(span key);
    void print_tree();
    uint64_t dfs(uint64_t level=0, uint64_t prev_pos=-1);
    uint64_t create_skip_table();
    void cal_cost(bool is_email, uint64_t level=0, uint64_t prev_pos=-1);

    // Policy-aware skip table construction (Phase 3).
    // Snapshots the top `max_entries` nodes from AccessTracker ranked by
    // `policy`, then walks the ART and inserts a RACE skip entry for each
    // of those nodes.  Returns the number of entries actually inserted.
    //
    // Call this instead of create_skip_table() when using POLICY_HOTNESS,
    // POLICY_CRITICALITY, or POLICY_HYBRID (selected in compute.cc).
    // Requires: ENABLE_ACCESS_TRACKING defined in art-node.cc AND the load
    // phase must have completed so the tracker holds meaningful data.
    uint64_t create_skip_table_policy(CachePolicy policy, uint64_t max_entries = 5000);


    DM::DisaggregatedMemoryController* dmc;

    uintptr_t root_fptr;
    uintptr_t alloc_start_fptr;
    uintptr_t alloc_end_fptr;
    uintptr_t alloc_now_fptr;
    uintptr_t local_start_ptr;
    uintptr_t local_end_ptr;
    uint32_t hash_bucket_size;
    coro<void>::push_type* yield_ptr;

    std::vector<RACE::Client*> race_cli;
    std::vector<RACE::rdma_client*> rdma_cli;
    uint64_t memory_machine_num;
};

}