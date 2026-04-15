#include "prheart/access-tracker.hpp"
#include "log/log.hpp"
#include "measure/measure.hpp"   // hex_str(), readable_byte()

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace prheart {

// -----------------------------------------------------------------------
// record_read
//
// Called on every RDMA READ of an inner ART node.
// The lock is held only for the map emplace/lookup; the counter increments
// are atomic and happen while the lock is already released in the common
// case (the node already exists in the map).
//
// Why track `traversal_depth` (= now_pos)?
//   now_pos is the byte offset into the key at which this node sits.
//   A node at now_pos=8 means 8 bytes of the key have been consumed by
//   traversal above it.  If this node were a skip-table entry, every
//   lookup would jump directly here, saving those 8 RTTs.  Accumulating
//   this value across all reads gives depth_sum = total RTT savings potential.
// -----------------------------------------------------------------------
void AccessTracker::record_read(
    uintptr_t fptr, uint32_t traversal_depth, uint8_t node_type
) {
    auto& sh = shard_of(fptr);
    std::lock_guard<std::mutex> lk(sh.mtx);
    // emplace is a no-op if the key already exists; it returns an iterator
    // to the existing record in that case, which is exactly what we want.
    auto& rec = sh.records[fptr];
    rec.read_count.fetch_add(1, std::memory_order_relaxed);
    rec.depth_sum.fetch_add(traversal_depth, std::memory_order_relaxed);
    rec.node_type.store(node_type, std::memory_order_relaxed);
}

// -----------------------------------------------------------------------
// record_write
//
// Called on every RDMA WRITE to an inner ART node (upgrades, new nodes).
// Write count is informational: high write counts signal a node that is
// frequently split/upgraded under insert pressure.
// -----------------------------------------------------------------------
void AccessTracker::record_write(
    uintptr_t fptr, uint32_t traversal_depth, uint8_t node_type
) {
    auto& sh = shard_of(fptr);
    std::lock_guard<std::mutex> lk(sh.mtx);
    auto& rec = sh.records[fptr];
    rec.write_count.fetch_add(1, std::memory_order_relaxed);
    rec.node_type.store(node_type, std::memory_order_relaxed);
    (void)traversal_depth;   // retained for API symmetry; not used for writes
}

// -----------------------------------------------------------------------
// get_top_k
//
// Collects all (fptr, score) pairs across every shard, sorts descending,
// and trims to k entries.  k=0 means return all entries.
// This is a snapshot: it acquires each shard's lock briefly while copying.
// -----------------------------------------------------------------------
std::vector<std::pair<uintptr_t, double>> AccessTracker::get_top_k(
    uint64_t k, CachePolicy policy, double alpha
) const {
    std::vector<std::pair<uintptr_t, double>> all;
    all.reserve(4096);

    for (uint32_t s = 0; s < NUM_SHARDS; ++s) {
        std::lock_guard<std::mutex> lk(shards_[s].mtx);
        for (const auto& [fptr, rec] : shards_[s].records) {
            // Only include nodes that were actually read (not just written).
            if (rec.read_count.load(std::memory_order_relaxed) > 0)
                all.emplace_back(fptr, rec.score(policy, alpha));
        }
    }

    // Sort descending by score so top-k are at the front.
    std::sort(all.begin(), all.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (k > 0 && all.size() > static_cast<size_t>(k))
        all.resize(static_cast<size_t>(k));

    return all;
}

// -----------------------------------------------------------------------
// get_top_k_set
//
// Same as get_top_k but returns an unordered_set for O(1) membership
// testing inside add_shortcut_policy().
// -----------------------------------------------------------------------
std::unordered_set<uintptr_t> AccessTracker::get_top_k_set(
    uint64_t k, CachePolicy policy, double alpha
) const {
    auto ranked = get_top_k(k, policy, alpha);
    std::unordered_set<uintptr_t> result;
    result.reserve(ranked.size());
    for (auto& [fptr, _] : ranked)
        result.insert(fptr);
    return result;
}

// -----------------------------------------------------------------------
// total_nodes_tracked
// -----------------------------------------------------------------------
uint64_t AccessTracker::total_nodes_tracked() const {
    uint64_t total = 0;
    for (uint32_t s = 0; s < NUM_SHARDS; ++s) {
        std::lock_guard<std::mutex> lk(shards_[s].mtx);
        total += shards_[s].records.size();
    }
    return total;
}

// -----------------------------------------------------------------------
// print_summary
//
// Prints the top `top_k` nodes ranked by `policy` with all three scores
// so the user can see hotness vs. criticality side by side.
// -----------------------------------------------------------------------
void AccessTracker::print_summary(uint64_t top_k, CachePolicy policy) const {
    const char* names[] = {"HOTNESS", "CRITICALITY", "HYBRID"};
    log_purple << "=== AccessTracker Summary  (policy="
               << names[static_cast<int>(policy)] << ") ===" << std::endl;
    log_info   << "  Distinct inner nodes tracked: "
               << total_nodes_tracked() << std::endl;

    auto top = get_top_k(top_k, policy);
    if (top.empty()) {
        log_warn << "  No reads recorded.  "
                 << "Set #define ENABLE_ACCESS_TRACKING in art-node.cc." << std::endl;
        return;
    }

    // Header
    log_purple
        << "  Rank | fptr               | reads    | depth_sum  | writes | hotness | criticality"
        << std::endl;
    log_purple
        << "  -----|--------------------|---------|-----------|---------|---------|-----------"
        << std::endl;

    for (size_t i = 0; i < top.size(); ++i) {
        uintptr_t fptr = top[i].first;

        // Re-acquire shard lock to read all fields together.
        const auto& sh = shard_of(fptr);
        std::lock_guard<std::mutex> lk(sh.mtx);
        auto it = sh.records.find(fptr);
        if (it == sh.records.end()) continue;
        const auto& rec = it->second;

        log_info << "  " << std::setw(4) << (i + 1)
                 << " | " << hex_str(fptr)
                 << " | " << std::setw(8)  << rec.read_count.load()
                 << " | " << std::setw(10) << rec.depth_sum.load()
                 << " | " << std::setw(6)  << rec.write_count.load()
                 << " | " << std::setw(7)  << std::fixed << std::setprecision(0)
                                           << rec.hotness_score()
                 << " | " << std::setw(11) << std::fixed << std::setprecision(0)
                                           << rec.criticality_score()
                 << std::endl;
    }
}

// -----------------------------------------------------------------------
// print_policy_comparison
//
// Compares the top-k HOTNESS set against the top-k CRITICALITY set and
// reports:
//   - Overlap percentage  (nodes ranked highly by both)
//   - HOTNESS-only nodes  (frequent but shallow → saves few RTTs each)
//   - CRITICALITY-only    (deep but less frequent → saves many RTTs each)
//
// A large divergence (low overlap) is the empirical signature of the
// hotness-vs-criticality gap described in the project proposal and the
// referenced tiered-memory papers.
// -----------------------------------------------------------------------
void AccessTracker::print_policy_comparison(uint64_t k) const {
    auto hot_ranked  = get_top_k(k, CachePolicy::HOTNESS);
    auto crit_ranked = get_top_k(k, CachePolicy::CRITICALITY);

    std::unordered_set<uintptr_t> hot_set, crit_set;
    for (auto& [f, _] : hot_ranked)  hot_set.insert(f);
    for (auto& [f, _] : crit_ranked) crit_set.insert(f);

    uint64_t overlap = 0;
    for (auto fptr : hot_set)
        if (crit_set.count(fptr)) ++overlap;

    uint64_t hot_only  = static_cast<uint64_t>(hot_set.size())  - overlap;
    uint64_t crit_only = static_cast<uint64_t>(crit_set.size()) - overlap;
    double   pct       = hot_set.empty()
                           ? 0.0
                           : 100.0 * overlap / hot_set.size();

    log_purple << "=== Policy Comparison  (top-" << k << ") ===" << std::endl;
    log_info   << "  HOTNESS top-"     << k << ": " << hot_set.size()  << " nodes" << std::endl;
    log_info   << "  CRITICALITY top-" << k << ": " << crit_set.size() << " nodes" << std::endl;
    log_info   << "  Overlap:           " << overlap
               << " nodes (" << std::fixed << std::setprecision(1) << pct << "%)" << std::endl;
    log_info   << "  HOTNESS-only  (frequent but shallow — already near root): "
               << hot_only  << std::endl;
    log_info   << "  CRITICALITY-only (deep critical path — high RTT savings): "
               << crit_only << std::endl;

    if (hot_only + crit_only > 0) {
        log_warn << "  => Divergence detected.  "
                 << "CRITICALITY policy may reduce latency for these "
                 << crit_only << " deep nodes vs. HOTNESS policy." << std::endl;
    } else {
        log_info << "  => Full overlap: hotness and criticality agree on the top-"
                 << k << " nodes." << std::endl;
    }
}

// -----------------------------------------------------------------------
// reset
//
// Called after the load phase and before the run phase so that runtime
// access tracking is not polluted by structural reads done during load
// or during skip table construction.
// -----------------------------------------------------------------------
void AccessTracker::reset() {
    for (uint32_t s = 0; s < NUM_SHARDS; ++s) {
        std::lock_guard<std::mutex> lk(shards_[s].mtx);
        shards_[s].records.clear();
    }
    log_info << "[AccessTracker] reset — tracking from clean state." << std::endl;
}

} // namespace prheart
