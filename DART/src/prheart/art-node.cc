#include "log/log.hpp"
#include "measure/measure.hpp"
#include "rdma/rdma.hpp"

#include "prheart/art-node.hpp"
#include "race/hash.h"


// some factors here

// #define NO_CAS_UPDATE_AND_USE_UPDATE_LOCK
// #define NO_LOCK
// #define USE_INSERT_LEAF_LOCK
// #define USE_FULL_NODE_READ
// #define DYNAMIC
#define USE_PREFIX_DATA_BEFORE

// ---- Phase 2/3: per-node access tracking --------------------------------
// Define ENABLE_ACCESS_TRACKING to record every RDMA read/write to inner
// ART nodes.  This populates AccessTracker with hotness and criticality
// data that can be queried after the benchmark.
//
// Disable for baseline performance runs (avoids shard-mutex overhead).
#define ENABLE_ACCESS_TRACKING
#ifdef ENABLE_ACCESS_TRACKING
#include "prheart/access-tracker.hpp"
#endif
// -------------------------------------------------------------------------


// some debug here
// #define USE_PRINT
// #define ENTER_TO_CONTINUE
// #define ENTER_TO_CONTINUE_PRINT(key_) ((key::to_string_only_no_0(key_) == "co.england@gayathri.karthik"))
// #define ONLY_PRINT_ENTER_TO_CONTINUE_MIN

// #define FALSE_OUPUT
// #define FALSE_STOP


// --- do not use these! just for unstable debug ---
// #define MAX_CAS_RETRY_TIME 10
// #define MAX_MEM_NUM_FOR_WILD_PTR 3
// #define WILD_PTR_STRIP
// #define WILD_PTR_OUTPUT
// #define WILD_PTR_OUTPUT_STOP
// --- do not use these! just for unstable debug ---


#if defined(ENTER_TO_CONTINUE) && defined(ENTER_TO_CONTINUE_PRINT) && defined(ONLY_PRINT_ENTER_TO_CONTINUE_MIN)
#ifdef USE_PRINT
#define PRINT_A(x) {if (ENTER_TO_CONTINUE_PRINT(key)) {x};}
#define PRINT_B(x) {if (ENTER_TO_CONTINUE_PRINT(try_key)) {x};}
#else
#define PRINT_A(x) {};
#define PRINT_B(x) {};
#endif
#else
#ifdef USE_PRINT
#define PRINT_A(x) {x};
#define PRINT_B(x) {x};
#else
#define PRINT_A(x) {};
#define PRINT_B(x) {};
#endif
#endif


__thread uint32_t errorno = 0;

constexpr uint32_t ERROR_NOTUSE = 100;
constexpr uint32_t ERROR_NORMAL = 200;
constexpr uint32_t ERROR_MALLOC = 300;

constexpr uint32_t ABOUT_CAS_ERROR = 1000;

constexpr uint32_t ERROR_CAS = ABOUT_CAS_ERROR + 100;
constexpr uint32_t ERROR_LOCK = ABOUT_CAS_ERROR + 200;
constexpr uint32_t ERROR_IS_LOCKED = ABOUT_CAS_ERROR + 300;
constexpr uint32_t ERROR_JUST_LIKE_CAS = ABOUT_CAS_ERROR + 400;

constexpr bool IS_CAS_ERROR(uint32_t errorno) { return errorno >= ABOUT_CAS_ERROR; }



__thread uint64_t rtt = 0;
__thread uint64_t access_size = 0;
__thread uint64_t wrong = 0;
__thread uint64_t fail = 0;
__thread uint64_t duplicate = 0;

#define MAX_LEVEL 32
#define INTERVAL_LEVEL 4
uint64_t node_num[MAX_LEVEL];
uint64_t leaf_num[MAX_LEVEL];
uint64_t node_prefix_num[MAX_LEVEL];
uint64_t leaf_prefix_num[MAX_LEVEL];
uint64_t leaf_len[MAX_LEVEL];
uint64_t shortcut_num[MAX_LEVEL];
int costs[MAX_LEVEL];
uint64_t compress[MAX_LEVEL];
uint64_t node_type_num[8];
__thread uint64_t start_level[MAX_LEVEL];
__thread uint64_t fail_level[MAX_LEVEL];
__thread uint64_t retry_level[MAX_LEVEL];
std::vector<uint64_t> prefix_list;


namespace prheart {
#ifdef DYNAMIC
PrheartNode PrheartTree::get_root(span key) {

    uint32_t len = key.size();
    if (len<2)
        return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
    else
    {
        uint64_t idx=MAX_LEVEL+1;
        for (int i=0; i<prefix_list.size();++i) {
            if (len > prefix_list[i]) {
                idx = i;
                len = prefix_list[idx];
                break;
            }
        }
        if (idx == MAX_LEVEL+1)
            return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
        start_level[len]++;
        uint64_t fall_back = 0;
        while (fall_back <= 1)
        {
            // start_level[len]++;
            // if (len!=4)
            //     log_info << "wrong!" << std::endl;
            span prefix = key::get_prefix(key, len);
            // log_info << "key: " << key::to_string(key) << " len: " << prefix.size() << " prefix: " << key::to_string(prefix) << std::endl;
            RACE::Slice key_, value;
            RACE::Node_Meta node;
            char buffer[1024];

            key_.len = prefix.size();
            key_.data = (char*)prefix.data();
            value.data = buffer;
            value.len = 0;
            uint64_t pattern = RACE::hash(key_.data, key_.len) >> 64;
            uint64_t mem_idx = pattern % memory_machine_num;
            rtt++;
            access_size+=128;
            rdma_cli[mem_idx]->run(race_cli[mem_idx]->search(&key_, &value));
            std::memcpy(&node, value.data, sizeof(uint64_t));

            if (value.len > sizeof(uint64_t))
            {
                // log_info << "value.len:" << value.len << std::endl;
                duplicate++;
                return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
            }
            // if (!value.data)
            //     log_info << "can not find the prefix!" << std::endl;
            // else
            //     log_info << "value len:" << value.len << " value data:" << uint64_t(value.data) << std::endl;
            // log_info << "pos: " << node.pos << " type: " << node.type << "fptr:" <<hex_str(node.fptr) << std::endl;
            else if (value.len == 8)
                return PrheartNode(this, static_cast<PrheartNodeType>(node.type), node.fptr, node.pos);
            else if (value.len == 0)
            {
                // log_info << "can not find the prefix!" << std::endl;
                // len = len/2;
                // prefix = span_get_prefix(key, len);
                // key_.data = (char*)&prefix;
                // rdma_cli->run(race_cli->search(&key_, &value));
                // std::memcpy(&node, value.data, sizeof(uint64_t));
                // if (value.len == 0)
                // {
                //     fail++;
                //     if (fail == 1)
                //         log_info << "key: " << key::to_string(key) << " prefix: " << key::to_string(span_get_prefix(key, 4)) << std::endl;

                // }
                fall_back++;
                if (fall_back<2)
                    retry_level[len]++;
                len = prefix_list[idx+1];
                if (idx < (prefix_list.size()-1))
                {
                    idx++;
                    len = prefix_list[idx];
                }
                else
                    break;
            }
            else {
                // what is it?
                return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
            }
        }
    }
    fail_level[len]++;
    fail++;
    return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
}
#else
PrheartNode PrheartTree::get_root(span key) {

    uint32_t len = key.size();
    len--;
    if (len==0)
        return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
    else
    {
        len |= len >> 1;
        len |= len >> 2;
        len |= len >> 4;
        len |= len >> 8;
        len |= len >> 16;
        len = len - (len >> 1);
        // if (len!=4)
        //     log_info << "wrong!" << std::endl;
        span prefix = key::get_prefix(key, len-1);
        // log_info << "key: " << key::to_string(key) << " len: " << prefix.size() << " prefix: " << key::to_string(prefix) << std::endl;
        RACE::Slice key_, value;
        RACE::Node_Meta node;
        char buffer[1024];

        key_.len = prefix.size();
        key_.data = (char*)prefix.data();
        value.data = buffer;
        value.len = 0;
        uint64_t pattern = RACE::hash(key_.data, key_.len) >> 64;
        uint64_t mem_idx = pattern % memory_machine_num;
        rdma_cli[mem_idx]->run(race_cli[mem_idx]->search(&key_, &value));
        std::memcpy(&node, value.data, sizeof(uint64_t));
        if (value.len > sizeof(uint64_t))
        {
            // log_info << "value.len:" << value.len << std::endl;
            duplicate++;
            return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
        }
        // if (!value.data)
        //     log_info << "can not find the prefix!" << std::endl;
        // else
        //     log_info << "value len:" << value.len << " value data:" << uint64_t(value.data) << std::endl;
        // log_info << "pos: " << node.pos << " type: " << node.type << "fptr:" <<hex_str(node.fptr) << std::endl;
        else if (value.len == 8)
            return PrheartNode(this, static_cast<PrheartNodeType>(node.type), node.fptr, node.pos);
        else if (value.len == 0)
        {
            // log_info << "can not find the prefix!" << std::endl;
            // len = len/2;
            // prefix = span_get_prefix(key, len);
            // key_.data = (char*)&prefix;
            // rdma_cli->run(race_cli->search(&key_, &value));
            // std::memcpy(&node, value.data, sizeof(uint64_t));
            // if (value.len == 0)
            // {
            //     fail++;
            //     if (fail == 1)
            //         log_info << "key: " << key::to_string(key) << " prefix: " << key::to_string(span_get_prefix(key, 4)) << std::endl;

            // }
            fail++;

            return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
        }
        else {
            // what is it?
            return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
        }
    }
}
#endif





PrheartNode PrheartTree::get_root() {
    return PrheartNode(this, PrheartNodeType::Node256, root_fptr, 0);
}

bool PrheartTree::malloc(uint32_t size, uintptr_t& fptr) {
    if (alloc_now_fptr + size <= alloc_end_fptr) {
        fptr = alloc_now_fptr;
        alloc_now_fptr += size;
        return true;
    }
    log_error << "space not enough! size=" << size
        << ", malloc_now_ptr=" << hex_str(alloc_now_fptr)
        << ", end_ptr=" << hex_str(alloc_end_fptr) << std::endl;
    return false;
}

bool PrheartTree::search(span key) {

    if (race_cli.empty()){
        auto res = get_root().search(key);

        #ifdef FALSE_OUPUT
        if (!res) {
            log_error << "search " << key::to_string(key) << " error" << std::endl;
            log_error << "errorno: " << errorno << std::endl;
            #ifdef FALSE_STOP
            str a;
            std::getline(std::cin, a);
            #endif
        }
        #endif

        #ifdef ENTER_TO_CONTINUE
        str a;
        #ifdef ENTER_TO_CONTINUE_PRINT
        if (!ENTER_TO_CONTINUE_PRINT(key)) goto end;
        #endif
        std::getline(std::cin, a);
        #endif
        end:
        return res;
    }
    else{
        auto res = get_root(key).search(key);
        return res;
    }
}

bool PrheartTree::insert(span key, span value) {
    if (race_cli.empty()) {
        auto res = get_root().insert(key, value, 0);

        // CAS error (usually)
        int now_cas_error_retry_time = 0;
        while (!res && IS_CAS_ERROR(errorno)) {
            now_cas_error_retry_time++;
            #ifdef MAX_CAS_RETRY_TIME
            if (now_cas_error_retry_time > MAX_CAS_RETRY_TIME) {
                break;
            }
            #endif
            res = get_root().insert(key, value, 0);
        }

        #ifdef FALSE_OUPUT
        if (!res) {
            log_error << "insert " << key::to_string(key) << " error" << std::endl;
            log_error << "errorno: " << errorno << std::endl;
            #ifdef FALSE_STOP
            str a;
            std::getline(std::cin, a);
            #endif
        }
        #endif

        #ifdef ENTER_TO_CONTINUE
        str a;
        #ifdef ENTER_TO_CONTINUE_PRINT
        if (!ENTER_TO_CONTINUE_PRINT(key)) goto end;
        #endif
        std::getline(std::cin, a);
        #endif
        end:
        return res;
    }
    else {
        auto res = get_root(key).insert(key, value, 0);

        return res;
    }
}

bool PrheartTree::update(span key, span value) {
    if (race_cli.empty()){
        auto res = get_root().update(key, value, 0);
        #ifdef FALSE_OUPUT
        if (!res) {
            log_error << "update " << key::to_string(key) << " error" << std::endl;
            log_error << "errorno: " << errorno << std::endl;
            #ifdef FALSE_STOP
            str a;
            std::getline(std::cin, a);
            #endif
        }
        #endif

        #ifdef ENTER_TO_CONTINUE
        str a;
        #ifdef ENTER_TO_CONTINUE_PRINT
        if (!ENTER_TO_CONTINUE_PRINT(key)) goto end;
        #endif
        std::getline(std::cin, a);
        #endif
        end:
        return res;
    }
    else {
        auto res = get_root(key).update(key, value, 0);
        return res;
    }
}

bool PrheartTree::scan(span start_key, span end_key, vec<str>& result_vec) {

    span try_key = start_key; // for PRINT_B define
    PRINT_B(
    log_debug << "search span (end - start) = " << key::span_to_u64_reverse(end_key) - key::span_to_u64_reverse(start_key) << std::endl;
    log_debug << "result vec size = " << result_vec.size() << std::endl;
    )

    auto res = get_root().scan(start_key, end_key, result_vec, unknown);
    #ifdef FALSE_OUPUT
    if (!res) {
        log_error << "scan " << key::to_string(start_key) << " error" << std::endl;
        log_error << "errorno: " << errorno << std::endl;
        #ifdef FALSE_STOP
        str a;
        std::getline(std::cin, a);
        #endif
    }
    #endif

    #ifdef ENTER_TO_CONTINUE
    str a;
    #ifdef ENTER_TO_CONTINUE_PRINT
    if (!ENTER_TO_CONTINUE_PRINT(start_key)) goto end;
    #endif
    std::getline(std::cin, a);
    #endif
    end:
    return res;
}

bool PrheartTree::remove(span key) {

    auto res = get_root().remove(key);
    #ifdef FALSE_OUPUT
    if (!res) {
        log_error << "remove " << key::to_string(key) << " error" << std::endl;
        log_error << "errorno: " << errorno << std::endl;
        #ifdef FALSE_STOP
        str a;
        std::getline(std::cin, a);
        #endif
    }
    #endif

    #ifdef ENTER_TO_CONTINUE
    str a;
    #ifdef ENTER_TO_CONTINUE_PRINT
    if (!ENTER_TO_CONTINUE_PRINT(key)) goto end;
    #endif
    std::getline(std::cin, a);
    #endif
    end:
    return res;
}

void PrheartTree::print_tree() {
    return get_root().print_tree(0);
}

uint64_t PrheartTree::dfs(uint64_t level, uint64_t prev_pos) {
    return get_root().dfs(level, prev_pos);
}

void PrheartTree::cal_cost(bool is_email, uint64_t level, uint64_t prev_pos) {
    // level = 0, prev_pos = -1
    uint64_t total_cost = 0;
    int max_cost;
    uint64_t cur_prefix;
    std::pair<uint64_t, uint64_t> res;
    uint64_t min_level = MAX_LEVEL/2;

    // res = get_root().cal_cost(level, prev_pos);
    // for (uint64_t i=0; i<MAX_LEVEL; ++i) {
    //     log_info << "[prefix_len = " << i << "] cost: " << costs[i] << " compress: " << compress[i] << std::endl;
    // }
#ifdef USE_PREFIX_DATA_BEFORE
    if (is_email) {
        log_info << "email" << std::endl;
        prefix_list = std::vector<uint64_t>({15, 11, 8, 6});
    } else {
        log_info << "int" << std::endl;
        prefix_list = std::vector<uint64_t>({3});
    }
#else
    do
    {
        for (uint64_t i=min_level-1; i<MAX_LEVEL; ++i)
        {
            node_num[i] = leaf_num[i] = node_prefix_num[i] = leaf_prefix_num[i] = costs[i] = leaf_len[i] = 0;
        }
        res = get_root().cal_cost(level, prev_pos);
        max_cost = 0;
        cur_prefix = MAX_LEVEL;
        for (uint64_t i=min_level-1; i<MAX_LEVEL; ++i) {
            log_info << "[prefix_len " << i << "] inner node: " << node_prefix_num[i] << "; leaf node: " << leaf_prefix_num[i] << "; cost:" << costs[i] << std::endl;
            if (costs[i] > max_cost)
            {
                max_cost = costs[i];
                cur_prefix = i;
            }
        }

        if (cur_prefix < MAX_LEVEL) {
            log_info << "[prefix_len = " << cur_prefix << "] cost: " << max_cost << " num: " << node_prefix_num[cur_prefix]  << std::endl;
            total_cost += max_cost;
            if (cur_prefix > 1 && max_cost > 0)
                prefix_list.push_back(cur_prefix);
        }
        else {
            log_info << "not found" << std::endl;
        }
        // for (uint64_t i=0; i<MAX_LEVEL; ++i) {
        //     log_info << "Level " << i << " Compress number: " << compress[i] << std::endl;
        // }
        if (min_level > 1)
            min_level /= 2;

    } while (cur_prefix > 1 && min_level > 1);
#endif

    for (uint64_t i = 0; i < prefix_list.size(); ++i)
    {
        log_info << "prefix_list[" << i << "]: " << prefix_list[i] << std::endl;
    }
}

#ifdef DYNAMIC
uint64_t PrheartTree::create_skip_table() {
    return get_root().add_shortcut(prefix_list.size()-1);
}
#else
uint64_t PrheartTree::create_skip_table() {
    return get_root().add_shortcut(2);
}
#endif

uint32_t PrheartNode::get_size() {
    return type_to_size(type);
}

bool PrheartNode::rdma_read_real_data(uint32_t size, uintptr_t nfptr) {
    rtt++;
    access_size+=size;
    bool r = tree->dmc->read_from_remote_fptr_to_local_addr(tree->local_start_ptr, nfptr, size, false);
    // (*tree->yield_ptr)();
    local_len = size / 8;
    tree->dmc->poll_cq_from_fptr(nfptr);
    // (*tree->yield_ptr)();

    // --- Phase 2: record access for hotness / criticality analysis -------
    // We track only inner nodes (Leaf and None are not useful skip targets).
    // We record this->fptr (the node's base address), not nfptr (which may be
    // a bucket sub-offset).  now_pos = byte depth in the key = traversal depth.
#ifdef ENABLE_ACCESS_TRACKING
    if (type != PrheartNodeType::Leaf && type != PrheartNodeType::None) {
        AccessTracker::instance().record_read(
            this->fptr,
            this->now_pos,
            static_cast<uint8_t>(this->type)
        );
    }
#endif
    // ---------------------------------------------------------------------

    return r;
}

bool PrheartNode::rdma_read_real_data() {
    return rdma_read_real_data(get_size(), fptr);
}

bool PrheartNode::rdma_write_real_data(uint32_t size, uintptr_t nfptr) {
    rtt++;
    access_size+=size;
    auto r = tree->dmc->write_from_local_addr_to_remote_fptr(nfptr, tree->local_start_ptr, size, false);
    // (*tree->yield_ptr)();
    tree->dmc->poll_cq_from_fptr(nfptr);
    // (*tree->yield_ptr)();

    // --- Phase 2: record write for write-pressure analysis ---------------
#ifdef ENABLE_ACCESS_TRACKING
    if (type != PrheartNodeType::Leaf && type != PrheartNodeType::None) {
        AccessTracker::instance().record_write(
            this->fptr,
            this->now_pos,
            static_cast<uint8_t>(this->type)
        );
    }
#endif
    // ---------------------------------------------------------------------
    return r;
}

bool PrheartNode::rdma_write_real_data() {
    return rdma_write_real_data(get_size(), fptr);
}

bool PrheartNode::rdma_cas_8_byte(
    uintptr_t nfptr, uintptr_t local_save_old_mr_addr, uint64_t swap_data, uint64_t compare_data
) {
    rtt++;
    access_size+=8;
    auto r = tree->dmc->cas_8_byte_to_remote_fptr(
        nfptr,
        local_save_old_mr_addr,
        swap_data,
        compare_data
    );
    return r;
}

bool PrheartNode::rdma_read_bucket_data(uint8_t key_byte, uint32_t& bucket_index) {
    if (tree->hash_bucket_size > get_size()) {
        bucket_index = 0;
        return rdma_read_real_data();
    }
    auto buckets = get_size() / tree->hash_bucket_size;  // WARN: must be integer, no check here
    uint32_t bucket_ind = key_byte % buckets;  // hash: mod
    bucket_index = bucket_ind;
    return rdma_read_bucket_data(bucket_ind);
}

bool PrheartNode::rdma_read_bucket_data(uint32_t bucket_index) {
    if (tree->hash_bucket_size > get_size()) {
        return rdma_read_real_data();
    }
    return rdma_read_real_data(tree->hash_bucket_size, fptr + bucket_index * tree->hash_bucket_size);
}

bool PrheartNode::rdma_write_bucket_data(uint8_t key_byte) {
    if (tree->hash_bucket_size > get_size()) {
        return rdma_read_real_data();
    }
    auto buckets = get_size() / tree->hash_bucket_size;  // WARN: must be integer, no check here
    uint32_t bucket_index = key_byte % buckets;  // hash: mod
    return rdma_write_bucket_data(bucket_index);
}

bool PrheartNode::rdma_write_bucket_data(uint32_t bucket_index) {
    if (tree->hash_bucket_size > get_size()) {
        return rdma_read_real_data();
    }
    return rdma_write_real_data(tree->hash_bucket_size, fptr + bucket_index * tree->hash_bucket_size);
}

search_local_result PrheartNode::search_local(
    span key, PrheartSlotData& searched_slot, uint32_t& slot_num, uint32_t& none_index
) {
    PRINT_A(
    log_cyan << "key = " << key::to_string(key) << std::endl;
    )

    uint8_t now_byte = key::get_byte(key, now_pos);
    PrheartSlotData* node = (PrheartSlotData*)tree->local_start_ptr;
    bool has_none = false;
    for (uint32_t i = 0; i < local_len; ++i) {
        if (node[i].node_type() == PrheartNodeType::None) {
            if (!has_none) none_index = i;
            has_none = true;
            continue;
        }
        #ifdef WILD_PTR_STRIP
        if (((node[i].fptr() & 0x0000'f000'0000'0000ull) >> 44 >= MAX_MEM_NUM_FOR_WILD_PTR) || node[i].version() > 1) {
            #ifdef WILD_PTR_OUTPUT_STOP
            log_error << "press to show" << std::endl;
            str a;
            std::getline(std::cin, a);
            #endif
            #ifdef WILD_PTR_OUTPUT
            log_purple << "this parent slot: " << hex_str(fptr) << std::endl;
            log_error << "fptr=" << hex_str(node[i].fptr()) << std::endl;
            log_cyan << "key=" << key::to_string(key) << std::endl;
            log_cyan << "now_pos=" << now_pos << std::endl;
            log_cyan << i << "/" << local_len << " " << node[i].get_readable_str() << std::endl;
            #endif
            continue;
        }
        #endif
        PRINT_A(
        log_cyan << i << "/" << local_len << " " << node[i].get_readable_str() << std::endl;
        )
        if (node[i].key_byte() == now_byte) {
            slot_num = i;
            searched_slot = node[i];
            PRINT_A(
            log_cyan << "Searched: now_byte=" << (uint32_t)now_byte << ", " << searched_slot.get_readable_str() << std::endl;
            )
            return search_local_result::found;
        }
    }

    PRINT_A(
    log_cyan << "has_none = (0 false 1 true) " << has_none << std::endl;
    )

    return has_none ? search_local_result::notfound_with_none : search_local_result::notfound_with_full;
}


uncompact_till_leaf_local_result PrheartNode::uncompact_till_leaf_local(
    span try_key, PrheartSlotData& searched_slot, uint32_t& slot_num
) {
    PRINT_B(
    log_cyan << "try_key = " << key::to_string(try_key) << std::endl;
    )
    // we consider that uncompact_till_leaf must find a key
    uint8_t now_byte = key::get_byte(try_key, now_pos);
    PrheartSlotData* node = (PrheartSlotData*)tree->local_start_ptr;
    bool at_least_found = false;
    PrheartSlotData at_least_one_slot;
    uint32_t at_leat_slot_num;
    for (uint32_t i = 0; i < local_len; ++i) {
        // TODO: debug why some data are error
        if (node[i].node_type() == PrheartNodeType::None) {
            continue;
        }
        #ifdef WILD_PTR_STRIP
        if (((node[i].fptr() & 0x0000'f000'0000'0000ull) >> 44 >= MAX_MEM_NUM_FOR_WILD_PTR) || node[i].version() > 1) {
            #ifdef WILD_PTR_OUTPUT_STOP
            log_error << "press to show" << std::endl;
            str a;
            std::getline(std::cin, a);
            #endif
            #ifdef WILD_PTR_OUTPUT
            log_purple << "this parent slot (very old maybe): " << hex_str(fptr) << std::endl;
            log_error << "fptr=" << hex_str(node[i].fptr()) << std::endl;
            log_cyan << "try_key=" << key::to_string(try_key) << std::endl;
            log_cyan << "now_pos=" << now_pos << std::endl;
            log_cyan << i << "/" << local_len << " " << node[i].get_readable_str() << std::endl;
            #endif
            continue;
        }
        #endif
        if (node[i].key_byte() == now_byte) {
            slot_num = i;
            searched_slot = node[i];
            PRINT_B(
            log_cyan << "uncompact_till_leaf_local: now_byte=" << (uint32_t)now_byte << ", " << searched_slot.get_readable_str() << std::endl;
            )
            return uncompact_till_leaf_local_result::found_use_seems_same_key;
        } else {
            at_least_found = true;
            at_leat_slot_num = i;
            at_least_one_slot = node[i];
        }
    }
    if (at_least_found) {
        searched_slot = at_least_one_slot;
        slot_num = at_leat_slot_num;
        return uncompact_till_leaf_local_result::found_use_different_key;
    }
    return uncompact_till_leaf_local_result::notfound_try_again;
}

uncompact_till_leaf_result PrheartNode::uncompact_till_leaf(
    span try_key,
    vec<PrheartSlotData>& slot_status, vec<uint32_t>& total_pos_status,
    vec<uintptr_t>& parent_slot_fptr_status,
    vec<u8>& real_key
) {

    uint8_t now_byte = key::get_byte(try_key, now_pos);

    PRINT_B(
    log_cyan << "uncompact_till_leaf: key=" << key::to_string(try_key) << ", now_pos=" << now_pos << ", now_byte=" << (uint32_t)now_byte << std::endl;
    )

    switch (type) {
    case PrheartNodeType::None: {
        errorno = ERROR_NORMAL + 9;
        return uncompact_till_leaf_result::error;
    }
    case PrheartNodeType::Leaf: {
        this->rdma_read_real_data(get_size() + 128, fptr); // WARN: consider to be that large
        PrheartLeafData* leaf = (PrheartLeafData*)tree->local_start_ptr;
        if (leaf->key_len > 128) {
            // TODO: we don't deal with this now
            errorno = ERROR_NOTUSE + 2;
            return uncompact_till_leaf_result::error;
        }
        real_key.resize(leaf->key_len);
        memcpy((void*)real_key.data(), (void*)leaf->key_start(), leaf->key_len);
        bool all_same = true;
        if (real_key.size() != try_key.size()) all_same = false;
        for (int i = 0; i < real_key.size(); ++i) {
            if (real_key[i] != try_key[i]) {
                all_same = false;
                break;
            }
        }
        if (all_same) {
            return uncompact_till_leaf_result::same_key;
        }
        return uncompact_till_leaf_result::not_same_key;
    }
    default: {
        PrheartSlotData searched_slot;
        uint32_t bucket_ind, searched_slot_num;
        #ifdef USE_FULL_NODE_READ
        this->rdma_read_real_data();
        #else
        this->rdma_read_bucket_data(now_byte, bucket_ind);
        #endif
        auto r = uncompact_till_leaf_local(try_key, searched_slot, searched_slot_num);
        if (r == uncompact_till_leaf_local_result::notfound_try_again) {
            this->rdma_read_real_data(); // get all data, must have one
            r = uncompact_till_leaf_local(try_key, searched_slot, searched_slot_num);
            if (r == uncompact_till_leaf_local_result::notfound_try_again) {  // maybe the thread not done
                errorno = ERROR_NORMAL + 10;
                return uncompact_till_leaf_result::error;
            }
        }
        // r found here ("seems same" key or "different" key)
        PrheartNode next(
            tree,
            searched_slot.node_type(),
            searched_slot.fptr(),
            now_pos + searched_slot.length()
        );
        #ifdef USE_FULL_NODE_READ
        auto parent_slot_fptr = this->fptr + searched_slot_num * sizeof(PrheartSlotData);
        #else
        auto parent_slot_fptr = this->fptr + (bucket_ind * (tree->hash_bucket_size / sizeof(PrheartSlotData)) + searched_slot_num) * sizeof(PrheartSlotData);
        #endif
        slot_status.push_back(searched_slot);
        total_pos_status.push_back(now_pos);
        parent_slot_fptr_status.push_back(parent_slot_fptr);

        PRINT_B(
        log_cyan << "this->fptr" << ": " << hex_str(this->fptr) << std::endl;
        log_cyan << "bucket_ind=" << bucket_ind << ", searched_slot_num=" << searched_slot_num << std::endl;
        log_cyan << "parent_slot_fptr=" << hex_str(parent_slot_fptr) << std::endl;
        )

        return next.uncompact_till_leaf(
            try_key,
            slot_status, total_pos_status, parent_slot_fptr_status,
            real_key
        );
    }
    }
}

get_diff_node_result PrheartNode::get_diff_node(
    span try_key,
    vec<PrheartSlotData>& slot_status, vec<uint32_t>& total_pos_status,
    vec<uintptr_t>& parent_slot_fptr_status,
    span real_key,
    uint32_t& get_index, uint32_t& get_same_len, uint32_t& not_same_pos
) {

    PRINT_B(
    log_cyan << "get_diff_node: real_key=" << key::to_string(real_key) << std::endl;
    )

    for (int i = 0; i < real_key.size() && i < try_key.size(); ++i) {
        if (key::get_byte(try_key, i) != key::get_byte(real_key, i)) {
            not_same_pos = i;
            break;
        }
    }
    for (uint32_t i = 0; i < slot_status.size(); ++i) {

        PRINT_B(
        log_cyan << "not_same_pos=" << not_same_pos << ", total_pos_status[" << i << "]=" << total_pos_status[i] << ", slot_status[" << i << "].length()=" << slot_status[i].length() << std::endl;
        )

        if (not_same_pos < total_pos_status[i]) continue;
        if (not_same_pos > total_pos_status[i] + slot_status[i].length()) continue;
        if (not_same_pos == total_pos_status[i] + slot_status[i].length()) {
            get_index = i;
            get_same_len = 1;
            return get_diff_node_result::head_diff;
        } else {
            get_index = i;
            get_same_len = not_same_pos - total_pos_status[i];
            return get_diff_node_result::inner_diff;
        }
    }
    return get_diff_node_result::diff_error;
}

bool PrheartNode::search(span key) {

    uint8_t now_byte = key::get_byte(key, now_pos);

    PRINT_A(
    log_green << "this fptr = " << hex_str(fptr) << std::endl;
    log_cyan << "search: key=" << key::to_string(key) << ", now_pos=" << now_pos << ", now_byte(or_full)=" << (uint32_t)now_byte << std::endl;
    )

    switch (type) {
    case PrheartNodeType::None: {
        wrong++;
        PRINT_A(
        log_cyan << "> Search Done (not found): MEET NONE" << std::endl << std::endl;
        )
        errorno = ERROR_NORMAL + 1;
        return false;
    }
    case PrheartNodeType::Leaf: {
        // access_size += 128;
        this->rdma_read_real_data(get_size() + 128, fptr); // WARN: consider to be that large
        PrheartLeafData* leaf = (PrheartLeafData*)tree->local_start_ptr;
        if (leaf->key_len > 128) {
            // TODO: we don't deal with this now
            errorno = ERROR_NOTUSE + 1;
            return false;
        }
        if (leaf->key_len + leaf->value_len > 128) {
            this->rdma_read_real_data(get_size() + leaf->key_len + leaf->value_len, fptr);
        }
        span key_((u8*)leaf->key_start(), leaf->key_len);
        bool all_same = true;
        if (key.size() != key_.size()) all_same = false;
        for (int i = 0; i < key.size(); ++i) {
            if (key[i] != key_[i]) {
                all_same = false;
                break;
            }
        }
        if (all_same) {
            PRINT_A(
            log_cyan << "> Search Done: key=" << key::to_string(key_) << ", value=" << (char*)leaf->value_start() << std::endl << std::endl;
            )
            return true;
        }
        wrong++;
        PRINT_A(
        log_cyan << "> Search Done (not same): key=" << key::to_string(key_) << ", value=" << (char*)leaf->value_start() << std::endl << std::endl;
        )
        errorno = ERROR_NORMAL + 2;
        return false;
    }
    default: {
        PrheartSlotData searched_slot;
        uint32_t _, __;
        uint32_t bucket_ind;
        // access_size += 256;
        #ifdef USE_FULL_NODE_READ
        this->rdma_read_real_data();
        #else
        this->rdma_read_bucket_data(now_byte, bucket_ind);
        #endif
        auto r = search_local(key, searched_slot, _, __);
        if (r != search_local_result::found) {
            PRINT_A(
            log_cyan << "> Search Done (not found): DO NOT FOUND" << std::endl << std::endl;
            )
            errorno = ERROR_NORMAL + 3;
            return false;
        }
        PrheartNode next(
            tree,
            searched_slot.node_type(),
            searched_slot.fptr(),
            now_pos + searched_slot.length()
        );
        return next.search(key);
    }
    }
}

bool PrheartNode::update(span key, span value, uintptr_t parent_slot_fptr) {

    uint8_t now_byte = key::get_byte(key, now_pos);

    PRINT_A(
    log_green << "this fptr = " << hex_str(fptr) << std::endl;
    log_green << "parent_slot_fptr = " << hex_str(parent_slot_fptr) << std::endl;
    log_cyan << "update: key=" << key::to_string(key) << ", now_pos=" << now_pos << ", now_byte(or_full)=" << (uint32_t)now_byte << std::endl;
    )

    switch (type) {

    case PrheartNodeType::None: {
        wrong++;
        PRINT_A(
        log_cyan << "> Update Error (not found): MEET NONE" << std::endl << std::endl;
        )
        errorno = ERROR_NORMAL + 4;
        return false;
    }

    default: {
        PrheartSlotData searched_slot;
        uint32_t searched_slot_num, _;
        uint32_t bucket_ind;
        // access_size += 256;
        #ifdef USE_FULL_NODE_READ
        this->rdma_read_real_data();
        #else
        this->rdma_read_bucket_data(now_byte, bucket_ind);
        #endif
        auto r = search_local(key, searched_slot, searched_slot_num, _);
        if (r != search_local_result::found) {
            PRINT_A(
            log_cyan << "> Update Error (not found): DO NOT FOUND" << std::endl << std::endl;
            )
            errorno = ERROR_NORMAL + 5;
            return false;
        }
        PrheartNode next(
            tree,
            searched_slot.node_type(),
            searched_slot.fptr(),
            now_pos + searched_slot.length()
        );

        #ifdef USE_FULL_NODE_READ
        uintptr_t searched_slot_fptr =
            this->fptr + searched_slot_num * sizeof(PrheartSlotData);
        #else
        uintptr_t searched_slot_fptr =
            this->fptr + (bucket_ind * (tree->hash_bucket_size / sizeof(PrheartSlotData)) + searched_slot_num) * sizeof(PrheartSlotData);
        #endif

        if (searched_slot.node_type() == PrheartNodeType::Leaf) {

            PRINT_A(
            log_cyan << "Leaf" << std::endl;
            )

#ifdef NO_CAS_UPDATE_AND_USE_UPDATE_LOCK

            // ------ lock + inplace + unlock ------

            // lock
#ifndef NO_LOCK
            if (searched_slot.version() == 1) {
                PRINT_A(
                log_error << "is locked, retry" << std::endl;
                )
                errorno = ERROR_IS_LOCKED + 1;
                int i = 0; while (i < 1000) ++i;
                return false;
            }
            PrheartSlotData lock_slot = searched_slot;
            lock_slot.set_version(1);

            bool lock_res = this->rdma_cas_8_byte(
                searched_slot_fptr,
                tree->local_start_ptr, lock_slot.get_data(), searched_slot.get_data()
            );

            PRINT_A(
            log_info << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
            log_info << "searched_slot: " << searched_slot.get_readable_str() << std::endl;
            log_info << "lock_slot: " << lock_slot.get_readable_str() << std::endl;
            )

            if (!lock_res) {
                PRINT_A(
                log_error << "lock failed, retry" << std::endl;
                )
                errorno = ERROR_LOCK + 1;
                return false;
            } else {
                PRINT_A(
                log_info << "lock succeeded" << std::endl;
                )
            }
            searched_slot = lock_slot;
#endif


            // inplace update
            // WARN: don't care the value size (very small)
            auto update_leaf = (PrheartLeafData*)(tree->local_start_ptr);
            update_leaf->set_key((uintptr_t)key.data(), key.size());
            update_leaf->set_value((uintptr_t)value.data(), value.size());
            this->rdma_write_real_data(sizeof(PrheartLeafData) + update_leaf->key_len + update_leaf->value_len, searched_slot.fptr());


            // unlock
#ifndef NO_LOCK
            // PrheartSlotData unlock_slot = searched_slot;
            // unlock_slot.set_version(0);

            // bool unlock_res = this->rdma_cas_8_byte(
            //     searched_slot_fptr,
            //     tree->local_start_ptr, unlock_slot.get_data(), searched_slot.get_data()
            // );

            PrheartSlotData* unlock_slot = (PrheartSlotData*)(tree->local_start_ptr);
            *unlock_slot = searched_slot;
            unlock_slot->set_version(0);

            bool unlock_res = this->rdma_write_real_data(sizeof(PrheartSlotData), searched_slot_fptr);

            PRINT_A(
            log_info << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
            log_info << "searched_slot: " << searched_slot.get_readable_str() << std::endl;
            log_info << "unlock_slot: " << unlock_slot->get_readable_str() << std::endl;
            )
            if (!unlock_res) {
                PRINT_A(
                log_error << "unlock failed, retry" << std::endl;
                )
                errorno = ERROR_LOCK + 2;
                return false;
            } else {
                PRINT_A(
                log_info << "unlock succeeded" << std::endl;
                )
            }

            PRINT_A(
            log_cyan << "> Update new leaf done (inplace)" << std::endl << std::endl;
            )
#endif
            return true;


#else

            // ------ outofplace + cas ------

            uintptr_t new_leaf_fptr = 0;
            auto s = this->tree->malloc(
                ALIGN(key.size() + value.size(), 64), new_leaf_fptr
            );
            if (!s) {
                errorno = ERROR_MALLOC + 1;
                // TODO: add unlock before return!!!
                return false;
            }
            auto new_leaf = (PrheartLeafData*)(tree->local_start_ptr);
            new_leaf->set_key((uintptr_t)key.data(), key.size());
            new_leaf->set_value((uintptr_t)value.data(), value.size());
            this->rdma_write_real_data(sizeof(PrheartSlotData) + new_leaf->key_len + new_leaf->value_len, new_leaf_fptr);

            PRINT_A(
            log_cyan << "new_leaf fptr=" << hex_str(new_leaf_fptr) << std::dec << std::endl;
            log_cyan << "new_leaf: " << new_leaf->get_readable_str() << std::endl;
            )

            // CAS old slot
            // remote slot fptr
            PrheartSlotData new_slot = searched_slot;
            new_slot.set_node_type(PrheartNodeType::Leaf);
            new_slot.set_fptr(new_leaf_fptr);
            new_slot.set_version(0);  // unlock here

            auto res = this->rdma_cas_8_byte(
                searched_slot_fptr,
                tree->local_start_ptr, new_slot.get_data(), searched_slot.get_data()
            );

            PRINT_A(
            log_cyan << "slot_fptr=" << hex_str(searched_slot_fptr) << std::dec << std::endl;
            log_cyan << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
            log_cyan << "searched_slot: " << searched_slot.get_readable_str() << std::endl;
            log_cyan << "new_slot: " << new_slot.get_readable_str() << std::endl;
            )

            if (!res) {
                PRINT_A(
                log_cyan << "> Update new leaf CAS error" << std::endl << std::endl;
                )
                errorno = ERROR_CAS + 1;
                return false;
            }
            PRINT_A(
            log_cyan << "> Update new leaf done" << std::endl << std::endl;
            )

            return true;

#endif
        } else {
            return next.update(
                key, value, searched_slot_fptr
            );
        }
    }
    }
}

bool PrheartNode::insert(span key, span value, uintptr_t parent_slot_fptr) {

    uint8_t now_byte = key::get_byte(key, now_pos);

    PRINT_A(
    log_green << "this fptr = " << hex_str(fptr) << std::endl;
    log_green << "parent_slot_fptr = " << hex_str(parent_slot_fptr) << std::endl;
    log_cyan << "insert: key=" << key::to_string(key) << ", now_pos=" << now_pos << ", now_byte=" << (uint32_t)now_byte << std::endl;
    )

    switch (type) {
    case PrheartNodeType::None: {
        errorno = ERROR_NORMAL + 6;
        return false;
    }
    case PrheartNodeType::Leaf: {
        PRINT_A(
        log_cyan << "Leaf" << std::endl;
        )
        // update leaf, normally won't be here (insert is not update), just in case
        // must be same: if not same, won't be here
        // WARN: don't care the value size (very small)
        auto update_leaf = (PrheartLeafData*)(tree->local_start_ptr);
        update_leaf->set_key((uintptr_t)key.data(), key.size());
        update_leaf->set_value((uintptr_t)value.data(), value.size());
        this->rdma_write_real_data(get_size() + update_leaf->key_len + update_leaf->value_len, fptr);
        PRINT_A(
        log_cyan << "> Insert new leaf done (update)" << std::endl << std::endl;
        )
        return true;
    }
    default: {
        PRINT_A(
        log_cyan << "Node" << std::endl;
        )

        // 1, search_result::notfound_with_none (safe): just insert to none slot
        // 2, search_result::notfound_with_full (safe): upgrade the inner node
        // 3, search_result::found && searched_slot.length() == 1 (safe): continue to insert next node
        // 4, search_result::found && searched_slot.length() > 1 (danger):
        //    uncompact_till_leaf (search and get real key) -> get diff node ->
        //    no diff:
        //      4.1, update the leaf
        //    (not same key) node keybyte head diff:
        //      4.2, go to the diff node, and perform (1) or (2)
        //    (not same key) node keybyte same, compact inner diff:
        //      4.3, split and insert
        PrheartSlotData searched_slot;
        uint32_t first_none_index, searched_slot_num, bucket_ind;
        #ifdef USE_FULL_NODE_READ
        this->rdma_read_real_data();
        #else
        this->rdma_read_bucket_data(now_byte, bucket_ind);
        #endif
        auto r = this->search_local(
            key, searched_slot, searched_slot_num, first_none_index
        );
        if (r == search_local_result::notfound_with_none) {
            PRINT_A(
            log_cyan << "notfound_with_none (safe): buc_ind=" << bucket_ind << ", non_ind=" << first_none_index << std::endl;
            log_cyan << "NEW_LEAF" << std::endl;
            )
            // 1, insert to none slot

            // save old for CAS
            PrheartSlotData old_slot = *(PrheartSlotData*)(tree->local_start_ptr + first_none_index * sizeof(PrheartSlotData));

            #ifdef USE_FULL_NODE_READ
            auto slot_fptr = fptr + first_none_index * sizeof(PrheartSlotData);
            #else
            auto slot_fptr = fptr + bucket_ind * tree->hash_bucket_size + first_none_index * sizeof(PrheartSlotData);
            #endif

            #if defined(USE_INSERT_LOCK) || defined(USE_INSERT_LEAF_LOCK)
            if (old_slot.version() == 1) {
                PRINT_A(
                log_error << "is locked, retry" << std::endl;
                )
                errorno = ERROR_IS_LOCKED + 2;
                int i = 0; while (i < 1000) ++i;
                return false;
            }
            PrheartSlotData lock_slot = old_slot;
            lock_slot.set_version(1);

            bool lock_res = this->rdma_cas_8_byte(
                slot_fptr,
                tree->local_start_ptr, lock_slot.get_data(), old_slot.get_data()
            );

            PRINT_A(
            log_info << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
            log_info << "old_slot: " << old_slot.get_readable_str() << std::endl;
            log_info << "lock_slot: " << lock_slot.get_readable_str() << std::endl;
            )

            if (!lock_res) {
                PRINT_A(
                log_error << "lock failed, retry" << std::endl;
                )
                errorno = ERROR_LOCK + 3;
                return false;
            } else {
                PRINT_A(
                log_info << "lock succeeded" << std::endl;
                )
            }
            old_slot = lock_slot;
            #endif

            // create new leaf
            uintptr_t new_leaf_fptr = 0;
            auto s = this->tree->malloc(
                ALIGN(key.size() + value.size(), 64), new_leaf_fptr
            );
            if (!s) {
                errorno = ERROR_MALLOC + 2;
                // TODO: add unlock before return!!!
                return false;
            }
            auto new_leaf = (PrheartLeafData*)(tree->local_start_ptr);
            new_leaf->set_key((uintptr_t)key.data(), key.size());
            new_leaf->set_value((uintptr_t)value.data(), value.size());
            this->rdma_write_real_data(sizeof(PrheartSlotData) + new_leaf->key_len + new_leaf->value_len, new_leaf_fptr);

            PRINT_A(
            log_cyan << "new_leaf_fptr=" << hex_str(new_leaf_fptr) << std::dec << std::endl;
            log_cyan << "new_leaf: " << new_leaf->get_readable_str() << std::endl;
            )

            // CAS old slot
            // remote slot fptr
            PrheartSlotData new_slot = old_slot;
            new_slot.set_node_type(PrheartNodeType::Leaf);
            new_slot.set_fptr(new_leaf_fptr);
            new_slot.set_length(key.size() - now_pos);
            new_slot.set_key_byte(now_byte);
            new_slot.set_version(0);  // unlock here

            auto res = this->rdma_cas_8_byte(
                slot_fptr,
                tree->local_start_ptr, new_slot.get_data(), old_slot.get_data()
            );

            PRINT_A(
            log_cyan << "slot_fptr=" << hex_str(slot_fptr) << std::dec << std::endl;
            log_cyan << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
            log_cyan << "old_slot: " << old_slot.get_readable_str() << std::endl;
            log_cyan << "new_slot: " << new_slot.get_readable_str() << std::endl;
            )

            if (!res) {
                #if defined(USE_INSERT_LOCK) || defined(USE_INSERT_LEAF_LOCK)
                log_cyan << "slot_fptr=" << hex_str(slot_fptr) << std::dec << std::endl;
                log_cyan << "value: " << ((PrheartSlotData*)tree->local_start_ptr)->get_data() << ", " << old_slot.get_data() << std::endl;
                log_cyan << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
                log_cyan << "old_slot: " << old_slot.get_readable_str() << std::endl;
                log_cyan << "new_slot: " << new_slot.get_readable_str() << std::endl;
                log_error << "unlock failed!!! please find why!!!" << std::endl;
                #endif
                PRINT_A(
                log_cyan << "> Insert new leaf CAS error" << std::endl << std::endl;
                )
                errorno = ERROR_CAS + 2;
                return false;
            }
            #if defined(USE_INSERT_LOCK) || defined(USE_INSERT_LEAF_LOCK)
            PRINT_A(
            log_info << "unlock done" << std::endl;
            )
            #endif
            PRINT_A(
            log_cyan << "> Insert new leaf done" << std::endl << std::endl;
            )

            return true;

        } else if (r == search_local_result::notfound_with_full) {
            PRINT_A(
            log_cyan << "notfound_with_full (safe)" << std::endl;
            log_cyan << "UPGRADE(0)" << std::endl;
            )
            // 2, upgrade the inner node
            // metadata: fptr, parent_slot_fptr
            this->rdma_read_real_data(sizeof(PrheartSlotData), parent_slot_fptr);
            PrheartSlotData old_slot = *(PrheartSlotData*)(tree->local_start_ptr);

            // very important!!!!!!!
            // a upgrade CAS from another thread may already occurred before this->rdma_read_real_data
            // so do this
            // just like frozen the old data
            if (old_slot.node_type() != type) {
                PRINT_A(
                log_cyan << "> An upgrade is already occurred" << std::endl;
                )
                errorno = ERROR_JUST_LIKE_CAS + 1;
                return false;
            }

            #ifdef USE_INSERT_LOCK
            if (old_slot.version() == 1) {
                PRINT_A(
                log_error << "is locked, retry" << std::endl;
                )
                errorno = ERROR_IS_LOCKED + 3;
                int i = 0; while (i < 1000) ++i;
                return false;
            }
            PrheartSlotData lock_slot = old_slot;
            lock_slot.set_version(1);

            bool lock_res = this->rdma_cas_8_byte(
                parent_slot_fptr,
                tree->local_start_ptr, lock_slot.get_data(), old_slot.get_data()
            );

            PRINT_A(
            log_info << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
            log_info << "old_slot: " << old_slot.get_readable_str() << std::endl;
            log_info << "lock_slot: " << lock_slot.get_readable_str() << std::endl;
            )

            if (!lock_res) {
                PRINT_A(
                log_error << "lock failed, retry" << std::endl;
                )
                errorno = ERROR_LOCK + 4;
                return false;
            } else {
                PRINT_A(
                log_info << "lock succeeded" << std::endl;
                )
            }
            old_slot = lock_slot;
            #endif

            // read the old data
            this->rdma_read_real_data();
            const auto new_type = PrheartNodeType((int)(type) + 1);
            const auto new_size = type_to_size(new_type);
            const auto new_num = new_size / sizeof(PrheartSlotData);
            const auto new_bucket_num = new_size / tree->hash_bucket_size == 0 ? 1 : new_size / tree->hash_bucket_size;
            const auto each_bucket_slot_size = tree->hash_bucket_size / sizeof(PrheartSlotData);
            PRINT_A(
            log_cyan
              << "new_type=(2:8, 3:16, ...) " << (int)new_type
              << ", new_size=" << new_size << ", new_num=" << new_num
              << ", tree->hash_bucket_size=" << tree->hash_bucket_size
              << ", new_bucket_num=" << new_bucket_num << ", each_bucket_slot_size=" << each_bucket_slot_size << std::endl;
            )
            uint32_t each_bucket_now_ind[new_bucket_num];
            PrheartSlotData total_new_data[new_num];
            PrheartSlotData* old_node = (PrheartSlotData*)(tree->local_start_ptr);
            for (int i = 0; i < new_bucket_num; ++i) each_bucket_now_ind[i] = 0;
            for (int i = 0; i < new_num; ++i) total_new_data[i].set_data(0);
            // update it
            for (int i = 0; i < get_size() / sizeof(PrheartSlotData); ++i) {
                PRINT_A(
                if (old_node[i].node_type() != PrheartNodeType::None)
                    log_cyan << old_node[i].get_readable_str() << std::endl;
                )
                if (old_node[i].node_type() == PrheartNodeType::None) continue;
                auto bucket = (uint32_t)old_node[i].key_byte() % new_bucket_num;
                total_new_data[bucket * each_bucket_slot_size + each_bucket_now_ind[bucket]].set_data(old_node[i].get_data());
                PRINT_A(
                log_cyan << "new bucket=" << bucket << ", each_bucket_now_ind[bucket]=" << each_bucket_now_ind[bucket] << std::endl;
                )
                each_bucket_now_ind[bucket]++;
            }
            // write to remote
            uintptr_t new_node_space = 0;
            auto s = this->tree->malloc(
                ALIGN(new_size, 64), new_node_space
            );
            if (!s) {
                errorno = ERROR_MALLOC + 3;
                return false;
            }
            memcpy((void*)tree->local_start_ptr, (void*)total_new_data, new_size);
            this->rdma_write_real_data(new_size, new_node_space);
            PrheartSlotData new_parent_slot = old_slot;
            new_parent_slot.set_node_type(new_type);
            new_parent_slot.set_fptr(new_node_space);
            new_parent_slot.set_version(0); // unlock here

            auto res = this->rdma_cas_8_byte(
                parent_slot_fptr, 
                tree->local_start_ptr, 
                new_parent_slot.get_data(), old_slot.get_data()
            );

            PRINT_A(
            log_cyan << "parent_slot_fptr=" << hex_str(parent_slot_fptr) << std::endl;
            log_cyan << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
            log_cyan << "old_slot: " << old_slot.get_readable_str() << std::endl;
            log_cyan << "new_slot: " << new_parent_slot.get_readable_str() << std::endl;
            )

            if (!res) {
                #ifdef USE_INSERT_LOCK
                log_cyan << "parent_slot_fptr=" << hex_str(parent_slot_fptr) << std::endl;
                log_cyan << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
                log_cyan << "old_slot: " << old_slot.get_readable_str() << std::endl;
                log_cyan << "new_slot: " << new_parent_slot.get_readable_str() << std::endl;
                log_error << "unlock failed!!! please find why!!!" << std::endl;
                #endif
                PRINT_A(
                log_cyan << "> Inner upgrade CAS error" << std::endl;
                )
                errorno = ERROR_CAS + 3;
                return false;
            }
            #ifdef USE_INSERT_LOCK
            PRINT_A(
            log_info << "unlock done" << std::endl;
            )
            #endif
            PRINT_A(
            log_cyan << "> Inner upgrade done" << std::endl;
            )

            PrheartNode next(
                tree,
                new_type,
                new_node_space,
                now_pos
            );
            return next.insert(key, value, parent_slot_fptr);

        } else if (r == search_local_result::found && searched_slot.length() == 1) {
            PRINT_A(
            log_cyan << "found (safe)" << std::endl;
            log_cyan << "CONTINUE(0)" << std::endl;
            )
            // 3, continue to insert next node
            #ifdef USE_FULL_NODE_READ
            uintptr_t parent_slot_fptr =
                this->fptr + searched_slot_num * sizeof(PrheartSlotData);
            #else
            uintptr_t parent_slot_fptr =
                this->fptr + (bucket_ind * (tree->hash_bucket_size / sizeof(PrheartSlotData)) + searched_slot_num) * sizeof(PrheartSlotData);
            #endif
            PrheartNode next(
                tree,
                searched_slot.node_type(),
                searched_slot.fptr(),
                now_pos + searched_slot.length()
            );
            return next.insert(key, value, parent_slot_fptr);

        } else if (r == search_local_result::found && searched_slot.length() > 1) {
            PRINT_A(
            log_cyan << "found (danger)" << std::endl;
            log_cyan << "CONTINUE(1/2) or SPLIT" << std::endl;
            )
            // 4, uncompact_till_leaf
            PrheartNode next(
                tree,
                searched_slot.node_type(),
                searched_slot.fptr(),
                now_pos + searched_slot.length()
            );
            vec<PrheartSlotData> slot_status;
            vec<uint32_t> total_pos_status;
            vec<uintptr_t> parent_slot_fptr_status;
            vec<u8> real_key;
            #ifdef USE_FULL_NODE_READ
            uintptr_t parent_slot_fptr =
                this->fptr + searched_slot_num * sizeof(PrheartSlotData);
            #else
            uintptr_t parent_slot_fptr =
                this->fptr + (bucket_ind * (tree->hash_bucket_size / sizeof(PrheartSlotData)) + searched_slot_num) * sizeof(PrheartSlotData);
            #endif
            slot_status.push_back(searched_slot);
            total_pos_status.push_back(now_pos);
            parent_slot_fptr_status.push_back(parent_slot_fptr);

            PRINT_A(
            log_cyan << "this->fptr" << ": " << hex_str(this->fptr) << std::endl;
            log_cyan << "bucket_ind=" << bucket_ind << ", searched_slot_num=" << searched_slot_num << std::endl;
            log_cyan << "parent_slot_fptr=" << hex_str(parent_slot_fptr) << std::endl;
            )

            auto nr = next.uncompact_till_leaf(
                key,
                slot_status, total_pos_status, parent_slot_fptr_status,
                real_key
            );
            if (nr == uncompact_till_leaf_result::error) {
                errorno = ERROR_NORMAL + 7;
                return false;
            }
            if (nr == uncompact_till_leaf_result::same_key) {
                PRINT_A(
                log_cyan << "same_key" << std::endl;
                log_cyan << "CONTINUE(1)" << std::endl;
                )
                // 4.1 update leaf, normally won't be here (insert is not update), just in case
                PrheartNode next(
                    tree,
                    PrheartNodeType::Leaf,
                    slot_status.back().fptr(),
                    total_pos_status.back() + slot_status.back().length()
                );
                uintptr_t next_parent_slot =
                    parent_slot_fptr_status.size() >= 2 ?
                    parent_slot_fptr_status[parent_slot_fptr_status.size() - 2] :
                    parent_slot_fptr;
                return next.insert(
                    key, value, next_parent_slot
                );

            } else {
                uint32_t get_index = 0, get_same_len = 0, not_same_pos = 0;
                auto res = get_diff_node(
                    key,
                    slot_status, total_pos_status,
                    parent_slot_fptr_status,
                    real_key,
                    get_index, get_same_len, not_same_pos
                );
                PRINT_A(
                for (int i = 0; i < slot_status.size(); ++i) {
                    log_purple << i << "/" << slot_status.size() << std::endl;
                    log_cyan << "slot_status[" << i << "]: " << slot_status[i].get_readable_str() << std::endl;
                    log_cyan << "total_pos_status[" << i << "]: " << total_pos_status[i] << std::endl;
                    log_cyan << "parent_slot_fptr_status[" << i << "]: " << hex_str(parent_slot_fptr_status[i]) << std::endl;
                }
                log_purple << "status output done" << std::endl;
                log_cyan << "get_index=" << get_index << ", get_same_len=" << get_same_len << ", not_same_pos=" << not_same_pos << std::endl;
                )
                if (res == get_diff_node_result::head_diff) {
                    PRINT_A(
                    log_cyan << "head_diff" << std::endl;
                    log_cyan << "CONTINUE(2) " << std::endl;
                    )
                    // 4.2 go to the diff node, and perform (1) or (2)
                    PrheartNode next(
                        tree,
                        slot_status[get_index].node_type(),
                        slot_status[get_index].fptr(),
                        total_pos_status[get_index] + slot_status[get_index].length()
                    );
                    return next.insert(key, value, parent_slot_fptr_status[get_index]);

                } else {
                    split:
                    PRINT_A(
                    log_cyan << "inner_diff" << std::endl;
                    log_cyan << "SPLIT" << std::endl;
                    )
                    // 4.3 split and insert
                    // make a new node, insert, move old to new

                    // save old for CAS
                    PrheartSlotData old_slot = slot_status[get_index];

                    #ifdef USE_INSERT_LOCK
                    if (old_slot.version() == 1) {
                        PRINT_A(
                        log_error << "is locked, retry" << std::endl;
                        )
                        errorno = ERROR_IS_LOCKED + 4;
                        int i = 0; while (i < 1000) ++i;
                        return false;
                    }
                    PrheartSlotData lock_slot = old_slot;
                    lock_slot.set_version(1);

                    bool lock_res = this->rdma_cas_8_byte(
                        parent_slot_fptr_status[get_index],
                        tree->local_start_ptr, lock_slot.get_data(), old_slot.get_data()
                    );

                    PRINT_A(
                    log_info << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
                    log_info << "old_slot: " << old_slot.get_readable_str() << std::endl;
                    log_info << "lock_slot: " << lock_slot.get_readable_str() << std::endl;
                    )

                    if (!lock_res) {
                        PRINT_A(
                        log_error << "lock failed, retry" << std::endl;
                        )
                        errorno = ERROR_LOCK + 5;
                        return false;
                    } else {
                        PRINT_A(
                        log_info << "lock succeeded" << std::endl;
                        )
                    }
                    old_slot = lock_slot;
                    #endif

                    // create split's new node8
                    uintptr_t new_node8_fptr = 0;
                    auto s = this->tree->malloc(
                        ALIGN(type_to_size(PrheartNodeType::Node8), 64),
                        new_node8_fptr
                    );
                    if (!s) {
                        errorno = ERROR_MALLOC + 4;
                        return false;
                    }

                    // create new leaf
                    uintptr_t new_leaf_fptr = 0;
                    auto s2 = this->tree->malloc(
                        ALIGN(key.size() + value.size(), 64), new_leaf_fptr
                    );
                    if (!s2) {
                        errorno = ERROR_MALLOC + 5;
                        return false;
                    }

                    PRINT_A(
                    log_cyan << "diff_key_1=" << (uint32_t)key::get_byte(real_key, not_same_pos)
                        << ", diff_key_2=" << (uint32_t)key::get_byte(key, not_same_pos)
                        << std::endl;
                    )

                    // node8 init and write
                    memset(
                        (void*)(tree->local_start_ptr), 0,
                        type_to_size(PrheartNodeType::Node8)
                    );
                    auto new_slot = (PrheartSlotData*)(tree->local_start_ptr);
                    new_slot->set_fptr(slot_status[get_index].fptr());
                    new_slot->set_node_type(slot_status[get_index].node_type());
                    new_slot->set_key_byte(
                        key::get_byte(real_key, not_same_pos)
                    );
                    new_slot->set_length(slot_status[get_index].length() - get_same_len);
                    new_slot->set_version(0);
                    auto new_slot_2 = (PrheartSlotData*)(tree->local_start_ptr) + 1;
                    new_slot_2->set_fptr(new_leaf_fptr);
                    new_slot_2->set_node_type(PrheartNodeType::Leaf);
                    new_slot_2->set_key_byte(
                        key::get_byte(key, not_same_pos)
                    );
                    new_slot_2->set_length(key.size() - get_same_len);
                    new_slot_2->set_version(0);
                    this->rdma_write_real_data(
                        type_to_size(PrheartNodeType::Node8), new_node8_fptr
                    );

                    PRINT_A(
                    log_cyan << "2 new slots saved int new_node8_fptr=" << hex_str(new_node8_fptr) << std::endl;
                    log_cyan << "new_slot: " << new_slot->get_readable_str() << std::endl;
                    log_cyan << "new_slot_2: " << new_slot_2->get_readable_str() << std::endl;
                    )

                    // leaf init and write
                    auto new_leaf = (PrheartLeafData*)(tree->local_start_ptr);
                    new_leaf->set_key((uintptr_t)key.data(), key.size());
                    new_leaf->set_value((uintptr_t)value.data(), value.size());
                    this->rdma_write_real_data(
                        sizeof(PrheartSlotData) + new_leaf->key_len + new_leaf->value_len, new_leaf_fptr
                    );

                    PRINT_A(
                    log_cyan << "new_leaf_fptr=" << hex_str(new_leaf_fptr) << std::dec << std::endl;
                    log_cyan << "new_leaf: " << new_leaf->get_readable_str() << std::endl;
                    )

                    PrheartSlotData parent_slot = old_slot;
                    parent_slot.set_node_type(PrheartNodeType::Node8);
                    parent_slot.set_fptr(new_node8_fptr);
                    parent_slot.set_length(get_same_len);
                    parent_slot.set_version(0); // unlock here

                    auto res = this->rdma_cas_8_byte(
                        parent_slot_fptr_status[get_index],
                        tree->local_start_ptr,
                        parent_slot.get_data(),
                        old_slot.get_data()
                    );

                    PRINT_A(
                    log_cyan << "parent_slot_fptr_status[get_index]=" << hex_str(parent_slot_fptr_status[get_index]) << std::endl;
                    log_cyan << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
                    log_cyan << "old_slot: " << old_slot.get_readable_str() << std::endl;
                    log_cyan << "new_slot: " << parent_slot.get_readable_str() << std::endl;
                    )

                    if (!res) {
                        #ifdef USE_INSERT_LOCK
                        log_cyan << "parent_slot_fptr_status[get_index]=" << hex_str(parent_slot_fptr_status[get_index]) << std::endl;
                        log_cyan << "remote get: " << ((PrheartSlotData*)tree->local_start_ptr)->get_readable_str() << std::endl;
                        log_cyan << "old_slot: " << old_slot.get_readable_str() << std::endl;
                        log_cyan << "new_slot: " << parent_slot.get_readable_str() << std::endl;
                        log_error << "unlock failed!!! please find why!!!" << std::endl;
                        #endif
                        PRINT_A(
                        log_cyan << "> Insert new leaf CAS error" << std::endl << std::endl;
                        )
                        errorno = ERROR_CAS + 4;
                        return false;
                    }
                    #ifdef USE_INSERT_LOCK
                    PRINT_A(
                    log_info << "unlock done" << std::endl;
                    )
                    #endif
                    PRINT_A(
                    log_cyan << "> Insert new leaf done" << std::endl << std::endl;
                    )

                    return true;
                }
            }
        } else {
            // log_error << "search result = " << (int)r << std::endl;
            // log_error << "searched_slot.length() = " << searched_slot.length() << std::endl;
            errorno = ERROR_NORMAL + 8;
            return false;
        }
    }
    }
}

bool PrheartNode::scan(span start_key, span end_key, vec<std::string>& buffer, scan_choice choice) {

    switch (type) {
    case PrheartNodeType::None: {
        wrong++;
        PRINT_A(
        log_cyan << "> Scan Done (not found): MEET NONE" << std::endl << std::endl;
        )
        errorno = ERROR_NORMAL + 11;
        return false;
    }
    case PrheartNodeType::Leaf: {
        this->rdma_read_real_data(get_size() + 128, fptr);
        PrheartLeafData *leaf = (PrheartLeafData*)tree->local_start_ptr;
        if (leaf->key_len > 128) {
            errorno = ERROR_NORMAL + 12;
            return false;
        }
        if (leaf->key_len + leaf->value_len > 128) {
            this->rdma_read_real_data(get_size() + leaf->key_len + leaf->value_len, fptr);
        }
        std::string key_str;
        key_str.assign((char*)leaf->key_start(), leaf->key_len);
        std::string value_str;
        value_str.assign((char*)leaf->value_start(), leaf->value_len);
        // if(choice == optimistic) {
        //     if(key_str.compare((char*)start_key.data()) < 0) {
        //         return false;
        //     } else if(key_str.compare((char*)end_key.data()) > 0) {
        //         return false;
        //     }
        // }
        buffer.push_back(key_str);
        // buffer.push_back(value_str);
        return true;
    }

    default:{
        this->rdma_read_real_data();
        return scan_local(start_key, end_key, buffer, choice);
    }
    }
}

bool PrheartNode::scan_local(span start_key, span end_key, vec<str> &buffer, scan_choice choice) {
    uint8_t start_key_byte = key::get_byte(start_key, now_pos);
    uint8_t end_key_byte = key::get_byte(end_key, now_pos);
    PrheartSlotData node[local_len];
    memcpy((void*)node, (void*)tree->local_start_ptr, local_len * sizeof(PrheartSlotData));

    for (uint32_t i = 0; i < local_len; ++i) {
        uint8_t node_key_byte = node[i].key_byte();
        if (node[i].node_type() == PrheartNodeType::None) {
            continue;
        }
        PrheartNode next(
            tree,
            node[i].node_type(),
            node[i].fptr(),
            now_pos + node[i].length()
        );
        if (choice == optimistic || node[i].length() > 1) {
            return next.scan(start_key, end_key, buffer, optimistic);
        }
        else if (choice == certain) {
            return next.scan(start_key, end_key, buffer, certain);
        }
        else if (choice == lower_bound) {
            if (node_key_byte < start_key_byte) {
                continue;
            } else if(node_key_byte == start_key_byte) {
                return next.scan(start_key, end_key, buffer, lower_bound);
            } else {
                return next.scan(start_key, end_key, buffer, certain);
            }
        }
        else if (choice == upper_bound) {
            if (node_key_byte > end_key_byte) {
                continue;
            } else if(node_key_byte == end_key_byte) {
                return next.scan(start_key, end_key, buffer, upper_bound);
            } else {
                return next.scan(start_key, end_key, buffer, certain);
            }
        }
        else {
            if (node_key_byte < start_key_byte || node_key_byte > end_key_byte) {
                continue;
            } else if (node_key_byte == start_key_byte && node_key_byte < end_key_byte) {
                return next.scan(start_key, end_key, buffer, lower_bound);
            } else if (node_key_byte > start_key_byte && node_key_byte < end_key_byte) {
                return next.scan(start_key, end_key, buffer, certain);
            } else if (node_key_byte == end_key_byte && node_key_byte > start_key_byte) {
                return next.scan(start_key, end_key, buffer, upper_bound);
            } else if (node_key_byte == end_key_byte && node_key_byte == start_key_byte) {
                return next.scan(start_key, end_key, buffer, unknown);
            }
        }
    }
    errorno = ERROR_NORMAL + 13;
    return false;
}

bool PrheartNode::remove(span key) {
    switch (type) {
    case PrheartNodeType::None: {
        errorno = ERROR_NOTUSE;
        return false;
    }
    case PrheartNodeType::Leaf: {
        errorno = ERROR_NOTUSE;
        return false;
    }
    default: {
        errorno = ERROR_NOTUSE;
        return false;
    }
    }
}

void PrheartNode::print_tree(uint32_t now_level) {
    switch (type) {
    case PrheartNodeType::None: {
        return;
    }
    case PrheartNodeType::Leaf: {
        this->rdma_read_real_data(get_size() + 128, fptr);
        PrheartLeafData* leaf = (PrheartLeafData*)tree->local_start_ptr;
        for (int i = 0; i < now_level; ++i) std::cout << "  ";
        if (leaf->key_len > 128) {
            stdlog::error << "leaf->key_len = " << leaf->key_len << ", too large!" << std::endl;
        } else {
            stdlog::green << leaf->get_readable_str() << std::endl;
        }
        return;
    }
    default: {
        this->rdma_read_real_data();
        PrheartSlotData node[local_len];
        memcpy((void*)node, (void*)tree->local_start_ptr, local_len * sizeof(PrheartSlotData));
        for (uint32_t i = 0; i < local_len; ++i) {
            for (int i = 0; i < now_level; ++i) std::cout << "  ";
            std::cout << node[i].get_readable_str() << std::endl;
            PrheartNode next(
                tree,
                node[i].node_type(),
                node[i].fptr(),
                now_pos + node[i].length()
            );
            next.print_tree(now_level + 1);
        }
        return;
    }
    }
}


uint64_t PrheartNode::dfs(uint64_t level, uint64_t prev_pos) {
    uint64_t num = 0;
    switch (type) {
        case PrheartNodeType::None: {
            return 0;
        }
        case PrheartNodeType::Leaf: {
            leaf_prefix_num[prev_pos]++;
            leaf_len[now_pos]++;
            leaf_num[level]++;
            node_type_num[uint8_t(this->type)]++;
            return 1;
        }
        default: {
            this->rdma_read_real_data();
            node_type_num[uint8_t(this->type)]++;
            node_num[level]++;
            node_prefix_num[now_pos]++;
            PrheartSlotData node[local_len];
            memcpy((void*)node, (void*)tree->local_start_ptr, local_len * sizeof(PrheartSlotData));
            for (uint32_t i = 0; i < local_len; ++i) {
                PrheartNode next(
                    tree,
                    node[i].node_type(),
                    node[i].fptr(),
                    now_pos + node[i].length()
                );
                num += next.dfs(level+1, now_pos);
            }
            return num;
        }
    }
}

std::pair<uint64_t, uint64_t> PrheartNode::cal_cost(uint64_t level, uint64_t prev_pos) {
    uint64_t num = 0;
    switch (type) {
        case PrheartNodeType::None: {
            return std::make_pair(0, 0);
        }
        case PrheartNodeType::Leaf: {
            leaf_prefix_num[prev_pos]++;
            leaf_len[now_pos]++;
            leaf_num[level]++;
            for (int i=prev_pos+1; i<=now_pos; ++i)
            {
                if (i < MAX_LEVEL)
                {
                    compress[i]++;
                    costs[i]--;
                }
            }

            return std::make_pair(1, 0);
        }
        default: {
            uint64_t kvs = 0;
            uint64_t cost = 0;
            this->rdma_read_real_data();
            node_num[level]++;
            node_prefix_num[now_pos]++;
            PrheartSlotData node[local_len];
            memcpy((void*)node, (void*)tree->local_start_ptr, local_len * sizeof(PrheartSlotData));
            for (uint32_t i = 0; i < local_len; ++i) {
                uint64_t next_pos = now_pos + node[i].length();
                if (!prefix_list.empty() && (node[i].node_type() != PrheartNodeType::Leaf) && (next_pos + 1) > prefix_list.back())
                    continue;
                PrheartNode next(
                    tree,
                    node[i].node_type(),
                    node[i].fptr(),
                    now_pos + node[i].length()
                );
                std::pair<uint64_t, uint64_t> res = next.cal_cost(level+1, now_pos);
                kvs += res.first;
                cost = kvs * (level - 1);
            }
            for (uint32_t i = now_pos; i > prev_pos; i--)
            {
                costs[i] += cost;
            }
            return std::make_pair(kvs, cost);
        }
    }
}

span PrheartNode::get_prefix(uint64_t len) {
    auto key = get_one_leaf();
    span prefix = key::get_prefix(key, len);
    // if (now_pos == 1)
    // log_info << "key: " << key::to_string(key) << " len: " << prefix.size() << " prefix: " << key::to_string(prefix) << " error" << std::endl;

    return prefix;
}

span PrheartNode::get_one_leaf() {

    switch (type) {
        case PrheartNodeType::None: {
            return span();
        }
        case PrheartNodeType::Leaf: {
            this->rdma_read_real_data(get_size() + 128, fptr); // WARN: consider to be that large
            PrheartLeafData* leaf = (PrheartLeafData*)tree->local_start_ptr;
            if (leaf->key_len > 128) {
                // TODO: we don't deal with this now
                return span();
            }
            // key.resize(leaf->key_len);
            span key((u8*)leaf->key_start(), leaf->key_len);
            // memcpy((void*)key.data(), (void*)leaf->key_start(), leaf->key_len);
            return key;
        }
        default: {
            this->rdma_read_real_data();
            PrheartSlotData node[local_len];
            memcpy((void*)node, (void*)tree->local_start_ptr, local_len * sizeof(PrheartSlotData));
            for (uint32_t i = 0; i < local_len; ++i) {
                PrheartNode next(
                    tree,
                    node[i].node_type(),
                    node[i].fptr(),
                    now_pos + node[i].length()
                );
                span key = next.get_one_leaf();
                if (!key.empty())
                    return key;
            }
            return span();
        }
    }
}
#ifdef DYNAMIC
uint64_t PrheartNode::add_shortcut(uint64_t level) {
    uint64_t num = 0;
    switch (type) {
        case PrheartNodeType::None: {
            return 0;
        }
        case PrheartNodeType::Leaf: {
            return 0;
        }
        default: {
            this->rdma_read_real_data();
            PrheartSlotData node[local_len];
            memcpy((void*)node, (void*)tree->local_start_ptr, local_len * sizeof(PrheartSlotData));
            uint64_t next_level = level;
            // log_info << "now pos: " << now_pos << "level:" << level << std::endl;
            if ((now_pos + 1) > prefix_list[level]) {
                num++;
                do {
                    next_level--;
                    if (next_level < 0)
                        break;
                } while ((now_pos + 1) > prefix_list[next_level]);
                RACE::Slice key, value;
                auto prefix = get_prefix(prefix_list[next_level+1]);
                shortcut_num[prefix_list[next_level+1]]++;
                RACE::Node_Meta node;
                node.fptr = fptr;
                node.pos = uint8_t(now_pos);
                node.type = static_cast<uint8_t>(type);
                value.len = sizeof(uint64_t);
                value.data = (char*)&node;
                key.len = prefix.size();
                key.data = (char*)prefix.data();

                uint64_t pattern = RACE::hash(key.data, key.len) >> 64;
                uint64_t mem_idx = pattern % tree->memory_machine_num;
                tree->rdma_cli[mem_idx]->run(tree->race_cli[mem_idx]->insert(&key, &value));

                RACE::Slice ret_value;
                char buffer[1024];
                ret_value.data = buffer;
                ret_value.len = 0;
                tree->rdma_cli[mem_idx]->run(tree->race_cli[mem_idx]->search(&key, &ret_value));

                if (ret_value.len == 8 && memcmp(ret_value.data, value.data, 8) != 0) {

                    RACE::Node_Meta ret_node;
                    std::memcpy(&ret_node, ret_value.data, sizeof(uint64_t));
                    log_info << "now_pos:" << now_pos << std::endl;
                    log_info << "[node] "
                            << " len:" << value.len
                            << " now pos: " << node.pos
                            << " type: " << node.type
                            << " fptr" << hex_str(node.fptr) << std::endl;
                    log_info << "[value] "
                            << " len:" << ret_value.len
                            << " now pos: " << ret_node.pos
                            << " type: " << ret_node.type
                            << " fptr" << hex_str(ret_node.fptr) << std::endl;
                }
                // if (ret_value.len == 8) {
                //     RACE::Node_Meta ret_node;
                //     std::memcpy(&ret_node, value.data, sizeof(uint64_t));
                //     if (node.pos != ret_node.pos || node.type != ret_node.type || node.fptr != ret_node.fptr)
                //     {
                //         log_info << "[node] "
                //                 << " len:" << value.len
                //                 << " now pos: " << node.pos
                //                 << " type: " << node.type
                //                 << " fptr" << hex_str(node.fptr) << std::endl;
                //         log_info << "[value] "
                //                 << " len:" << ret_value.len
                //                 << " now pos: " << ret_node.pos
                //                 << " type: " << ret_node.type
                //                 << " fptr" << hex_str(ret_node.fptr) << std::endl;
                //     }
                // }
            }

            for (uint32_t i = 0; i < local_len; ++i) {
                PrheartNode next(
                    tree,
                    node[i].node_type(),
                    node[i].fptr(),
                    now_pos + node[i].length()
                );
                if (!(next_level < 0))
                    num += next.add_shortcut(next_level);
            }
            return num;

        }
    }
}

#else
uint64_t PrheartNode::add_shortcut(uint64_t level) {
    uint64_t num = 0;
    switch (type) {
        case PrheartNodeType::None: {
            return 0;
        }
        case PrheartNodeType::Leaf: {
            return 0;
        }
        default: {
            this->rdma_read_real_data();
            PrheartSlotData node[local_len];
            memcpy((void*)node, (void*)tree->local_start_ptr, local_len * sizeof(PrheartSlotData));
            uint64_t next_level = level;
            // log_info << "now pos: " << now_pos << "level:" << level << std::endl;
            if ((now_pos + 2) > level) {
                num++;
                do {
                    next_level *= 2;
                } while ((now_pos + 2) > next_level);
                RACE::Slice key, value;
                auto prefix = get_prefix(next_level/2-1);
                shortcut_num[next_level/2-1]++;
                RACE::Node_Meta node;
                node.fptr = fptr;
                node.pos = now_pos;
                node.type = static_cast<uint8_t>(type);
                value.len = sizeof(uint64_t);
                value.data = (char*)&node;
                key.len = prefix.size();
                key.data = (char*)prefix.data();

                // log_info << "[insert shorcut] len: " << prefix.size() << " prefix: " << key::to_string(prefix) << std::endl;
                // RACE::Slice ret_key;
                // RACE::Node_Meta ret_node;
                // std::memcpy(&ret_key.data, key.data, sizeof(uint64_t));
                // std::memcpy(&ret_node, value.data, sizeof(uint64_t));
                // log_info << "[key] len: " << key.len << " data: " << key.data << std::endl;
                // log_info << "[node] "
                //         << " now pos: " << node.pos
                //         << " type: " << node.type
                //         << " fptr" << hex_str(node.fptr) << std::endl;
                // log_info << "[value] "
                //         << " now pos: " << ret_node.pos
                //         << " type: " << ret_node.type
                //         << " fptr" << hex_str(ret_node.fptr) << std::endl;
                uint64_t pattern = RACE::hash(key.data, key.len) >> 64;
                uint64_t mem_idx = pattern % tree->memory_machine_num;
                tree->rdma_cli[mem_idx]->run(tree->race_cli[mem_idx]->insert(&key, &value));

                RACE::Slice ret_value;
                char buffer[1024];
                ret_value.data = buffer;
                ret_value.len = 0;
                // log_info << "[Check Correctness]" << std::endl;
                tree->rdma_cli[mem_idx]->run(tree->race_cli[mem_idx]->search(&key, &ret_value));
                // log_info << "[Search Done]" << std::endl;
                // if (ret_value.len != value.len || memcmp(ret_value.data, value.data, value.len) != 0) {
                //     RACE::Node_Meta ret_node;
                //     std::memcpy(&ret_node, value.data, sizeof(uint64_t));
                //     log_info << "[node] "
                //             << " len:" << value.len
                //             << " now pos: " << node.pos
                //             << " type: " << node.type
                //             << " fptr" << hex_str(node.fptr) << std::endl;
                //     log_info << "[value] "
                //             << " len:" << ret_value.len
                //             << " now pos: " << ret_node.pos
                //             << " type: " << ret_node.type
                //             << " fptr" << hex_str(ret_node.fptr) << std::endl;
                // }
            }

            for (uint32_t i = 0; i < local_len; ++i) {
                PrheartNode next(
                    tree,
                    node[i].node_type(),
                    node[i].fptr(),
                    now_pos + node[i].length()
                );
                num += next.add_shortcut(next_level);
            }
            return num;

        }
    }
}
#endif

bool PrheartNode::is_shortcut(uint64_t prev_pos) {
    return 0;
}


// =========================================================================
// Phase 3 — policy-aware skip table construction
// =========================================================================

// -------------------------------------------------------------------------
// PrheartNode::add_shortcut_policy
//
// DFS walk that inserts a RACE skip entry for every inner node whose fptr
// appears in `target_fptrs` (the policy-ranked top-K set computed from the
// AccessTracker after the load phase).
//
// Differences from add_shortcut():
//   • No depth threshold — any tracked node at any depth is eligible.
//   • The membership check is O(1) via the unordered_set.
//   • Always recurses into ALL children (not cut short at a level boundary),
//     because tracked nodes can appear at any depth.
//   • Uses now_pos as the RACE key prefix length, which is the exact number
//     of key bytes consumed to reach this node from the root.
// -------------------------------------------------------------------------
uint64_t PrheartNode::add_shortcut_policy(
    const std::unordered_set<uintptr_t>& target_fptrs
) {
    uint64_t num = 0;

    switch (type) {
        case PrheartNodeType::None:
        case PrheartNodeType::Leaf:
            return 0;

        default: {
            // Read this node's slot array into the thread-local buffer.
            this->rdma_read_real_data();
            PrheartSlotData node_slots[local_len];
            memcpy(
                (void*)node_slots,
                (void*)tree->local_start_ptr,
                local_len * sizeof(PrheartSlotData)
            );

            // Check whether this inner node is in the policy-selected set.
            // now_pos == 0 is the global root; skip it because an empty prefix
            // is not a useful RACE key and the root is always traversed anyway.
            if (now_pos > 0 && target_fptrs.count(fptr)) {

                // Build the key prefix: the first `now_pos` bytes of any key
                // in this node's subtree.  get_prefix() does a DFS to a leaf
                // and slices the key accordingly.
                RACE::Slice key_slice, value_slice;
                span prefix = get_prefix(now_pos);

                // Build the Node_Meta value stored in RACE:
                //   fptr → base address of this ART node (the jump target)
                //   pos  → now_pos (byte depth to restart traversal from)
                //   type → PrheartNodeType (to know the node size on lookup)
                RACE::Node_Meta meta;
                meta.fptr = fptr;
                meta.pos  = static_cast<uint8_t>(now_pos);
                meta.type = static_cast<uint8_t>(type);

                value_slice.len  = sizeof(uint64_t);
                value_slice.data = reinterpret_cast<char*>(&meta);
                key_slice.len    = prefix.size();
                key_slice.data   = reinterpret_cast<char*>(
                                       const_cast<uint8_t*>(prefix.data()));

                // Hash the prefix to decide which memory node holds its RACE
                // bucket (same sharding as get_root(key) uses at lookup time).
                uint64_t pattern = RACE::hash(key_slice.data, key_slice.len) >> 64;
                uint64_t mem_idx = pattern % tree->memory_machine_num;
                tree->rdma_cli[mem_idx]->run(
                    tree->race_cli[mem_idx]->insert(&key_slice, &value_slice)
                );

                shortcut_num[now_pos]++;
                num++;
            }

            // Recurse into every child slot regardless of whether we inserted
            // an entry for this node.  Target nodes can appear at any depth.
            for (uint32_t i = 0; i < local_len; ++i) {
                if (node_slots[i].node_type() == PrheartNodeType::None) continue;
                PrheartNode next(
                    tree,
                    node_slots[i].node_type(),
                    node_slots[i].fptr(),
                    now_pos + node_slots[i].length()
                );
                num += next.add_shortcut_policy(target_fptrs);
            }
            return num;
        }
    }
}

// -------------------------------------------------------------------------
// PrheartTree::create_skip_table_policy
//
// Entry point called from compute.cc when a non-static policy is active.
// Flow:
//   1. Snapshot the top `max_entries` nodes from AccessTracker by `policy`.
//   2. Walk the entire ART via add_shortcut_policy, inserting a RACE entry
//      for each tracked node found during the walk.
//   3. Return the number of entries inserted.
//
// The tracker must have been populated during the load phase
// (ENABLE_ACCESS_TRACKING must be defined).
// -------------------------------------------------------------------------
uint64_t PrheartTree::create_skip_table_policy(CachePolicy policy, uint64_t max_entries) {

    if (race_cli.empty()) {
        log_error << "[create_skip_table_policy] No RACE clients configured. "
                  << "Build with #define SKIP_TABLE." << std::endl;
        return 0;
    }

    // Snapshot the top-k nodes from the access tracker.
    auto target_fptrs = AccessTracker::instance().get_top_k_set(max_entries, policy);

    const char* policy_names[] = {"HOTNESS", "CRITICALITY", "HYBRID"};
    log_info << "[create_skip_table_policy] policy="
             << policy_names[static_cast<int>(policy)]
             << "  max_entries=" << max_entries
             << "  tracked_candidates=" << target_fptrs.size() << std::endl;

    if (target_fptrs.empty()) {
        log_warn << "[create_skip_table_policy] AccessTracker has no data. "
                 << "Ensure ENABLE_ACCESS_TRACKING is defined and the load "
                 << "phase ran before this call." << std::endl;
        return 0;
    }

    // Reset per-level shortcut counters (same as the static version does).
    for (uint64_t i = 0; i < MAX_LEVEL; ++i)
        shortcut_num[i] = 0;

    uint64_t inserted = get_root().add_shortcut_policy(target_fptrs);

    log_info << "[create_skip_table_policy] inserted " << inserted
             << " entries into RACE." << std::endl;
    for (uint64_t i = 0; i < MAX_LEVEL; ++i) {
        if (shortcut_num[i] > 0)
            log_info << "  [depth " << i << "] shortcuts inserted: "
                     << shortcut_num[i] << std::endl;
    }
    return inserted;
}

} // namespace prheart