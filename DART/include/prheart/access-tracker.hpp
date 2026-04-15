#pragma once
// access-tracker.hpp
//
// Per-node RDMA access frequency and traversal-depth tracking for the
// DART-based index.  Used to implement performance-criticality-aware
// caching policies for the RACE skip table (Phase 2 / Phase 3).
//
// --- Two core metrics ---
//
//   Hotness     = raw RDMA read count for a node.
//                 High hotness  → the node is fetched often.
//
//   Criticality = sum of traversal depths (now_pos) at every read.
//                 = total RTT savings if this node were a skip target.
//                 High criticality → the node sits deep on many lookup paths;
//                 making it a skip target saves the most network round trips.
//
// --- The key distinction (mirrors tiered-memory research) ---
//
//   A node with high hotness but low criticality is shallow and visited
//   frequently, but it may already benefit from being near the root and
//   does not save many RTTs per lookup when used as a skip entry.
//
//   A node with high criticality but moderate hotness is accessed deep in
//   the traversal chain; every lookup through it incurs many RTTs, so
//   caching it as a skip target yields large per-operation savings.
//
//   Comparing which top-K set each policy selects — and measuring throughput
//   under each — is the central experiment of the project.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace prheart {

// -----------------------------------------------------------------------
// CachePolicy: ranking criterion for skip-table slot assignment
// -----------------------------------------------------------------------
enum class CachePolicy : uint8_t {
    HOTNESS      = 0,   // rank by raw read frequency
    CRITICALITY  = 1,   // rank by cumulative traversal depth saved (depth_sum)
    HYBRID       = 2,   // alpha*hotness + (1-alpha)*criticality  (alpha = 0.5)
};

// -----------------------------------------------------------------------
// NodeAccessRecord
// One record per distinct inner-node fptr observed at runtime.
// All counters are atomics: worker threads update them without a lock;
// the shard mutex is only held during map insertion/lookup.
// -----------------------------------------------------------------------
struct NodeAccessRecord {

    // How many RDMA READs were issued to this node during the benchmark.
    std::atomic<uint64_t> read_count{0};

    // Sum of now_pos values at every RDMA READ.
    //   depth_sum = Σ now_pos_i  over all reads
    // If this node were a skip target, each lookup would save `now_pos` RTTs
    // (those are the levels above it that would be skipped).
    // Therefore depth_sum equals the total RTT savings potential of this node
    // across all recorded operations.
    std::atomic<uint64_t> depth_sum{0};

    // How many RDMA WRITEs / node upgrades touched this node.
    std::atomic<uint64_t> write_count{0};

    // PrheartNodeType cast to uint8_t (informational only).
    std::atomic<uint8_t>  node_type{0};

    // --- Scoring ---

    // Raw access frequency.
    double hotness_score() const noexcept {
        return static_cast<double>(read_count.load(std::memory_order_relaxed));
    }

    // Total RTT savings potential.
    // Favours nodes that are both deep and frequently accessed.
    double criticality_score() const noexcept {
        return static_cast<double>(depth_sum.load(std::memory_order_relaxed));
    }

    // Weighted combination.  alpha=1 → pure hotness; alpha=0 → pure criticality.
    double hybrid_score(double alpha = 0.5) const noexcept {
        return alpha * hotness_score() + (1.0 - alpha) * criticality_score();
    }

    double score(CachePolicy policy, double alpha = 0.5) const noexcept {
        switch (policy) {
            case CachePolicy::HOTNESS:     return hotness_score();
            case CachePolicy::CRITICALITY: return criticality_score();
            case CachePolicy::HYBRID:      return hybrid_score(alpha);
        }
        return hotness_score();
    }
};

// -----------------------------------------------------------------------
// AccessTracker — global singleton
//
// Uses NUM_SHARDS sharded mutexes to distribute lock contention across
// the many worker threads that call record_read() on every RDMA operation.
// Each shard owns (fptr % NUM_SHARDS) → its mutex protects 1/256 of all
// node records, so contention is proportionally reduced.
// -----------------------------------------------------------------------
class AccessTracker {
public:
    static constexpr uint32_t NUM_SHARDS = 256;

    static AccessTracker& instance() {
        static AccessTracker inst;
        return inst;
    }

    // Called from rdma_read_real_data() for every inner-node RDMA READ.
    //   fptr             — base address (fake pointer) of the node being read
    //   traversal_depth  — now_pos of the PrheartNode at access time
    //   node_type        — PrheartNodeType as uint8_t
    void record_read(uintptr_t fptr, uint32_t traversal_depth, uint8_t node_type);

    // Called from rdma_write_real_data() for every inner-node RDMA WRITE.
    void record_write(uintptr_t fptr, uint32_t traversal_depth, uint8_t node_type);

    // Return the top-k (fptr, score) pairs in descending score order.
    // k=0 returns all tracked nodes.
    std::vector<std::pair<uintptr_t, double>> get_top_k(
        uint64_t k, CachePolicy policy, double alpha = 0.5
    ) const;

    // Convenience: build a fast-lookup set of the top-k fptrs.
    // Used by add_shortcut_policy() to check membership in O(1).
    std::unordered_set<uintptr_t> get_top_k_set(
        uint64_t k, CachePolicy policy, double alpha = 0.5
    ) const;

    // Number of distinct inner nodes observed so far.
    uint64_t total_nodes_tracked() const;

    // Print a ranked summary of the top `top_k` nodes.
    void print_summary(uint64_t top_k = 20,
                       CachePolicy policy = CachePolicy::HOTNESS) const;

    // Side-by-side comparison of HOTNESS vs CRITICALITY top-k sets.
    // Reports the overlap percentage and lists "divergent" nodes that appear
    // in one ranking but not the other — these represent cases where hotness
    // and performance criticality disagree, which is the central question of
    // the project.
    void print_policy_comparison(uint64_t k = 100) const;

    // Clear all records.  Call between the load and run phases so that
    // run-phase tracking starts from a clean state.
    void reset();

    AccessTracker(const AccessTracker&)            = delete;
    AccessTracker& operator=(const AccessTracker&) = delete;

private:
    AccessTracker() = default;

    struct Shard {
        mutable std::mutex mtx;
        std::unordered_map<uintptr_t, NodeAccessRecord> records;
    };
    Shard shards_[NUM_SHARDS];

    Shard& shard_of(uintptr_t fptr) {
        return shards_[fptr % NUM_SHARDS];
    }
    const Shard& shard_of(uintptr_t fptr) const {
        return shards_[fptr % NUM_SHARDS];
    }
};

} // namespace prheart
