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
 * @file SequenceSplitter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Greedy sequence partitioning strategies for linear clustering.
 */

#include "SequenceSplitter.hh"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LinearClusteringTypes.hh"
#include "TopologyConfig.hh"

namespace icts {
namespace {

auto ResolveFanoutLimit(const LinearClusteringConfig& config, std::size_t fallback) -> std::size_t
{
  if (config.max_fanout > 0) {
    return config.max_fanout;
  }
  return std::max<std::size_t>(std::size_t{1}, fallback);
}

auto ResolvePrefixCount(const LinearClusteringConfig& config, std::size_t load_count) -> std::size_t
{
  if (load_count == 0U) {
    return 0U;
  }
  return std::min(load_count, ResolveFanoutLimit(config, load_count));
}

auto BuildInvalidPartition() -> PartitionScore
{
  PartitionScore partition;
  partition.legal = false;
  partition.total_score = std::numeric_limits<double>::infinity();
  return partition;
}

auto BuildRotatedLoads(const std::vector<OrderedLoad>& ordered_loads, std::size_t offset) -> std::vector<OrderedLoad>
{
  if (ordered_loads.empty()) {
    return {};
  }

  const auto load_count = ordered_loads.size();
  const auto normalized_offset = offset % load_count;
  if (normalized_offset == 0U) {
    return ordered_loads;
  }

  std::vector<OrderedLoad> rotated_loads;
  rotated_loads.reserve(load_count);
  rotated_loads.insert(rotated_loads.end(), ordered_loads.begin() + static_cast<std::ptrdiff_t>(normalized_offset), ordered_loads.end());
  rotated_loads.insert(rotated_loads.end(), ordered_loads.begin(), ordered_loads.begin() + static_cast<std::ptrdiff_t>(normalized_offset));
  return rotated_loads;
}

auto ExpandAnchoredSequentialWindows(std::size_t load_count, std::size_t prefix_count, const std::vector<std::size_t>& anchors)
    -> std::vector<std::size_t>
{
  if (load_count == 0U) {
    return {};
  }

  const auto resolved_prefix_count = std::max<std::size_t>(std::size_t{1}, std::min(load_count, prefix_count));
  if (resolved_prefix_count >= load_count) {
    return SequenceSplitter::resolvePrefixOffsets(load_count, load_count);
  }

  std::vector<std::size_t> offsets;
  offsets.reserve(std::min(load_count, anchors.size() * resolved_prefix_count));

  std::unordered_set<std::size_t> seen_offsets;
  seen_offsets.reserve(std::min(load_count, anchors.size() * resolved_prefix_count * 2U));
  for (const auto anchor : anchors) {
    const auto normalized_anchor = anchor % load_count;
    for (std::size_t step = 0; step < resolved_prefix_count; ++step) {
      const auto offset = (normalized_anchor + step) % load_count;
      if (seen_offsets.insert(offset).second) {
        offsets.push_back(offset);
        if (offsets.size() == load_count) {
          return SequenceSplitter::resolvePrefixOffsets(load_count, load_count);
        }
      }
    }
  }

  if (offsets.empty()) {
    offsets.push_back(0U);
  }
  return offsets;
}

}  // namespace

auto SequenceSplitter::resolvePrefixOffsets(std::size_t load_count, std::size_t prefix_count) -> std::vector<std::size_t>
{
  if (load_count == 0U) {
    return {};
  }

  const auto resolved_prefix_count = std::max<std::size_t>(std::size_t{1}, std::min(load_count, prefix_count));
  std::vector<std::size_t> offsets;
  offsets.reserve(resolved_prefix_count);
  for (std::size_t offset = 0; offset < resolved_prefix_count; ++offset) {
    offsets.push_back(offset);
  }
  return offsets;
}

auto SequenceSplitter::resolveStridedOffsets(std::size_t load_count, std::size_t strided_count) -> std::vector<std::size_t>
{
  if (load_count == 0U) {
    return {};
  }

  if (load_count == 1U || strided_count == 0U) {
    return {0U};
  }

  const auto candidate_count = std::min(load_count, strided_count);
  std::vector<std::size_t> offsets;
  offsets.reserve(candidate_count);

  std::unordered_set<std::size_t> seen_offsets;
  seen_offsets.reserve(candidate_count * 2U);
  for (std::size_t index = 0; index < candidate_count; ++index) {
    const auto offset = std::min(load_count - std::size_t{1}, (index * load_count) / candidate_count);
    if (seen_offsets.insert(offset).second) {
      offsets.push_back(offset);
    }
  }

  for (std::size_t offset = 0; offsets.size() < candidate_count && offset < load_count; ++offset) {
    if (seen_offsets.insert(offset).second) {
      offsets.push_back(offset);
    }
  }

  if (offsets.empty()) {
    offsets.push_back(0U);
  }
  return offsets;
}

auto SequenceSplitter::resolveSweepOffsets(std::size_t load_count, const LinearClusteringConfig& config) -> SweepResolution
{
  SweepResolution resolution;
  resolution.requested_mode = config.sweep_mode;
  resolution.effective_mode = config.sweep_mode;
  resolution.prefix_count = ResolvePrefixCount(config, load_count);
  resolution.strided_count = std::min(load_count, config.strided_sweep_count);

  if (load_count == 0U) {
    return resolution;
  }

  const auto prefix_offsets = resolvePrefixOffsets(load_count, resolution.prefix_count);
  const auto strided_offsets = resolveStridedOffsets(load_count, resolution.strided_count);

  switch (config.sweep_mode) {
    case LinearSweepMode::kPrefixSweep:
      resolution.offsets = prefix_offsets;
      break;
    case LinearSweepMode::kStridedSweep:
      resolution.offsets = strided_offsets;
      break;
    case LinearSweepMode::kPrefixAndStridedSweep: {
      // Combined sweep reuses the Sink-style prefix width around every strided anchor.
      // This preserves deterministic locality and naturally expands to the full ring when the
      // anchor windows cover the complete sequence.
      resolution.offsets = ExpandAnchoredSequentialWindows(load_count, resolution.prefix_count, strided_offsets);
      if (resolution.offsets == prefix_offsets) {
        resolution.effective_mode = LinearSweepMode::kPrefixSweep;
        resolution.degraded_to_prefix = true;
      }
      break;
    }
  }

  if (resolution.offsets.empty()) {
    resolution.offsets.push_back(0U);
  }
  return resolution;
}

auto SequenceSplitter::split(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config) -> PartitionScore
{
  if (ordered_loads.empty()) {
    return {};
  }

  const auto sweep_resolution = resolveSweepOffsets(ordered_loads.size(), config);
  const auto& rotation_offsets = sweep_resolution.offsets;

  if (config.split_strategy == LinearSplitStrategy::kForwardGreedy) {
    return splitForwardWithOffsets(ordered_loads, config, rotation_offsets);
  }

  if (config.split_strategy == LinearSplitStrategy::kReverseGreedy) {
    return splitReverseWithOffsets(ordered_loads, config, rotation_offsets);
  }

  return splitBidirectionalWithOffsets(ordered_loads, config, rotation_offsets);
}

auto SequenceSplitter::splitForwardWithOffsets(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                                               const std::vector<std::size_t>& rotation_offsets) -> PartitionScore
{
  PartitionScore best = BuildInvalidPartition();
  const auto update_best = [&best](PartitionScore candidate) -> void {
    if (!candidate.legal) {
      return;
    }
    if (!best.legal || candidate.total_score < best.total_score
        || (candidate.total_score == best.total_score && candidate.rotation_offset < best.rotation_offset)) {
      best = std::move(candidate);
    }
  };

  for (const auto offset : rotation_offsets) {
    auto rotated_loads = BuildRotatedLoads(ordered_loads, offset);
    SegmentScoreCache rotated_score_cache;
    rotated_score_cache.reserve(rotated_loads.size() * 4);
    auto rotated_partition = runForwardGreedy(rotated_loads, 0, rotated_loads.size(), {}, config, rotated_score_cache);
    if (!rotated_partition.legal) {
      continue;
    }
    rotated_partition.rotation_offset = offset;
    update_best(std::move(rotated_partition));
  }
  return best;
}

auto SequenceSplitter::splitReverseWithOffsets(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                                               const std::vector<std::size_t>& rotation_offsets) -> PartitionScore
{
  PartitionScore best = BuildInvalidPartition();
  const auto update_best = [&best](PartitionScore candidate) -> void {
    if (!candidate.legal) {
      return;
    }
    if (!best.legal || candidate.total_score < best.total_score
        || (candidate.total_score == best.total_score && candidate.rotation_offset < best.rotation_offset)) {
      best = std::move(candidate);
    }
  };

  for (const auto offset : rotation_offsets) {
    auto rotated_loads = BuildRotatedLoads(ordered_loads, offset);
    SegmentScoreCache rotated_score_cache;
    rotated_score_cache.reserve(rotated_loads.size() * 4);
    auto rotated_partition = runReverseGreedy(rotated_loads, 0, rotated_loads.size(), {}, config, rotated_score_cache);
    if (!rotated_partition.legal) {
      continue;
    }
    rotated_partition.rotation_offset = offset;
    update_best(std::move(rotated_partition));
  }
  return best;
}

auto SequenceSplitter::splitBidirectionalWithOffsets(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                                                     const std::vector<std::size_t>& rotation_offsets) -> PartitionScore
{
  PartitionScore best = BuildInvalidPartition();
  const auto update_best = [&best](PartitionScore candidate) -> void {
    if (!candidate.legal) {
      return;
    }
    if (!best.legal || candidate.total_score < best.total_score
        || (candidate.total_score == best.total_score && candidate.rotation_offset < best.rotation_offset)) {
      best = std::move(candidate);
    }
  };

  for (const auto offset : rotation_offsets) {
    auto rotated_loads = BuildRotatedLoads(ordered_loads, offset);
    SegmentScoreCache rotated_score_cache;
    rotated_score_cache.reserve(rotated_loads.size() * 4);
    auto rotated_partition = runBidirectionalGreedy(rotated_loads, 0, rotated_loads.size(), {}, config, rotated_score_cache);
    if (!rotated_partition.legal) {
      continue;
    }
    rotated_partition.rotation_offset = offset;
    update_best(std::move(rotated_partition));
  }
  return best;
}

auto SequenceSplitter::evaluateSegmentCached(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment,
                                             const LinearClusteringConfig& config, bool need_exact_cap,
                                             SequenceSplitter::SegmentScoreCache& score_cache) -> const ClusterScoreBreakdown&
{
  const SegmentCacheKey key{.begin = segment.begin, .end = segment.end};
  auto iter = score_cache.find(key);
  if (iter != score_cache.end()) {
    return iter->second;
  }

  auto score = _clustering_evaluator.evaluateSegment(ordered_loads, segment, config, need_exact_cap);
  auto [inserted_iter, insert_ok] = score_cache.emplace(key, score);
  (void) insert_ok;
  return inserted_iter->second;
}

auto SequenceSplitter::buildPartition(const std::vector<OrderedLoad>& ordered_loads, std::vector<SegmentRange> segments,
                                      const LinearClusteringConfig& config, bool need_exact_cap,
                                      SequenceSplitter::SegmentScoreCache& score_cache) -> PartitionScore
{
  if (segments.empty()) {
    return BuildInvalidPartition();
  }

  std::ranges::sort(segments, [](const SegmentRange& lhs, const SegmentRange& rhs) -> bool {
    if (lhs.begin != rhs.begin) {
      return lhs.begin < rhs.begin;
    }
    return lhs.end < rhs.end;
  });

  std::size_t cursor = 0;
  for (const auto& segment : segments) {
    if (segment.begin != cursor || segment.end > ordered_loads.size() || segment.isEmpty()) {
      return BuildInvalidPartition();
    }
    cursor = segment.end;
  }
  if (cursor != ordered_loads.size()) {
    return BuildInvalidPartition();
  }

  PartitionScore partition;
  partition.legal = true;
  partition.total_score = 0.0;
  partition.segments = segments;
  partition.clusters.reserve(segments.size());

  for (const auto& segment : segments) {
    const auto& score = evaluateSegmentCached(ordered_loads, segment, config, need_exact_cap, score_cache);
    partition.clusters.push_back(score);
    if (!score.legal) {
      partition.legal = false;
      partition.total_score = std::numeric_limits<double>::infinity();
      return partition;
    }
    partition.total_score += score.total_score;
  }

  return partition;
}

auto SequenceSplitter::findForwardCandidate(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t range_end,
                                            const LinearClusteringConfig& config, SequenceSplitter::SegmentScoreCache& score_cache)
    -> SegmentCandidate
{
  SegmentCandidate candidate;
  if (begin >= range_end) {
    return candidate;
  }

  const auto fanout_limit = ResolveFanoutLimit(config, range_end - begin);
  const auto max_end = std::min(range_end, begin + fanout_limit);
  for (auto end = max_end; end > begin; --end) {
    const SegmentRange segment{.begin = begin, .end = end};
    const auto& score = evaluateSegmentCached(ordered_loads, segment, config, config.enable_exact_cap, score_cache);
    if (!score.legal) {
      continue;
    }
    candidate.valid = true;
    candidate.segment = segment;
    candidate.score = score;
    return candidate;
  }

  return candidate;
}

auto SequenceSplitter::findBackwardCandidate(const std::vector<OrderedLoad>& ordered_loads, std::size_t range_begin, std::size_t end,
                                             const LinearClusteringConfig& config, SequenceSplitter::SegmentScoreCache& score_cache)
    -> SegmentCandidate
{
  SegmentCandidate candidate;
  if (range_begin >= end) {
    return candidate;
  }

  const auto fanout_limit = ResolveFanoutLimit(config, end - range_begin);
  const auto min_begin = end - std::min(fanout_limit, end - range_begin);
  for (auto begin = min_begin; begin < end; ++begin) {
    const SegmentRange segment{.begin = begin, .end = end};
    const auto& score = evaluateSegmentCached(ordered_loads, segment, config, config.enable_exact_cap, score_cache);
    if (!score.legal) {
      continue;
    }
    candidate.valid = true;
    candidate.segment = segment;
    candidate.score = score;
    return candidate;
  }

  return candidate;
}

auto SequenceSplitter::runForwardGreedy(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t end,
                                        std::vector<SegmentRange> prefix_segments, const LinearClusteringConfig& config,
                                        SequenceSplitter::SegmentScoreCache& score_cache) -> PartitionScore
{
  auto segments = std::move(prefix_segments);
  auto cursor = begin;
  while (cursor < end) {
    auto candidate = findForwardCandidate(ordered_loads, cursor, end, config, score_cache);
    if (!candidate.valid) {
      return BuildInvalidPartition();
    }
    segments.push_back(candidate.segment);
    cursor = candidate.segment.end;
  }
  return buildPartition(ordered_loads, std::move(segments), config, config.enable_exact_cap, score_cache);
}

auto SequenceSplitter::runReverseGreedy(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t end,
                                        std::vector<SegmentRange> suffix_segments, const LinearClusteringConfig& config,
                                        SequenceSplitter::SegmentScoreCache& score_cache) -> PartitionScore
{
  auto segments = std::move(suffix_segments);
  auto cursor = end;
  while (cursor > begin) {
    auto candidate = findBackwardCandidate(ordered_loads, begin, cursor, config, score_cache);
    if (!candidate.valid) {
      return BuildInvalidPartition();
    }
    segments.push_back(candidate.segment);
    cursor = candidate.segment.begin;
  }
  return buildPartition(ordered_loads, std::move(segments), config, config.enable_exact_cap, score_cache);
}

auto SequenceSplitter::runBidirectionalGreedy(const std::vector<OrderedLoad>& ordered_loads, std::size_t begin, std::size_t end,
                                              std::vector<SegmentRange> seed_segments, const LinearClusteringConfig& config,
                                              SequenceSplitter::SegmentScoreCache& score_cache) -> PartitionScore
{
  std::vector<SegmentRange> front_segments;
  std::vector<SegmentRange> back_segments;

  auto left = begin;
  auto right = end;
  while (left < right) {
    auto front_candidate = findForwardCandidate(ordered_loads, left, right, config, score_cache);
    auto back_candidate = findBackwardCandidate(ordered_loads, left, right, config, score_cache);
    if (!front_candidate.valid && !back_candidate.valid) {
      return BuildInvalidPartition();
    }

    bool choose_front = false;
    if (front_candidate.valid && !back_candidate.valid) {
      choose_front = true;
    } else if (!front_candidate.valid && back_candidate.valid) {
      choose_front = false;
    } else {
      const auto front_size = front_candidate.segment.size();
      const auto back_size = back_candidate.segment.size();
      if (front_candidate.score.total_score != back_candidate.score.total_score) {
        choose_front = front_candidate.score.total_score < back_candidate.score.total_score;
      } else if (front_size != back_size) {
        choose_front = front_size >= back_size;
      } else {
        choose_front = true;
      }
    }

    if (choose_front) {
      front_segments.push_back(front_candidate.segment);
      left = front_candidate.segment.end;
    } else {
      back_segments.push_back(back_candidate.segment);
      right = back_candidate.segment.begin;
    }
  }

  auto segments = std::move(seed_segments);
  segments.insert(segments.end(), front_segments.begin(), front_segments.end());
  segments.insert(segments.end(), back_segments.begin(), back_segments.end());
  return buildPartition(ordered_loads, std::move(segments), config, config.enable_exact_cap, score_cache);
}

}  // namespace icts
