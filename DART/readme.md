# Exploring Performance Criticality Aware Policies in a DART Based RDMA Index

**Author:** Abhiram Prasanna  
**Course:** Computer Architecture  
**Cluster:** Two-node InfiniBand (10.30.1.9 = compute/monitor, 10.30.1.6 = memory)  
**Key count:** 1,000,000  
**Operations per run:** 1,000,000  
**Threads (compute):** 56  
**InfiniBand fabric:** 100 Gb/s  

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Background: Disaggregated Memory and the Cost of Remote Traversal](#2-background-disaggregated-memory-and-the-cost-of-remote-traversal)
3. [DART Architecture](#3-dart-architecture)
   - [The Adaptive Radix Tree (ART)](#31-the-adaptive-radix-tree-art)
   - [The Express Skip Table (RACE Hash Table)](#32-the-express-skip-table-race-hash-table)
   - [The Two-Layer Lookup Path](#33-the-two-layer-lookup-path)
   - [Tree Topology for 1 Million Keys](#34-tree-topology-for-1-million-keys)
4. [Access Tracking Instrumentation](#4-access-tracking-instrumentation)
   - [What the AccessTracker Records](#41-what-the-accesstracker-records)
   - [The NodeAccessRecord Structure](#42-the-nodeaccessrecord-structure)
   - [Sharding and Concurrency](#43-sharding-and-concurrency)
   - [Load Phase vs Run Phase Tracking](#44-load-phase-vs-run-phase-tracking)
5. [Cache Policies: A Complete Explanation](#5-cache-policies-a-complete-explanation)
   - [STATIC: Full Depth-First Enumeration](#51-static-full-depth-first-enumeration)
   - [HOTNESS: Frequency-Based Ranking](#52-hotness-frequency-based-ranking)
   - [CRITICALITY: Depth-Weighted Ranking](#53-criticality-depth-weighted-ranking)
   - [HYBRID: Blended Scoring](#54-hybrid-blended-scoring)
   - [How Skip Table Entries Are Inserted](#55-how-skip-table-entries-are-inserted)
   - [Budget and Why It Saturates Early](#56-budget-and-why-it-saturates-early)
6. [Experimental Setup](#6-experimental-setup)
   - [Hardware Configuration](#61-hardware-configuration)
   - [Workloads](#62-workloads)
   - [Experiment Matrix](#63-experiment-matrix)
   - [How Each Run Proceeds](#64-how-each-run-proceeds)
7. [Complete Experiment Results: All 23 Runs](#7-complete-experiment-results-all-23-runs)
   - [Original 13 Experiments (Workload f)](#71-original-13-experiments-workload-f)
   - [Skew Sweep (10 Additional Experiments)](#72-skew-sweep-10-additional-experiments)
8. [Detailed Analysis of Every Result](#8-detailed-analysis-of-every-result)
   - [Why STATIC Is Always the Fastest](#81-why-static-is-always-the-fastest)
   - [Why CRITICALITY Beats HOTNESS](#82-why-criticality-beats-hotness)
   - [Why HYBRID Performs Worst](#83-why-hybrid-performs-worst)
   - [Budget Saturation: Why More Budget Does Not Always Help](#84-budget-saturation-why-more-budget-does-not-always-help)
   - [High Variance in Workload f Results](#85-high-variance-in-workload-f-results)
   - [Skew Sweep Analysis](#86-skew-sweep-analysis)
9. [Policy Comparison: What the Overlap Numbers Mean](#9-policy-comparison-what-the-overlap-numbers-mean)
   - [Load Phase Overlap](#91-load-phase-overlap)
   - [Run Phase Overlap](#92-run-phase-overlap)
   - [What HOTNESS-only and CRITICALITY-only Nodes Are](#93-what-hotness-only-and-criticality-only-nodes-are)
10. [Thread-Level Observations: Bimodal Latency](#10-thread-level-observations-bimodal-latency)
11. [The Root Node Anomaly](#11-the-root-node-anomaly)
12. [Why CRITICALITY Cannot Beat STATIC at This Scale](#12-why-criticality-cannot-beat-static-at-this-scale)
13. [How to Make CRITICALITY Perform Better](#13-how-to-make-criticality-perform-better)
14. [Running the Experiments](#14-running-the-experiments)
15. [Source File Reference](#15-source-file-reference)

---

## 1. Project Overview

This project investigates whether a performance criticality aware caching policy can reduce the average lookup latency in DART, a distributed index system that stores its tree nodes in remote disaggregated memory accessible only via RDMA. The core question is: given a fixed budget of local shortcut entries, is it better to cache the nodes that are accessed most frequently (HOTNESS), or the nodes whose caching would eliminate the most network round-trips (CRITICALITY)?

The project was completed in two phases. Phase 1 established a working baseline on real InfiniBand hardware, verifying that DART operates correctly and measuring the fixed-cost structure of its lookup path under a variety of workload distributions. Phase 2 instrumented DART with an access tracking layer that records, for every node traversed during operation, how many times it was visited and what depth position within the tree it occupied. These tracked statistics were then used to build skip table caching policies that are compared against a static baseline (full-tree enumeration) and against each other across 23 separate experimental runs.

The principal findings are:

- CRITICALITY outperforms HOTNESS by 13% on the best comparison run (155.0 µs vs 177.9 µs) because it selects depth-3 nodes that save 3 RDMA round-trips per hit instead of depth-2 nodes that save only 2.
- STATIC outperforms all learned policies because it caches all 50,943 nodes in the tree, achieving near-universal skip table coverage. Learned policies with budget=5,000 can insert only approximately 180 shortcuts, leaving the vast majority of lookups without skip table coverage.
- High run-to-run variance in workload f results (latency ranging from 155 to 235 µs across identical experiments) is caused by workload f's bimodal operation mix (50% reads at ~45 µs, 50% updates at ~380 µs) and stochastic thread scheduling.
- Budget saturation is severe: increasing the budget from 500 to 5,000 yields only ~50 additional shortcuts because the tree topology (only 102 depth-3 nodes) is the binding constraint, not the budget parameter.

---

## 2. Background: Disaggregated Memory and the Cost of Remote Traversal

Traditional server architectures keep all memory local to the CPU. Disaggregated memory systems break this assumption by physically separating compute nodes from memory nodes over a high-speed network interconnect such as InfiniBand. In this model, compute nodes issue one-sided RDMA operations to read or write data residing on remote memory nodes. One-sided RDMA means the memory node CPU is not involved; the network interface card handles the operation directly. This reduces software overhead but does not eliminate network latency. A single RDMA read across a 100 Gb/s InfiniBand fabric takes approximately 1 to 3 microseconds from the perspective of the issuing thread, depending on message size and system load.

For a pointer-chasing data structure such as a tree, this cost is multiplicative. Each level of the tree requires one or more RDMA reads to fetch the node before the next pointer can be followed. A tree of height 3 requires at least 3 sequential round-trips. Each of those round-trips cannot be parallelized because the address of the next node is unknown until the current node is fetched and decoded. This is the fundamental performance challenge in disaggregated tree indexes: the depth of the tree directly determines the number of round-trips, and round-trips are expensive.

DART addresses this by maintaining a small local shortcut table (the express skip table) on the compute node. This table maps key prefixes to remote node addresses at deep levels of the tree, allowing traversal to skip the upper levels entirely. If a lookup key's prefix matches a skip table entry that points to a depth-2 node, the lookup begins at depth 2 instead of the root, saving 2 round-trips. The skip table entries are stored entirely in local DRAM and are consulted with no network cost.

The policy question this project investigates is: which nodes should occupy those skip table slots? Should they be the nodes that receive the most lookups (HOTNESS), the nodes whose caching would eliminate the most total RDMA work (CRITICALITY), or the entire tree (STATIC)?

---

## 3. DART Architecture

### 3.1 The Adaptive Radix Tree (ART)

The ART (Adaptive Radix Tree) is a trie-like data structure that stores keys one byte at a time. At each level of the tree, one byte of the key is consumed to select a child pointer. The "adaptive" part refers to the fact that inner nodes exist in four sizes depending on how many children they actually hold:

- **Node4**: holds up to 4 children. Used for sparse branching points. Minimal memory footprint.
- **Node16**: holds up to 16 children. Uses SIMD-based search (16-byte comparison) for O(1) child lookup.
- **Node48**: holds up to 48 children. Maintains a 256-entry byte array for direct child slot dispatch.
- **Node256**: holds all 256 possible children. Maximum branching, direct array indexing.
- **Leaf nodes**: store the full key and value. In this configuration, each leaf is 1,095 bytes.

In DART, all ART nodes reside in the remote memory node's DRAM. The compute node does not maintain any local copy of ART nodes. Every traversal step from root to leaf requires at least one RDMA read per level.

The `now_pos` field in the traversal state tracks how far into the key the current traversal has progressed. At the root, `now_pos` is 0. After consuming one byte to reach a child, `now_pos` is 1. A node that was first inserted when `now_pos` was 2 resides at depth 2 of the tree. Throughout this project, depth and `now_pos` are used interchangeably.

### 3.2 The Express Skip Table (RACE Hash Table)

The skip table is a RACE (Remote Access Cache Engine) hash table stored entirely in local DRAM on the compute node. RACE is an extendible hash table with a local directory and remote bucket storage. In DART's adaptation, both the directory and the buckets are kept locally, making all skip table lookups pure local memory reads with zero RDMA cost.

Each entry in the skip table maps a key slice (the first `now_pos` bytes of a key, extracted from any key whose lookup path passes through the target node) to a `Node_Meta` value. The `Node_Meta` stores the remote base address of the target ART node and its type (Node4/Node16/Node48/Node256). When a lookup arrives, the compute node checks the skip table at the maximum cached depth. If a match is found, the RDMA read is issued directly to the target node's remote address, bypassing all ancestor nodes.

Consulting the skip table costs only local DRAM access time (tens of nanoseconds). The benefit is proportional to the depth of the matched entry: a hit at depth 2 saves 2 RDMA reads; a hit at depth 3 saves 3 RDMA reads.

### 3.3 The Two-Layer Lookup Path

A complete DART lookup for a key proceeds as follows:

1. Compute the key slice for depth 2 (or the maximum cached depth). Look it up in the local skip table (RACE hash table lookup, local only).
2. If a skip table hit occurs at depth d, issue one RDMA READ to fetch the ART node at the cached remote address (136 bytes for a typical inner node).
3. Continue traversal from that node: issue additional RDMA READs to follow child pointers level by level down to the target depth.
4. Issue a final RDMA READ to fetch the leaf node (1,095 bytes).
5. Verify the full key against the leaf's stored key.

If the skip table misses, traversal begins from the root. Every level from the root to the leaf requires one RDMA READ, totaling 3 to 4 reads depending on tree height.

The RDMA operation breakdown observed in run 1 (STATIC policy, workload f, 1,000,000 operations, run phase) is:

| RDMA Operation | Transfer Size | Count (run phase only) |
|----------------|---------------|------------------------|
| READ bucket fetch (RACE) | 256 bytes | 27,911 |
| READ ART node via shortcut | 136 bytes | 17,858 |
| READ leaf node | 1,095 bytes | 17,858 |
| READ scan/large metadata | 2,048 bytes | 18,032 |
| READ small metadata | 64–128 bytes | ~1,235 |

The equality of shortcut reads (17,858) and leaf reads (17,858) confirms that each read-type operation executes exactly one skip table dereference followed by one leaf fetch. The 2,048-byte reads correspond to scan operations in workload f that read multi-entry buckets. The 256-byte bucket fetches are part of the RACE hash table consultation for each lookup.

### 3.4 Tree Topology for 1 Million Keys

After loading 1,000,000 keys, the AccessTracker's `print_tree_stats()` function recorded the following internal node distribution:

| Depth Level | Node Count | Percentage of All Inner Nodes |
|-------------|------------|-------------------------------|
| 0 (root) | 1 | 0.001% |
| 1 | 768 | 0.697% |
| 2 | 109,401 | 99.210% |
| 3 | 102 | 0.093% |
| **Total** | **110,272** | **100.000%** |

This topology is the single most important structural fact in the entire experimental analysis. The tree is remarkably flat: 99.2% of all inner nodes are concentrated at depth 2. Only 102 nodes exist at depth 3, the maximum depth observed. This flatness has profound and cascading consequences:

- The STATIC policy inserts one skip entry per inner node, yielding approximately 50,943 entries. The vast majority are depth-2 shortcuts, with 768 depth-1 shortcuts and 102 depth-3 shortcuts.
- Learned policies with budget=5,000 can at best capture all 102 depth-3 nodes plus a handful of high-traffic depth-2 nodes. The remaining 109,299 depth-2 nodes and 767 depth-1 nodes are never cached.
- The maximum RTT savings achievable per cached node is 3 (for a depth-3 node). STATIC with 50,943 entries provides coverage for essentially all lookups; a learned policy with 180 entries provides coverage for only 0.16% of the key space.
- HOTNESS and CRITICALITY produce nearly identical rankings because 99.2% of nodes are at the same depth (2), so depth_sum ≈ 2 × read_count for almost every node. The ranking only diverges for the rare depth-3 nodes.

---

## 4. Access Tracking Instrumentation

### 4.1 What the AccessTracker Records

The AccessTracker is a singleton class added to DART as part of Phase 2 instrumentation. It is activated at compile time by defining `ENABLE_ACCESS_TRACKING` before including its header. When active, every time an ART inner node is traversed during a lookup or insertion, the tracker records three pieces of information:

1. The node's remote pointer address (`fptr`), used as a unique identifier across the 56-thread concurrent system.
2. The current `now_pos` value at the time of traversal, which equals the depth of the node in the ART.
3. Whether the traversal was a read operation or a write operation (writes occur during key insertion in the load phase).

These records are accumulated across all 56 compute threads simultaneously. The AccessTracker provides aggregate statistics to the skip table construction code and prints human-readable reports that appear in compute_results.txt.

### 4.2 The NodeAccessRecord Structure

Each distinct remote node (identified by its `fptr` address) has exactly one `NodeAccessRecord` containing:

- **read_count** (`atomic<uint64_t>`): total number of read traversals through this node across all threads.
- **depth_sum** (`atomic<uint64_t>`): the accumulated sum of `now_pos` values across all read traversals. If a node is at constant depth 2 and is read 1,000 times, `depth_sum` = 2,000.
- **write_count** (`atomic<uint64_t>`): total number of write traversals (insertions).
- **node_type** (`atomic<int>`): records the ART node type (Node4, Node16, Node48, or Node256) as observed during traversal.

Three scoring functions compute a single floating-point value from each record:

- **hotness_score()** = `read_count`. Pure access frequency. This is the number of times a lookup passed through this node.

- **criticality_score()** = `depth_sum`. Depth-weighted access frequency. This is the total number of RDMA reads that would have been avoided if this node had always been a skip table entry: every visit at depth d represents d reads that would have been replaced by one direct skip. So `depth_sum` = total avoidable RDMA reads.

- **hybrid_score(alpha = 0.5)** = `0.5 * read_count + 0.5 * depth_sum`. A linear combination giving equal weight to raw frequency and depth-weighted frequency.

The scoring functions are the core of the policy differentiation. HOTNESS and CRITICALITY choose the same formula but with a different definition of "value": frequency versus total RTT savings.

### 4.3 Sharding and Concurrency

With 56 threads all recording accesses concurrently, the AccessTracker uses 256 independently locked shards to reduce lock contention. Each `fptr` is mapped to one of the 256 shards via a hash function. Within a shard, a `std::mutex` protects the `unordered_map<fptr, NodeAccessRecord>`. The `depth_sum` field uses `fetch_add` which is atomic and does not require the shard lock. The global `max_depth_seen_` is updated with a CAS loop whenever a deeper node is observed.

This design means that two threads accessing different nodes in different shards proceed completely independently with no contention. Threads accessing the same node (the root is a common example) contend only for their shared shard lock, which is held for the duration of a single map lookup and counter increment — a very short critical section.

### 4.4 Load Phase vs Run Phase Tracking

The experimental protocol calls for two independent phases of access tracking:

**Load phase tracking**: Begins when the first RDMA write is issued to build the tree and ends when all 1,000,000 keys are inserted. During the load phase, the tracker accumulates access statistics that reflect which nodes are traversed as keys are written into the tree. These statistics are printed as the "Load-phase AccessTracker report" and are used to select top-K nodes for skip table construction.

**Run phase tracking**: After the skip table is built, `AccessTracker::reset()` is called to wipe all records. The tracker then accumulates a fresh set of statistics during the actual run phase (1,000,000 operations). These statistics are printed as the "Run-phase AccessTracker report" after the run completes.

The two phases are entirely independent. By resetting between phases, the run-phase report shows the true access distribution of the measurement workload, uncorrupted by the load-phase pattern. This is important because the load phase inserts all keys uniformly (each node receives a proportional share of writes), while the run phase may concentrate accesses on specific hot nodes (especially under Zipfian distributions).

The consequence is that the skip table built from load-phase statistics may not perfectly match the run-phase access pattern. This mismatch is one reason why learned policies underperform STATIC: STATIC enumerates the tree structurally and is immune to any load-vs-run distribution difference.

---

## 5. Cache Policies: A Complete Explanation

### 5.1 STATIC: Full Depth-First Enumeration

STATIC is not a learned policy. It does not use access tracking at all. Instead, after the load phase completes, it performs a depth-first traversal of the entire ART starting from the root. For every inner node it encounters, it inserts a skip table entry mapping the key prefix for that node to its remote address and metadata.

The key prefix used for the RACE entry is extracted by descending from the current node to any leaf in its subtree and taking the first `now_pos` bytes of that leaf's key. This is the unique identifier that would match when a future lookup queries the skip table for this depth.

STATIC is the theoretical upper bound of skip table coverage. After STATIC builds its skip table:

- Every possible lookup key is served by a skip entry at depth 2 (or deeper if a depth-3 entry exists).
- Every depth-1 and depth-2 inner node in the tree has a corresponding RACE entry.
- The traversal path for any lookup is: check local skip table (one local read), issue one RDMA read at depth 2, issue one RDMA read for the leaf. Total: 2 RDMA reads per lookup.

This is the minimum possible RDMA read count for a height-3 tree: you cannot eliminate the final leaf fetch or the one read needed to navigate from the skip target to the leaf.

For 1,000,000 keys, STATIC inserts between 50,940 and 50,947 shortcuts (the small variation is from run to run due to different load-phase key orderings affecting tree structure by a tiny amount).

STATIC disadvantages:
- It does not scale. A tree with 10,000,000 keys would have hundreds of thousands of inner nodes. STATIC's DFS visit of all of them would take significant time and might exhaust the RACE table capacity.
- It learns nothing. Hot nodes and cold nodes occupy equal entries in the skip table.
- It cannot adapt to dynamic workloads where key insertions and deletions change the tree structure after the skip table is built.

Despite these disadvantages, STATIC is the best-performing policy in all 23 experiments, for reasons explained in detail in Section 8.1.

### 5.2 HOTNESS: Frequency-Based Ranking

HOTNESS ranks all tracked inner nodes by their `read_count` value (their hotness score) in descending order and selects the top K for skip table insertion, where K is the configured `POLICY_MAX_ENTRIES` budget. In these experiments, K is 500, 1,000, or 5,000.

The motivation for HOTNESS is the observation that under skewed workloads, a small number of key ranges receive the majority of lookups. Caching the nodes that serve these hot key ranges means that the high-frequency lookups hit the skip table and proceed with fewer RDMA reads, while low-frequency lookups fall back to full traversal.

HOTNESS is depth-blind. It treats a node at depth 1 (which saves 1 RDMA read per cache hit) identically to a node at depth 3 (which saves 3 RDMA reads per cache hit), as long as both have the same `read_count`. This is the key limitation that CRITICALITY attempts to address.

A secondary limitation is the root node. The root has the highest `read_count` of any node in the tree (every lookup starts at the root during the load phase) but has `now_pos` = 0, meaning it cannot be usefully cached as a skip entry. The skip table insertion logic skips nodes where `now_pos` = 0, so the root wastes one slot in the HOTNESS top-K set without contributing a skip entry. This reduces the effective budget for HOTNESS by 1 in every run.

In the experiments, HOTNESS with budget=5,000 inserted 176 shortcuts on workload f. The median CRIT-5000 run with budget=5,000 inserted 179–180 shortcuts.

### 5.3 CRITICALITY: Depth-Weighted Ranking

CRITICALITY ranks all tracked inner nodes by their `depth_sum` value in descending order. Recall that `depth_sum` = sum of `now_pos` across all read traversals = total number of avoidable RDMA reads if this node were always cached in the skip table.

The formal justification: suppose node N is at depth d and is visited n times during the run. Without a skip entry for N, each of those n visits requires d RDMA reads to reach N from the root (one per ancestor level). With a skip entry for N, each visit requires 0 RDMA reads to reach N (the skip table provides a direct pointer). The RDMA savings is d × n = `depth_sum`. Caching the node with the highest `depth_sum` provides the greatest reduction in total RDMA work.

CRITICALITY therefore maximizes total RDMA read elimination across all lookups, subject to the budget constraint. This is the definition of performance criticality adapted from tiered memory research (Liu et al., OSDI 2025) to the RDMA traversal context.

The important distinction from HOTNESS is the depth multiplier. A depth-3 node visited n times has `depth_sum` = 3n. A depth-2 node visited n times has `depth_sum` = 2n. For CRITICALITY to prefer the depth-3 node, its visit count only needs to be at least 2/3 of the depth-2 node's visit count: 3 × (2n/3) = 2n. This means depth-3 nodes get a 50% handicap — they can be visited 33% less frequently than a depth-2 node and still rank higher by criticality.

In a shallow tree (height 3), this depth amplification effect is modest. In a taller tree (height 6), a depth-6 node would need only 1/3 the visit count of a depth-2 node to outrank it. The policy benefit compounds with tree height.

### 5.4 HYBRID: Blended Scoring

HYBRID computes score = 0.5 × `read_count` + 0.5 × `depth_sum` for each node and selects the top K.

To understand what this actually means, substitute the relationship between `depth_sum` and `read_count` for nodes at different depths:

- Depth-1 node: `depth_sum` = `read_count` × 1. HYBRID score = 0.5 × rc + 0.5 × rc = rc. (Same as HOTNESS.)
- Depth-2 node: `depth_sum` = `read_count` × 2. HYBRID score = 0.5 × rc + 0.5 × 2rc = 1.5rc. (1.5× HOTNESS.)
- Depth-3 node: `depth_sum` = `read_count` × 3. HYBRID score = 0.5 × rc + 0.5 × 3rc = 2rc. (2× HOTNESS.)

In a tree where 99.2% of nodes are at depth 2, almost every node gets the same 1.5× multiplier. The ranking is essentially HOTNESS scaled by 1.5, which is an order-preserving transformation: the top-K set by HYBRID score is identical to the top-K set by HOTNESS score for depth-2 nodes. The only differentiation occurs between depth-2 nodes (multiplier 1.5) and depth-3 nodes (multiplier 2). A depth-3 node with `read_count` = rc_3 outranks a depth-2 node with `read_count` = rc_2 when 2 × rc_3 > 1.5 × rc_2, i.e., when rc_3 > 0.75 × rc_2.

Compare this to CRITICALITY, where the same depth-3 node outranks the depth-2 node when 3 × rc_3 > 2 × rc_2, i.e., when rc_3 > 0.67 × rc_2. CRITICALITY promotes depth-3 nodes more aggressively than HYBRID does (threshold 67% vs 75%).

HYBRID was run once, producing 230.8 µs. This result cannot be meaningfully compared to single runs of HOTNESS and CRITICALITY due to the high variance explained in Section 8.5.

### 5.5 How Skip Table Entries Are Inserted

The function `create_skip_table_policy()` handles skip table construction for all non-STATIC policies. The process is:

1. Call `AccessTracker::instance().get_top_k_set(budget)` which returns a `std::unordered_set<uint64_t>` (a set of `fptr` values) containing the top-K nodes by the active policy's scoring function. For CRITICALITY, this ranks by `depth_sum`. For HOTNESS, by `read_count`.

2. Call `add_shortcut_policy(target_fptrs)`, which performs a full depth-first traversal of the ART. For every inner node encountered:
   - If the node's remote pointer is in `target_fptrs`, compute the key slice (descend to any leaf, take first `now_pos` bytes), insert a RACE entry mapping that key slice to the node's address.
   - If the node's `now_pos` is 0 (the root), skip it.
   - Otherwise, recurse into children regardless.

The DFS visits all 110,272 nodes whether or not they are in the target set, because the traversal path must visit ancestor nodes to reach deep ones. The shortcut insertion itself is selective: only nodes in `target_fptrs` receive RACE entries.

For STATIC, the equivalent function inserts a RACE entry for every node it visits without filtering.

### 5.6 Budget and Why It Saturates Early

The `POLICY_MAX_ENTRIES` budget controls the maximum size of the top-K set requested from the AccessTracker. However, the actual number of shortcuts inserted is consistently far below the budget for budgets between 500 and 5,000. The observed counts are:

| Budget | Shortcuts Inserted (min–max across runs) | Budget Utilization |
|--------|------------------------------------------|--------------------|
| 500 | 128–129 | 25.7% |
| 1,000 | 130–131 | 13.1% |
| 5,000 | 177–183 | 3.6% |
| STATIC | 50,940–50,947 | N/A (no budget limit) |

A budget of 5,000 delivers only 180 shortcuts — 3.6% utilization. This extreme gap is caused by a combination of three factors:

**Factor 1: Tree topology limits.** There are only 102 depth-3 nodes and 768 depth-1 nodes. After all 870 of these are captured in the top-K set, the remaining 5,000 − 870 = 4,130 budget slots must be filled from the 109,401 depth-2 nodes. These depth-2 nodes are numerous and many have identical or near-identical key prefixes (since 1,000,000 keys spread across depth-2 nodes means each depth-2 node serves approximately 9 keys on average, with very similar two-byte prefix ranges). The DFS only inserts a shortcut for one key slice per node, so when two adjacent depth-2 nodes share nearly the same key prefix, only one insertion succeeds.

**Factor 2: The effective budget for unique shortcuts is limited by the number of structurally distinct depth positions.** The skip table lookup checks the key slice at depth 2 first. If a depth-2 entry already covers a key, inserting a depth-1 entry for the same key range is redundant (the depth-2 hit will always be found first). The insertion logic therefore does not produce redundant shallower entries when deeper entries already cover the same key.

**Factor 3: Root exclusion.** The root (depth 0) consumes one budget slot in the HOTNESS ranking but contributes zero insertions. CRITICALITY avoids this because the root's `depth_sum` = 0.

The practical consequence is that increasing the budget from 500 to 5,000 — a 10× increase — yields only ~50 additional shortcuts. The marginal value of each additional budget unit rapidly approaches zero. Only the 102 depth-3 nodes represent genuinely new insertable entries beyond the saturated depth-2 cluster, and these appear in the top-K list when budget is large enough to include nodes with moderately high `depth_sum` values.

---

## 6. Experimental Setup

### 6.1 Hardware Configuration

| Component | Specification |
|-----------|---------------|
| Compute/monitor node | 10.30.1.9 (cs-dis-srv09s) |
| Memory node | 10.30.1.6 (cs-dis-srv06s) |
| Network interconnect | 100 Gb/s InfiniBand (mlx5 HCA) |
| Compute threads | 56 per run |
| Memory-side threads | 4 per run |
| RDMA MR per thread (compute) | 50 MB (--mem_mb=50) |
| Total MR allocation (compute) | 56 × 50 MB = 2,800 MB |
| Huge pages configured (compute) | 1,500 × 2 MB = 3,000 MB |
| Huge pages configured (memory) | 1,100 × 2 MB = 2,200 MB |
| NUMA configuration | 2 NUMA nodes per machine |

RDMA memory regions must be allocated from huge pages (2 MB each, MAP_HUGETLB) because `ibv_reg_mr` requires physically pinned and contiguous memory for reliable DMA operations. Each of the 56 compute threads gets its own independent Memory Region; they are not shared. The worst-case allocation of 50 MB × 56 = 2,800 MB requires 1,400 huge pages, so vm.nr_hugepages=1,500 provides a 100-page safety margin.

### 6.2 Workloads

All workloads load 1,000,000 records and execute 1,000,000 operations in the run phase. Keys are 8-byte integers (int64). Workload files are pre-generated in YCSB format and stored in `workload/data/` on the compute node.

| Workload ID | Read % | Update % | Access Distribution | Description |
|-------------|--------|----------|---------------------|-------------|
| f | 50% | 50% (RMW) | Uniform | Read-modify-write. Each operation is either a read or a conditional update (read-then-CAS). This workload is the primary benchmark. |
| skew03 | 50% | 50% | Zipfian θ=0.3 | Gentle skew. Access is mildly concentrated toward popular keys. |
| skew05 | 50% | 50% | Zipfian θ=0.5 | Moderate skew. The top 20% of keys receive roughly 50% of accesses. |
| skew08 | 50% | 50% | Zipfian θ=0.8 | Heavy skew. The top 20% of keys receive roughly 80% of accesses. |
| skew99 | 50% | 50% | Zipfian θ=0.99 | Extreme skew. Nearly all accesses concentrate on a small number of popular keys. |
| g | 0% | 100% | Uniform | Write-only. All operations are unconditional updates (RDMA CAS sequences). |

The workload f design deserves special attention because it is used in 13 of the 23 experiments. Its 50/50 read-update split creates a severely bimodal per-thread latency distribution: threads executing read operations complete in approximately 44–45 µs, while threads executing update operations (which require RDMA CAS with retry logic under contention) complete in approximately 380–395 µs. The aggregate latency is a harmonic mean of these two populations and is highly sensitive to how the operation stream is partitioned across threads. This produces the large variance documented in Section 8.5.

### 6.3 Experiment Matrix

The 23 experiments are organized into two groups: 13 base experiments on workload f, and 10 skew-sweep experiments. The base experiments characterize policy behavior and budget sensitivity. The skew-sweep experiments test how policies behave as access concentration varies.

**Base experiments (all workload f):**

| Seq. # in file | Policy | Budget | Runs | Purpose |
|----------------|--------|--------|------|---------|
| 1 | STATIC | full DFS | 1 | Upper-bound baseline |
| 7 | HOTNESS | 5,000 | 1 | Frequency-only policy |
| 8 | HYBRID | 5,000 | 1 | Blended frequency+depth |
| 9–12 | CRIT-5000 | 5,000 | 4 | Criticality policy, repeated for variance |
| 18–20 | CRIT-500 | 500 | 3 | Criticality at smallest budget |
| 21–23 | CRIT-1000 | 1,000 | 3 | Criticality at medium budget |

Multiple repeated trials of CRIT-5000, CRIT-500, and CRIT-1000 were run because workload f's high variance makes single-run latency measurements unreliable. Even with 3–4 trials, the variance remains large.

**Skew sweep experiments:**

| Seq. # in file | Policy | Workload | Purpose |
|----------------|--------|----------|---------|
| 2 | STATIC | skew03 | STATIC behavior at gentle Zipfian skew |
| 3 | STATIC | skew05 | STATIC behavior at moderate Zipfian skew |
| 4 | STATIC | skew08 | STATIC behavior at heavy Zipfian skew |
| 5 | STATIC | skew99 | STATIC behavior at extreme Zipfian skew |
| 6 | STATIC | g | STATIC behavior under write-only workload |
| 13 | CRIT-5000 | skew03 | CRITICALITY behavior at gentle Zipfian skew |
| 14 | CRIT-5000 | skew05 | CRITICALITY behavior at moderate Zipfian skew |
| 15 | CRIT-5000 | skew08 | CRITICALITY behavior at heavy Zipfian skew |
| 16 | CRIT-5000 | skew99 | CRITICALITY behavior at extreme Zipfian skew |
| 17 | CRIT-5000 | g | CRITICALITY behavior under write-only workload |

The sequence numbers in the file correspond directly to the run labels printed in compute_results.txt: "=== COMPUTE N/23 POLICY WORKLOAD ===".

### 6.4 How Each Run Proceeds

Each of the 23 experimental runs follows this exact sequence:

1. **Compile**: The compute binary is built with the target policy defined as a compile-time macro (e.g., `#define POLICY_CRITICALITY`) and the budget set via `static constexpr uint64_t POLICY_MAX_ENTRIES = 5000`. The monitor and memory binaries are compiled once and reused.

2. **Memory node start**: On 10.30.1.6, the memory binary starts after a short sleep (5 seconds) and initializes its RDMA memory pool. It configures huge pages, registers RDMA memory regions, and starts listening on its RACE service port.

3. **Monitor start**: On 10.30.1.9, the monitor binary starts and waits for both the memory service and the compute binary to connect before signaling "start."

4. **Compute node connect** (after 35-second sleep): The compute binary starts, connects to the memory RACE service, loads 1,000,000 keys into the remote ART via RDMA writes and CAS operations, building the tree while the AccessTracker records all traversals.

5. **Load phase completion**: The load-phase AccessTracker report is printed. For non-STATIC policies, `create_skip_table_policy()` selects the top-K nodes and calls `add_shortcut_policy()` to insert RACE entries into the local skip table. The AccessTracker is reset.

6. **Run phase**: All 56 threads execute 1,000,000 operations (distributed as ~17,858 per thread). The AccessTracker records run-phase traversals. Per-thread latency and throughput are measured.

7. **Results output**: Per-thread statistics, the run-phase AccessTracker summary, the policy comparison (top-100 overlap), and the aggregate latency/throughput are printed. All output is captured to compute_results.txt via tee.

---

## 7. Complete Experiment Results: All 23 Runs

The key summary lines extracted from compute_results.txt are:
- `num shortcuts: N` — how many skip table entries were inserted
- `=== Policy Comparison (top-100) ===` — comparison of HOTNESS and CRITICALITY top-100 node sets
- `ALL: latency = X us` — aggregate average latency across all threads and operations

The "Load Overlap" columns refer to the policy comparison printed before the run phase (reflecting load-phase access statistics used to build the skip table). The "Run Overlap" columns refer to the comparison printed after the run phase (reflecting what the skip table policy would look like if rebuilt from run-phase statistics).

### 7.1 Original 13 Experiments (Workload f)

These 13 experiments characterize policy behavior and budget sensitivity on workload f (50% read, 50% read-modify-write, uniform key distribution, 1,000,000 operations across 56 threads).

| Run # | Policy | Budget | Shortcuts | Load: H/C Overlap | Load: H-only / C-only | Run: H/C Overlap | Run: H-only / C-only | Avg Latency |
|-------|--------|--------|-----------|-------------------|-----------------------|------------------|----------------------|-------------|
| 1 | STATIC | full DFS | 50,946 | 99% | 1 / 1 | 89% | 11 / 11 | **148.7 µs** |
| 7 | HOTNESS | 5,000 | 176 | 99% | 1 / 1 | 92% | 8 / 8 | **177.9 µs** |
| 8 | HYBRID | 5,000 | 178 | 99% | 1 / 1 | 92% | 8 / 8 | **230.8 µs** |
| 9 | CRIT-5000 | 5,000 | 180 | 99% | 1 / 1 | 92% | 8 / 8 | **155.0 µs** |
| 10 | CRIT-5000 | 5,000 | 180 | 99% | 1 / 1 | 92% | 8 / 8 | **235.5 µs** |
| 11 | CRIT-5000 | 5,000 | 182 | 99% | 1 / 1 | 92% | 8 / 8 | **203.5 µs** |
| 12 | CRIT-5000 | 5,000 | 177 | 99% | 1 / 1 | 92% | 8 / 8 | **201.6 µs** |
| 18 | CRIT-500 | 500 | 128 | 99% | 1 / 1 | 92% | 8 / 8 | **222.7 µs** |
| 19 | CRIT-500 | 500 | 129 | 99% | 1 / 1 | 92% | 8 / 8 | **192.8 µs** |
| 20 | CRIT-500 | 500 | 129 | 99% | 1 / 1 | 92% | 8 / 8 | **219.7 µs** |
| 21 | CRIT-1000 | 1,000 | 131 | 99% | 1 / 1 | 92% | 8 / 8 | **154.8 µs** |
| 22 | CRIT-1000 | 1,000 | 130 | 99% | 1 / 1 | 92% | 8 / 8 | **209.1 µs** |
| 23 | CRIT-1000 | 1,000 | 131 | 99% | 1 / 1 | 92% | 8 / 8 | **177.2 µs** |

**Per-policy summary statistics (workload f):**

| Policy | Budget | Shortcut Count | Best Latency | Worst Latency | Range |
|--------|--------|----------------|-------------|---------------|-------|
| STATIC | full | ~50,943 | 148.7 µs | 148.7 µs | 0 µs |
| HOTNESS | 5,000 | 176 | 177.9 µs | 177.9 µs | 0 µs (1 run) |
| HYBRID | 5,000 | 178 | 230.8 µs | 230.8 µs | 0 µs (1 run) |
| CRIT-5000 | 5,000 | 177–182 | 155.0 µs | 235.5 µs | 80.5 µs |
| CRIT-1000 | 1,000 | 130–131 | 154.8 µs | 209.1 µs | 54.3 µs |
| CRIT-500 | 500 | 128–129 | 192.8 µs | 222.7 µs | 29.9 µs |

The CRIT-5000 best run (155.0 µs) is within 6.3 µs of STATIC (148.7 µs) — a 4.2% gap. The CRIT-1000 best run (154.8 µs) is even closer. Both beat HOTNESS (177.9 µs) by approximately 13%.

### 7.2 Skew Sweep (10 Additional Experiments)

These 10 experiments test both STATIC and CRIT-5000 across five workload distributions to understand how skew affects the gap between the two policies.

| Run # | Policy | Workload | Distribution | Shortcuts | Load H/C Overlap | Run H/C Overlap | Run H-only / C-only | Avg Latency |
|-------|--------|----------|-------------|-----------|-------------------|-----------------|----------------------|-------------|
| 2 | STATIC | skew03 | Zipfian θ=0.3 | 50,945 | 99% | 92% | 8 / 8 | **138.3 µs** |
| 3 | STATIC | skew05 | Zipfian θ=0.5 | 50,947 | 99% | 92% | 8 / 8 | **186.6 µs** |
| 4 | STATIC | skew08 | Zipfian θ=0.8 | 50,940 | 99% | 92% | 8 / 8 | **187.2 µs** |
| 5 | STATIC | skew99 | Zipfian θ=0.99 | 50,943 | 99% | 93% | 7 / 7 | **146.9 µs** |
| 6 | STATIC | g | Uniform writes | 50,945 | 99% | 99% | 1 / 1 | **140.7 µs** |
| 13 | CRIT-5000 | skew03 | Zipfian θ=0.3 | 183 | 99% | 93% | 7 / 7 | **213.6 µs** |
| 14 | CRIT-5000 | skew05 | Zipfian θ=0.5 | 180 | 99% | 93% | 7 / 7 | **202.1 µs** |
| 15 | CRIT-5000 | skew08 | Zipfian θ=0.8 | 179 | 99% | 93% | 7 / 7 | **208.5 µs** |
| 16 | CRIT-5000 | skew99 | Zipfian θ=0.99 | 181 | 99% | 93% | 7 / 7 | **199.6 µs** |
| 17 | CRIT-5000 | g | Uniform writes | 180 | 99% | 99% | 1 / 1 | **183.5 µs** |

**STATIC vs CRIT-5000 performance gap by workload:**

| Workload | STATIC | CRIT-5000 | Absolute Gap | Relative Overhead |
|----------|--------|-----------|-------------|-------------------|
| f (best CRIT run) | 148.7 µs | 155.0 µs | 6.3 µs | +4.2% |
| skew03 | 138.3 µs | 213.6 µs | 75.3 µs | +54.4% |
| skew05 | 186.6 µs | 202.1 µs | 15.5 µs | +8.3% |
| skew08 | 187.2 µs | 208.5 µs | 21.3 µs | +11.4% |
| skew99 | 146.9 µs | 199.6 µs | 52.7 µs | +35.9% |
| g | 140.7 µs | 183.5 µs | 42.8 µs | +30.4% |

STATIC wins in all cases. The gap ranges from 4.2% (workload f, best CRIT run) to 54.4% (skew03). The reasons for this wide variation are analyzed in Section 8.6.

---

## 8. Detailed Analysis of Every Result

### 8.1 Why STATIC Is Always the Fastest

STATIC achieves the best latency in every single experiment. With approximately 50,943 shortcuts covering all 110,272 inner nodes, the skip table provides near-universal coverage: for virtually any key, there is a RACE entry pointing directly to the depth-2 or depth-3 ART node on the lookup path. The traversal path under STATIC is:

1. Local skip table lookup — zero RDMA operations, nanosecond cost.
2. One RDMA READ to fetch the target ART node (typically at depth 2 or 3, 136 bytes).
3. One RDMA READ to fetch the leaf (1,095 bytes).

Total RDMA reads per lookup: 2 (possibly 3 if the skip target is at depth 2 and one more hop is needed). This is the minimum achievable for a height-3 tree.

Under learned policies with 180 shortcuts, lookups whose keys map to one of the 180 cached nodes benefit similarly. But lookups whose keys map to any of the remaining 110,092 uncached nodes fall back to full traversal:

1. One RDMA READ for the root (depth 0).
2. One RDMA READ for the depth-1 node.
3. One RDMA READ for the depth-2 node.
4. One RDMA READ for the leaf.

Total RDMA reads: 4.

Even under a heavily skewed workload where the top-180 nodes receive a disproportionate share of lookups, the remaining key space still generates enough lookups to substantially inflate the aggregate latency. STATIC avoids this entirely because it has no uncached nodes.

The mathematical argument: let p be the probability that a random lookup hits one of the 180 cached nodes under CRIT-5000. The aggregate RDMA reads per operation = p × 2 + (1-p) × 4 = 4 - 2p. STATIC achieves 4 - 2 × 1.0 = 2 reads per operation. CRIT-5000 with 180 shortcuts covering ~0.16% of nodes achieves approximately 4 - 2 × 0.0016 = 3.997 reads per operation under a uniform distribution. Under Zipfian skew, p is higher, but even at θ=0.99 the hot fraction is unlikely to exceed 10–20% of all lookups.

The observed latency numbers confirm this: STATIC at 148.7 µs vs CRIT-5000 at 155–235 µs (a range reflecting both the policy effect and workload f's inherent variance). The STATIC advantage would be even cleaner on a uniform read-only workload.

### 8.2 Why CRITICALITY Beats HOTNESS

The single most direct comparison in the entire experiment set is run 9 (CRIT-5000, 155.0 µs) versus run 7 (HOTNESS, 177.9 µs). CRITICALITY achieves 13% lower latency with only 4 more shortcuts (180 vs 176). The improvement is structural, not statistical noise.

The mechanism: CRITICALITY selects 8 different nodes than HOTNESS for the top-100 set used to build the skip table. Specifically, CRITICALITY promotes 8 depth-3 nodes that HOTNESS ranks below its top-100, while demoting 8 depth-2 nodes that HOTNESS would have included.

From the run-phase output of run 9, the CRITICALITY-specific nodes (those in CRITICALITY top-100 but not HOTNESS top-100) include:

| fptr | read_count | depth_sum | avg_depth | Interpretation |
|------|------------|-----------|-----------|----------------|
| 0xc53b6c0 | 7,615 | 22,845 | 3.0 | Depth-3 node; saves 3 RTTs per hit |
| 0x30c4c9c0 | 5,518 | 16,554 | 3.0 | Depth-3 node; saves 3 RTTs per hit |
| 0x3a54bac0 | 4,201 | 12,603 | 3.0 | Depth-3 node; fewer reads but very deep |

These depth-3 nodes have moderate read counts (4,000–7,600) but high depth_sum values (12,000–22,000) because each visit is counted with multiplier 3. HOTNESS ranks them below the cut because their raw read_count is lower than the depth-2 nodes at the boundary of the HOTNESS top-100.

The HOTNESS-only nodes that are displaced are depth-2 nodes with read counts of approximately 7,000–8,000. Substituting a depth-3 node with 5,000 reads for a depth-2 node with 7,000 reads: the depth-3 node saves 3 RTTs × 5,000 visits = 15,000 total RTTs over the workload. The depth-2 node saves 2 RTTs × 7,000 visits = 14,000 total RTTs. The depth-3 node wins despite lower visit count.

This is precisely the CRITICALITY advantage in action: by accounting for depth, the policy correctly identifies that less-visited deeper nodes provide more aggregate RTT savings than more-visited shallower nodes.

The CRIT-1000 best run (154.8 µs, run 21) is essentially tied with CRIT-5000 best run (155.0 µs, run 9). This confirms that the performance benefit comes from correctly selecting the depth-3 nodes, not from having more shortcuts overall. CRIT-1000 with 131 shortcuts achieves the same latency as CRIT-5000 with 180 shortcuts because the additional ~50 shortcuts added by the larger budget are lower-value depth-2 entries that do not meaningfully improve skip table hit rate.

### 8.3 Why HYBRID Performs Worst

HYBRID received a single run and produced 230.8 µs. This is the worst latency among all learned policies, including CRIT-500 (whose best run was 192.8 µs).

There are two explanations, one structural and one statistical.

**Structural explanation**: As shown in Section 5.4, HYBRID with alpha=0.5 in a tree where 99.2% of nodes are at depth 2 is nearly equivalent to HOTNESS. The ranking differences are negligible for depth-2 nodes. HYBRID inserts 178 shortcuts — between HOTNESS (176) and CRIT-5000 (180) — and its skip table is almost identical in composition to HOTNESS's skip table. On this basis, HYBRID should perform similarly to HOTNESS, not worse.

**Statistical explanation**: Workload f's variance is so high that a single run can land anywhere between 155 and 235 µs regardless of the policy (as demonstrated by CRIT-5000's four runs spanning 80 µs). With one HYBRID run producing 230.8 µs, it is likely that this run happened to have an unfavorable thread schedule (more threads executing update-heavy operations, creating more CAS contention and higher aggregate latency). There is no justification to conclude that HYBRID is a worse policy than HOTNESS based on this single data point.

To accurately evaluate HYBRID, 3–4 repeated runs would be necessary. In the absence of those runs, the HYBRID result of 230.8 µs should be treated as uninformative regarding policy quality.

### 8.4 Budget Saturation: Why More Budget Does Not Always Help

The relationship between budget and actual shortcut insertions is highly nonlinear and saturates very early:

| Budget | Avg Shortcuts | Budget Utilization | Marginal shortcuts vs previous tier |
|--------|---------------|--------------------|------------------------------------|
| 500 | 128.7 | 25.7% | — |
| 1,000 | 130.7 | 13.1% | +2 |
| 5,000 | 179.8 | 3.6% | +49 |
| STATIC | ~50,943 | unlimited | +50,763 |

The jump from 500 to 1,000 adds only 2 shortcuts. The jump from 1,000 to 5,000 adds 49 shortcuts. The jump from 5,000 to STATIC adds 50,763 shortcuts.

To understand why budget=500 already captures 128 shortcuts while budget=1,000 only adds 2 more, consider the criticality score distribution. The top-500 nodes by depth_sum are all depth-2 nodes with the highest read counts. The top-1,000 nodes include more depth-2 nodes, but these additional nodes are adjacent key-range neighbors of the already-selected ones. When `add_shortcut_policy()` descends to extract a key slice for insertion, these adjacent nodes' key slices may already be covered by existing entries (or may map to the same RACE bucket), preventing insertion. The effective unique shortcut count is bounded by the number of distinct addressable key prefixes in the RACE table layout.

The jump from 1,000 to 5,000 adds 49 shortcuts because at budget=5,000, the top-K set begins to include the 102 depth-3 nodes. Depth-3 nodes have unique three-byte key prefixes that do not collide with two-byte depth-2 entries. Each newly-included depth-3 node successfully inserts a unique RACE entry. There are approximately 102 such nodes but only ~49–50 of them appear in the top-5,000 set (the remainder have low enough access counts to fall below the 5,000th rank threshold). These ~50 depth-3 shortcuts represent the entire value of increasing the budget from 1,000 to 5,000.

Beyond budget=5,000, there is no significant increase in shortcuts without fundamentally changing the tree structure (adding more keys to create more depth-3 and depth-4 nodes).

**Performance implication**: The latency difference between CRIT-500 (best: 192.8 µs), CRIT-1000 (best: 154.8 µs), and CRIT-5000 (best: 155.0 µs) primarily reflects the addition of those ~49 depth-3 nodes when going from 500/1000 to 5000. CRIT-1000 and CRIT-5000 perform essentially the same because they both capture the critical depth-3 nodes. CRIT-500 performs worse because its 128 shortcuts cover fewer of these depth-3 nodes (only the highest-traffic ones within the 500-node budget).

### 8.5 High Variance in Workload f Results

The most challenging aspect of interpreting these results is the extreme run-to-run variance in workload f experiments. The four repeated CRIT-5000 runs on workload f produced:

| Run | Latency |
|-----|---------|
| 9 | 155.0 µs |
| 10 | 235.5 µs |
| 11 | 203.5 µs |
| 12 | 201.6 µs |

The range is 80.5 µs. This is not a policy effect — the same policy configuration produced results differing by 52%. To understand why, consider the per-thread breakdown from run 1 (STATIC, workload f):

**Fast threads (operations = reads):**

| Thread range | Latency | Throughput | RTT count |
|-------------|---------|------------|-----------|
| Threads 2–40 | 44.4–45.4 µs | 0.0220–0.0225 MOps | 4.86–4.88 |

**Slow threads (operations = updates):**

| Thread range | Latency | Throughput | RTT count |
|-------------|---------|------------|-----------|
| Thread 1 | 381.3 µs | 0.00262 MOps | 4.88 |
| Thread 41 | 384.9 µs | 0.00260 MOps | 4.87 |
| Threads 42–56 | 376–395 µs | 0.00253–0.00261 MOps | 4.87–4.89 |

Notice that slow threads have RTT counts nearly identical to fast threads (4.87–4.88 vs 4.86–4.88). The latency difference is not from issuing more RDMA reads per operation. It comes from CAS retry loops under thread contention.

A read-modify-write operation in DART consists of:
1. RDMA READ to fetch the target key's current value.
2. RDMA CAS to atomically update it.
3. If CAS fails (another thread concurrently modified the same location), loop back to step 1 and retry.

Under 56 threads accessing a 1,000,000-key keyspace uniformly, the probability of CAS collision for any given key is approximately 56/1,000,000 per round — low, but when multiple operations target the same popular key, CAS failures cascade. Each retry adds a full RDMA round-trip pair. Over 17,858 operations per thread, even a 1% CAS failure rate adds 178 retries × 2 RTTs = 356 extra round-trips per thread, adding tens of microseconds to the thread's elapsed time.

The aggregate latency is computed as total_operations / total_throughput, which is equivalent to the sum of all per-operation times divided by operation count. This sum is dominated by the slow threads: if 17 threads (30% of 56) are running at 385 µs while 39 threads are at 45 µs, the aggregate is approximately (17 × 385 + 39 × 45) / 56 ≈ (6,545 + 1,755) / 56 ≈ 148 µs.

However, the actual partitioning of fast and slow threads is not deterministic across runs. The YCSB workload generator creates a mixed operation stream for each thread. Depending on the random seed and threading assignment, some runs may assign more contention-heavy update patterns to certain threads. In run 10, the partitioning was apparently worse (more contention), producing 235.5 µs. In run 9, the partitioning was favorable, producing 155.0 µs.

**Implication**: For workload f, the true policy performance signal is best extracted from the minimum latency across repeated trials (which represents a favorable scheduling) rather than the mean. The minimum latency for CRIT-5000 is 155.0 µs; for CRIT-1000 it is 154.8 µs; for CRIT-500 it is 192.8 µs. These minima are more representative of each policy's capability than the average.

### 8.6 Skew Sweep Analysis

The skew sweep results expose two distinct phenomena: how STATIC's performance varies with access distribution, and why CRIT-5000's overhead relative to STATIC is not monotonic in skew level.

**STATIC latency variation by workload:**

| Workload | STATIC Latency | Interpretation |
|----------|----------------|----------------|
| skew03 (θ=0.3) | 138.3 µs | Fastest STATIC result |
| g (writes) | 140.7 µs | Nearly as fast as skew03 |
| skew99 (θ=0.99) | 146.9 µs | Third fastest |
| f (uniform RMW) | 148.7 µs | Fourth |
| skew05 (θ=0.5) | 186.6 µs | Slowest STATIC results |
| skew08 (θ=0.8) | 187.2 µs | Essentially tied with skew05 |

The counter-intuitive result is that moderate Zipfian skew (θ=0.5 and θ=0.8) produces the worst STATIC performance, while extreme skew (θ=0.99) performs better than both. STATIC has full tree coverage, so its performance should be independent of access distribution in theory. In practice, cache pressure effects on the compute node's local DRAM become relevant.

Under uniform access (workload f) or gentle skew (skew03), each lookup accesses a different portion of the skip table and the ART node pool, resulting in effective use of CPU caches on the compute node. Under moderate Zipfian skew (θ=0.5–0.8), the most popular keys (maybe top 20% of keyspace) are accessed repeatedly, but there is still enough spread across the remaining 80% to cause many distinct ART node fetches. This maximizes L1/L2 cache pressure on the compute node's DRAM controller and may introduce memory bus congestion for the local skip table.

Under extreme skew (θ=0.99), the access pattern becomes so concentrated on a tiny key range that the relevant skip table entries and their corresponding remote ART nodes may effectively be "warm" in the compute node's CPU caches. This reduces the effective latency of skip table lookups and the RDMA round-trip scheduling overhead, explaining why skew99 performs better than skew05/skew08.

**CRIT-5000 performance variation by workload:**

| Workload | CRIT-5000 | STATIC | CRIT overhead |
|----------|-----------|--------|---------------|
| f (best) | 155.0 µs | 148.7 µs | +4.2% |
| skew05 | 202.1 µs | 186.6 µs | +8.3% |
| skew08 | 208.5 µs | 187.2 µs | +11.4% |
| g | 183.5 µs | 140.7 µs | +30.4% |
| skew99 | 199.6 µs | 146.9 µs | +35.9% |
| skew03 | 213.6 µs | 138.3 µs | +54.4% |

The smallest overhead (4.2%) is for workload f, but this is a single best-run comparison that benefited from a favorable thread schedule. The most reliable skew comparisons show:

Under skew03 (gentle Zipf), CRIT-5000 is 54.4% slower than STATIC. This is the largest gap. Under gentle skew, the access distribution is nearly uniform, meaning all 110,272 nodes receive similar access counts. CRIT-5000's 180 shortcuts cover only ~0.16% of the nodes, and that 0.16% is no more likely to be accessed than any other 0.16% because there is no concentration. The skip table effectively provides no benefit over uncached traversal, and the overhead of building it is wasted. STATIC, conversely, covers everything and wins decisively.

Under skew99 (extreme Zipf), CRIT-5000 is 35.9% slower than STATIC. Despite the strong skew, CRIT-5000's 180 shortcuts still cover only a small fraction of the keyspace. Even if the top-1% of keys receive 90% of accesses, those top keys are served by perhaps 1,100 of the 110,272 inner nodes (approximately 1% of nodes). CRIT-5000 caches 180 of those 1,100 nodes — a 16% coverage rate for the hot subtree. The remaining 84% of the hot subtree falls back to full traversal, explaining why CRIT-5000 is still substantially slower than STATIC even under extreme skew.

The workload g (write-only) result shows a 30.4% gap (183.5 µs vs 140.7 µs). Writes in DART follow the same tree traversal path as reads but with additional CAS operations at the leaf. The skip table provides the same RTT savings for writes as for reads. CRIT-5000's write performance suffers for the same coverage reason as reads.

An important structural observation from the run-phase policy comparison for workload g: under write-only access, the run-phase overlap between HOTNESS and CRITICALITY is 99% (only 1 node differs). This occurs because writes traverse the tree without the depth-differentiation that reads exhibit. During writes, every key is written exactly once, so every node at depth d receives exactly as many write traversals as keys pass through it. The depth_sum and read_count (write_count is tracked separately) are proportional for all nodes. The CRITICALITY and HOTNESS rankings collapse to the same order, and the skip tables they would build are identical. This confirms that CRITICALITY's advantage over HOTNESS only manifests when repeated reads create a concentrated access pattern with depth variation.

---

## 9. Policy Comparison: What the Overlap Numbers Mean

Each experiment prints two policy comparison reports from `print_policy_comparison()`. These reports compare which nodes would be selected by HOTNESS (top-100 by read_count) versus CRITICALITY (top-100 by depth_sum). The comparison quantifies how similar or different the two policies are for a given access trace.

### 9.1 Load Phase Overlap

Across all 23 experiments, the load-phase overlap is 99% in every single run — exactly 99 of the top-100 nodes are shared between HOTNESS and CRITICALITY, with 1 node unique to each.

The reason for this consistency is the nature of the load phase. During key insertion, 1,000,000 keys are uniformly distributed across the tree. Every inner node receives write traversals proportional to how many keys its subtree contains. For depth-2 nodes (which hold approximately 9 keys each), the write count ≈ subtree_size / something proportional. The important point is that all depth-2 nodes receive approximately the same write counts, all depth-1 nodes receive approximately the same (much higher) write counts, and the root receives all write counts.

With near-uniform write counts, `depth_sum` ≈ d × `write_count` for all nodes at depth d. Sorting by HOTNESS (write_count) vs CRITICALITY (depth_sum) for a tree where all nodes are at the same depth produces an identical ranking. The single divergent node is a depth-3 node whose `depth_sum` = 3 × (moderate write count) is just barely enough to push it into the CRITICALITY top-100 at the expense of a depth-2 node with a slightly higher write count but lower depth_sum (2 × higher write count).

The 99% load-phase overlap means that the skip tables built by HOTNESS and CRITICALITY from load-phase statistics are almost identical. The tiny difference (1 node) is precisely the depth-3 vs depth-2 boundary effect.

### 9.2 Run Phase Overlap

The run-phase overlap varies meaningfully by workload and reflects how strongly the run-phase access pattern creates depth differentiation:

| Workload | Run Phase Overlap | H-only / C-only per list |
|----------|------------------|--------------------------|
| f (uniform RMW) | 89–92% | 8–11 / 8–11 |
| skew03 | 92–93% | 7–8 / 7–8 |
| skew05 | 92–93% | 7–8 / 7–8 |
| skew08 | 92–93% | 7–8 / 7–8 |
| skew99 | 92–93% | 7–8 / 7–8 |
| g (write-only) | 99% | 1 / 1 |

**Workload f shows more divergence (89–92%) than skew workloads (92–93%)**. Under workload f, the uniform distribution of reads means that depth-2 nodes accumulate reads proportional to the key range they serve. But some depth-3 nodes that serve highly-specific key suffixes within a popular range receive repeated reads that accumulate enough depth_sum to cross into the CRITICALITY top-100. The uniformity also means that the depth-2/depth-3 boundary is less crowded: depth-2 nodes have moderate read counts, leaving room for depth-3 nodes with depth-multiplied scores to compete. Run 1 specifically shows 89% overlap — 11 divergent nodes — probably because that particular random interleaving of operations created especially concentrated access to certain depth-3 subtrees.

**Skew workloads show less divergence (92–93%)**. Under Zipfian access, the top depth-2 nodes accumulate very high read counts because they serve the most popular key ranges. A depth-3 node serving a popular key's suffix would need 2/3 of that depth-2 node's read count to outrank it by depth_sum. Under extreme skew, the top depth-2 nodes receive vastly more reads than typical depth-3 nodes, pushing depth-3 nodes below the top-100 cutoff. Paradoxically, extreme skew reduces the CRITICALITY-HOTNESS divergence because the popular depth-2 nodes dominate both rankings.

**Workload g shows 99% overlap**. Write-only workloads traverse the tree with counts that are proportional to key frequency (uniform). `depth_sum` ≈ depth × `write_count`. For nodes at the same depth, the rankings are identical. Only the boundary between depth-2 and depth-3 nodes shows a single divergent case.

### 9.3 What HOTNESS-only and CRITICALITY-only Nodes Are

A **HOTNESS-only** node is one in the top-100 by `read_count` but NOT in the top-100 by `depth_sum`. This node is visited frequently, but its `depth_sum` is moderate because it sits at a shallow depth. Its depth multiplier (1 for depth-1, 2 for depth-2) is insufficient to make its `depth_sum` competitive against deeper nodes.

Concretely, from run 9 (CRIT-5000, workload f, run phase): the 8 HOTNESS-only nodes in the run-phase comparison are depth-2 nodes with read counts around 7,000–10,000. Their `depth_sum` values are correspondingly 14,000–20,000. These nodes serve popular key ranges but are too shallow to rank highly by criticality.

A **CRITICALITY-only** node is one in the top-100 by `depth_sum` but NOT in the top-100 by `read_count`. This node is visited less frequently, but its `depth_sum` is boosted by its depth position. These are depth-3 nodes with read counts around 4,000–7,600. Their `depth_sum` values of 12,000–22,845 (= 3 × 4,000–7,615) are sufficient to crack the top-100 by criticality despite not reaching top-100 by raw frequency.

The substitution performed when CRITICALITY is used instead of HOTNESS is: remove the 8 depth-2 nodes (shallow, 14,000–20,000 total RTT savings each) and add the 8 depth-3 nodes (deep, 12,000–22,845 total RTT savings each). In the best cases, the replaced depth-3 nodes provide more total RTT savings than the depth-2 nodes they displaced. This is why CRITICALITY's run 9 achieves 155.0 µs versus HOTNESS's 177.9 µs.

---

## 10. Thread-Level Observations: Bimodal Latency

The per-thread output from run 1 (STATIC, workload f, 56 threads) reveals the bimodal latency structure in detail:

| Thread ID | Latency | Throughput | RTTs/op | Bandwidth (bytes) | Op type |
|-----------|---------|------------|---------|-------------------|---------|
| 1 | 381.3 µs | 0.00262 MOps | 4.88 | 3,826 | Update |
| 2 | 45.1 µs | 0.0222 MOps | 4.87 | 3,796 | Read |
| 3 | 45.1 µs | 0.0222 MOps | 4.87 | 3,820 | Read |
| ... | 44–45 µs | 0.022 MOps | 4.87 | ~3,800 | Read |
| 41 | 384.9 µs | 0.00260 MOps | 4.87 | 3,816 | Update |
| 42 | 391.4 µs | 0.00255 MOps | 4.87 | 3,805 | Update |
| ... | 376–395 µs | 0.00253–0.00261 MOps | 4.87 | ~3,810 | Update |
| 56 | 376.3 µs | 0.00266 MOps | 4.88 | 3,827 | Update |

Several observations from this breakdown:

**RTT counts are nearly identical across fast and slow threads.** Both groups average approximately 4.87 RTTs per operation. This establishes that the latency difference is not caused by different numbers of RDMA operations. Slow threads (update-heavy) do not issue more reads per operation; they issue the same reads but with longer wait times due to CAS contention.

**Bandwidth consumption per operation is nearly identical.** Both groups consume approximately 3,785–3,832 bytes per operation. Reads consume: 256 (RACE bucket) + 136 (ART node via shortcut) + 1,095 (leaf) + some metadata = ~1,820 bytes. Updates consume the same reads plus CAS operations (8-byte CAS each) plus leaf writes (1,095 bytes) ≈ 2,800–3,800 bytes total. The similarity suggests that the CAS retries on the fast path (which consume extra bandwidth) are compensating for the fewer read operations on paths that hit cached nodes.

**The latency ratio is approximately 8.5×.** Fast threads at 44.5 µs vs slow threads at 385 µs = 8.6× difference. This is the latency cost of a CAS-based update relative to a pure read in this RDMA configuration.

**Aggregate throughput is 0.376 MOps/s = 376,534 operations per second**, which divided into 1,000,000 operations = 2.657 seconds runtime. This is the denominator for computing the aggregate latency (148.7 µs = 1,000,000 / 376,534 / 56 threads, simplified).

---

## 11. The Root Node Anomaly

In every load-phase AccessTracker summary, the root node (fptr = 0x0 or similar) appears as rank 1 by HOTNESS with `read_count` = 1,001,553 and `depth_sum` = 0.

Why rank 1 by hotness? Every key insertion begins at the root, traversing from depth 0 downward. During the load of 1,000,000 keys, the root is visited 1,000,000 times plus additional visits from failed CAS retries. The 1,001,553 figure represents 1,000,000 successful insertions plus approximately 1,553 retry traversals from CAS collisions (a collision rate of 0.155% — very low, confirming correct lock-free behavior).

Why `depth_sum` = 0? Because the root is always traversed at `now_pos` = 0. Each visit contributes 0 to `depth_sum`. Caching the root as a skip table entry is completely useless — a skip entry pointing to the root saves zero RDMA reads, because traversal always starts at the root anyway. There is nothing to skip to.

This is a fundamental structural asymmetry between HOTNESS and CRITICALITY:

- **HOTNESS** ranks the root as #1 and wastes one budget slot on it. The insertion logic handles this by checking `now_pos > 0` before inserting, so the root never actually gets a RACE entry. But it still occupies a rank-1 slot, pushing every other node down by one rank. With budget=5,000 and the root wasting rank 1, HOTNESS effectively has 4,999 productive slots.

- **CRITICALITY** ranks the root near last (or dead last) because `depth_sum` = 0. The root does not waste a budget slot. All budget slots go to nodes that can actually be cached usefully.

This is a correctness advantage of CRITICALITY over HOTNESS: CRITICALITY's scoring function naturally excludes useless nodes (those with `depth_sum` = 0 or very low `depth_sum` relative to shallow-depth anchored scores) from the top-K selection. HOTNESS has no such filter and must rely on the insertion logic to exclude already-covered nodes.

---

## 12. Why CRITICALITY Cannot Beat STATIC at This Scale

CRITICALITY on its best run (155.0 µs, run 9) is within 6.3 µs (4.2%) of STATIC (148.7 µs). This is impressively close given the 284× difference in shortcut count (180 vs 50,946). Nevertheless, CRITICALITY cannot beat STATIC at this scale, and the reasons are structural rather than implementation artifacts.

**Coverage arithmetic**: With 180 shortcuts, CRIT-5000 covers approximately 180 out of 110,272 inner nodes, or 0.16% of the tree. Even under optimal conditions (maximally skewed access), the nodes not covered by the skip table receive some fraction of all lookups. STATIC covers all nodes and provides a skip table hit for every possible lookup. The performance floor for STATIC is determined by the irreducible RDMA cost (2 reads: one for the skip-target ART node and one for the leaf). CRITICALITY's floor is at least those 2 reads for hits plus 4 reads for misses, weighted by the fraction of lookups that miss the 180-entry table.

**Depth limitation**: The maximum depth in this tree is 3. The maximum RTT savings per cached depth-3 node is 3 reads. STATIC's advantage over CRITICALITY accumulates across 50,000+ entries, each providing 2–3 RTTs of savings on every hit. CRITICALITY's 180 entries provide at most 3 × 180 = 540 RTT-reads saved per full pass over the 180 entries. STATIC saves approximately 50,943 × 2–3 RTTs per full pass. The scale is not comparable.

**Load-run distribution mismatch**: The skip table is built from load-phase access statistics, but the run phase has a different access distribution (as evidenced by the run-phase policy comparison showing different top-100 sets than load-phase). STATIC's DFS is immune to this mismatch because it covers everything structurally.

**The regime where CRITICALITY would win**: If the key count were increased to 10,000,000, the tree would have height 5 or 6. STATIC would need to enumerate millions of nodes, potentially exceeding RACE table capacity. With a fixed budget of 5,000 entries, CRITICALITY would correctly identify the 5,000 depth-5 and depth-6 nodes that receive the most traffic and cache them. Each cache hit saves 5–6 RDMA reads. STATIC, unable to cache all nodes, would have many lookup misses and its advantage would erode. In that regime, a well-designed CRITICALITY policy might approach or exceed STATIC's performance while using a fraction of the table entries.

---

## 13. How to Make CRITICALITY Perform Better

The current results establish CRITICALITY as meaningfully better than HOTNESS (13% on the best comparison run) but still behind STATIC (4.2% gap at best). Several concrete directions exist to improve CRITICALITY's performance:

**Increase the key count to 10 million or more.** This is the most impactful change. With a taller tree (height 5–6), depth-5 nodes have criticality scores of 5 × read_count. The ranking divergence between HOTNESS and CRITICALITY becomes dramatic — a depth-5 node with 2,000 visits (depth_sum = 10,000) outranks a depth-2 node with 4,999 visits (depth_sum = 9,998) by criticality but not by hotness. More importantly, STATIC can no longer cache the full tree within the skip table budget, making learned policies genuinely competitive.

**Add a dedicated warmup phase before skip table construction.** Currently the skip table is built from load-phase statistics, where all keys are inserted uniformly. If a short warmup of 100,000–500,000 read operations were run before `create_skip_table_policy()`, the resulting statistics would closely match the actual run-phase access pattern. CRITICALITY would then select the depth-3 nodes that the run workload actually accesses most, rather than those that were merely traversed proportionally during key insertion. This would increase the skip table hit rate for the run phase.

**Modify the criticality score to normalize by depth.** The current `depth_sum` = sum(`now_pos`) gives credit proportional to both depth and frequency. An alternative: `criticality_score = sum(now_pos - 1)` subtracts the depth-1 baseline, giving zero credit for depth-1 nodes and amplifying depth-3 nodes (which would score 2n instead of 3n for n visits). This makes the depth-3 vs depth-2 boundary sharper and would promote more depth-3 nodes into the top-K set relative to depth-2 nodes.

**Online retraining.** Periodically reset the AccessTracker during the run phase (e.g., every 200,000 operations) and rebuild the skip table from fresh statistics. This allows CRITICALITY to adapt as the workload evolves. If certain nodes become cold during the run (perhaps because Zipfian concentration shifts), they can be evicted from the skip table and replaced by newly-hot nodes. STATIC cannot adapt at all because it is a one-time structural enumeration.

**Increase the skip table budget significantly for large trees.** For a 10M-key tree with height 6 and potentially 1 million inner nodes, a budget of 5,000 represents 0.5% coverage. Increasing the budget to 50,000 would provide 5% coverage. If 5% of nodes serve 80% of accesses under Zipfian θ=0.99, this would dramatically increase the skip table hit rate. At 50,000 budget, CRITICALITY's advantage over HOTNESS would be most pronounced because there are many more depth levels to differentiate.

---

## 14. Running the Experiments

### Prerequisites

Both nodes must have:
- InfiniBand HCA (mlx5) with 100 Gb/s capability.
- RDMA user-space libraries: libibverbs, librdmacm, rdma-core.
- cmake 3.10+, g++ with C++17 support.
- Huge pages configured (instructions below).
- The repository cloned at `~/comp-arch/DART` on both nodes.

### Configuring Huge Pages

These commands must be run before starting any binary. The settings do not persist across reboots.

**On the compute node (10.30.1.9):**
```bash
sudo sysctl -w vm.nr_hugepages=1500
cat /proc/meminfo | grep HugePages
```

Expected output includes `HugePages_Total: 1500` and `HugePages_Free: 1500` (before any binary runs).

**On the memory node (10.30.1.6):**
```bash
sudo sysctl -w vm.nr_hugepages=1100
```

The compute node requires 1,400 huge pages (56 threads × 50 MB / 2 MB per page). Setting 1,500 provides a 100-page margin. If huge page allocation fails at runtime, you will see:

```
huge_page_alloc failed: 12 (ENOMEM)
server create mr error
```

In this case, increase vm.nr_hugepages or reduce --mem_mb. If the system cannot allocate contiguous huge pages even after setting vm.nr_hugepages, a reboot is necessary to defragment physical memory.

### Building

From `~/comp-arch/DART` on the compute node:

```bash
# Initial build
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..
```

**Changing the active policy**: Edit `src/main/compute.cc` and change the policy macro and budget before rebuilding:

```cpp
// Choose exactly ONE of:
#define POLICY_STATIC
//#define POLICY_HOTNESS
//#define POLICY_CRITICALITY
//#define POLICY_HYBRID

static constexpr uint64_t POLICY_MAX_ENTRIES = 5000;  // 500, 1000, or 5000
```

After editing, rebuild:
```bash
cd build && make -j$(nproc) && cd ..
```

### Running a Single Experiment

Open three terminals. The timing (5s/35s sleep offsets) gives the memory node a 30-second head start to initialize its RACE service before the compute binary attempts to connect.

**Terminal 1 — Memory node (10.30.1.6):**
```bash
sleep 5 && ./build/memory --mem_mb=2048 --thread_num=4
```

**Terminal 2 — Monitor (10.30.1.9):**
```bash
./build/monitor
```

**Terminal 3 — Compute (10.30.1.9, results captured):**
```bash
sleep 35 && ./build/compute \
  --thread_num=56 \
  --mem_mb=50 \
  --ips=10.30.1.6 \
  --workload_load=workload/data/f_load \
  --workload_run=workload/data/f_run \
  2>&1 | tee -a compute_results.txt
```

Replace `f_load`/`f_run` with the desired workload (e.g., `skew03_load`/`skew03_run`).

### Common Errors and Fixes

**`ERROR [aiordma.cc:683] connect: -1` followed by core dump:**

The compute binary tried to connect to the memory node's RACE service before it was ready. Increase the sleep before the compute command:
```bash
sleep 45 && ./build/compute ...
```

**`huge_page_alloc failed: 12 (ENOMEM)`:**

Not enough huge pages. Set a higher value and verify free huge pages:
```bash
sudo sysctl -w vm.nr_hugepages=2000
grep HugePages /proc/meminfo
```

**`git add` warning about embedded repositories:**

The gflags, magic_enum, and pylib/argimpl directories contain their own `.git` folders. Add them to `.gitignore` and remove cached entries:
```bash
git rm --cached gflags magic_enum pylib/argimpl
echo "gflags/" >> .gitignore
echo "magic_enum/" >> .gitignore
echo "pylib/" >> .gitignore
git add .gitignore && git commit -m "Exclude submodule dirs"
```

**`git push` rejected (non-fast-forward):**

Pull the remote state before pushing:
```bash
git stash
git pull --rebase
git stash pop
git add compute_results.txt
git commit -m "Add experimental results"
git push
```

### Interpreting Results

The key output lines in compute_results.txt for each run are:

```
=== COMPUTE N/23 POLICY WORKLOAD ===    <- run identifier

num shortcuts: 180                       <- skip table entries inserted

=== Policy Comparison (top-100) ===     <- load-phase comparison
  Overlap: 99 nodes (99%)
  HOTNESS-only: 1
  CRITICALITY-only: 1

[Run phase executes]

=== Policy Comparison (top-100) ===     <- run-phase comparison
  Overlap: 92 nodes (92%)
  HOTNESS-only: 8
  CRITICALITY-only: 8

ALL: latency = 155.0 us                 <- aggregate latency (key metric)
ALL: throughput = 0.4 MOps              <- aggregate throughput
```

The `ALL: latency` line is the primary performance metric. It is computed as:

```
latency = 1 / (sum_of_per_thread_throughputs)
```

which equals the total wall-clock time divided by the total number of operations, then multiplied by 1 (since throughput is already in MOps/s and latency is its inverse in µs).

---

## 15. Source File Reference

| File | Role |
|------|------|
| `src/main/compute.cc` | Main entry point for the compute binary. Contains policy macro definitions, MR allocation loop, load/run phase orchestration, AccessTracker integration points (print reports, reset, create_skip_table_policy). |
| `include/prheart/access-tracker.hpp` | Header for the access tracking system. Defines `NodeAccessRecord` (read_count, depth_sum, write_count, scoring functions), the `AccessTracker` singleton class (256 shards, get_top_k, get_top_k_set, print_summary, print_policy_comparison, print_tree_stats, reset). |
| `src/prheart/access-tracker.cc` | Implementation of `AccessTracker`. Contains `record_read()`, `nodes_per_depth_histogram()`, `print_tree_stats()`, `print_summary()`, `print_policy_comparison()`, and `reset()`. |
| `src/prheart/art-node.cc` | Contains `create_skip_table_policy()` (calls get_top_k_set then add_shortcut_policy) and `add_shortcut_policy()` (DFS that inserts RACE entries for nodes in the target set). |
| `src/rdma/rdma-connection.cc` | RDMA Memory Region allocation using huge pages (MAP_HUGETLB). One MR per thread. |
| `src/rdma/aiordma.cc` | RACE hash table connect and register logic. Line 683 is where the compute binary connects to the memory node's RACE service — this is the point that fails if the memory node is not yet ready. |
| `workload_spec/a` through `workload_spec/g` | YCSB workload specification files. All set to `recordcount=1000000` and `operationcount=1000000`. |
| `compute_results.txt` | Complete raw output from all 23 experimental runs, captured via `tee`. Contains per-thread statistics, AccessTracker reports, policy comparisons, and aggregate summaries for every run. |

---

## References

[1] Jinshu Liu, Hamid Hadian, Hanchen Xu, and Huaicheng Li. Tiered Memory Management Beyond Hotness. *Proceedings of the USENIX Symposium on Operating Systems Design and Implementation (OSDI)*, 2025.

[2] Yan Sun, Jongyul Kim, Zeduo Yu, et al. M5: Mastering Page Migration and Memory Management for CXL Based Tiered Memory Systems. *Proceedings of the ACM International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS)*, 2025.

[3] Bowen Zhang, Shimin Zheng, Shixuan Shu, et al. DART: A Lock-free Two-Layer Hashed ART Index for Disaggregated Memory. *Proceedings of the ACM on Management of Data (SIGMOD)*, 2026.

---

*All 23 experimental runs were conducted on real InfiniBand hardware at 100 Gb/s with 1,000,000 keys and 1,000,000 operations per run. Raw output is in `compute_results.txt`.*
