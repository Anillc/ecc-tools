// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file SequenceSplitter.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Greedy sequence partitioning strategies for linear clustering.
 *
 * Sweep semantics:
 * - prefix sweep: evaluate Sink-style prefix offsets [0, ..., prefix_count - 1]
 * - strided sweep: evaluate uniformly sampled circular offsets over the full 1D ring
 * - combined sweep: expand each strided anchor into a prefix-length sequential window, de-duplicate the
 *   union, and normalize to the full ring when the anchor windows cover every rotation
 */

#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "ClusteringEvaluator.hh"
#include "LinearClusteringTypes.hh"
#include "TopologyConfig.hh"

namespace icts {

struct SweepResolution
{
  LinearSweepMode requested_mode = LinearSweepMode::kPrefixSweep;
  LinearSweepMode effective_mode = LinearSweepMode::kPrefixSweep;
  std::size_t prefix_count = 0;
  std::size_t strided_count = 0;
  bool degraded_to_prefix = false;
  std::vector<std::size_t> offsets;
};

class SequenceSplitter
{
 public:
  SequenceSplitter() = default;
  ~SequenceSplitter() = default;

  static auto resolvePrefixOffsets(std::size_t load_count, std::size_t prefix_count) -> std::vector<std::size_t>;
  static auto resolveStridedOffsets(std::size_t load_count, std::size_t strided_count) -> std::vector<std::size_t>;
  static auto resolveSweepOffsets(std::size_t load_count, const LinearClusteringConfig& config) -> SweepResolution;
  auto split(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config) -> PartitionScore;

 private:
  struct SegmentCandidate
  {
    bool valid = false;
    SegmentRange segment;
    ClusterScoreBreakdown score;
  };

  using SegmentScoreCache = std::unordered_map<SegmentCacheKey, ClusterScoreBreakdown, SegmentCacheKeyHash>;

  auto evaluateSegmentCached(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment,
                             const LinearClusteringConfig& config, bool need_exact_cap, SegmentScoreCache& score_cache)
      -> const ClusterScoreBreakdown&;
  auto buildPartition(const std::vector<OrderedLoad>& ordered_loads, std::vector<SegmentRange> segments,
                      const LinearClusteringConfig& config, bool need_exact_cap, SegmentScoreCache& score_cache) -> PartitionScore;

  auto findForwardCandidate(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t range_end,
                            const LinearClusteringConfig& config, SegmentScoreCache& score_cache) -> SegmentCandidate;
  auto findBackwardCandidate(const std::vector<OrderedLoad>& ordered_loads, std::size_t range_begin, std::size_t end,
                             const LinearClusteringConfig& config, SegmentScoreCache& score_cache) -> SegmentCandidate;

  auto runForwardGreedy(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t end,
                        std::vector<SegmentRange> prefix_segments, const LinearClusteringConfig& config, SegmentScoreCache& score_cache)
      -> PartitionScore;
  auto runReverseGreedy(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t end,
                        std::vector<SegmentRange> suffix_segments, const LinearClusteringConfig& config, SegmentScoreCache& score_cache)
      -> PartitionScore;
  auto runBidirectionalGreedy(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t end,
                              std::vector<SegmentRange> seed_segments, const LinearClusteringConfig& config, SegmentScoreCache& score_cache)
      -> PartitionScore;
  auto splitForwardWithOffsets(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                               const std::vector<std::size_t>& rotation_offsets) -> PartitionScore;
  auto splitReverseWithOffsets(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                               const std::vector<std::size_t>& rotation_offsets) -> PartitionScore;
  auto splitBidirectionalWithOffsets(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                                     const std::vector<std::size_t>& rotation_offsets) -> PartitionScore;

  ClusteringEvaluator _clustering_evaluator;
};

}  // namespace icts
