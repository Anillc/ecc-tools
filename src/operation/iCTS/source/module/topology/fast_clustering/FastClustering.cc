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
 * @file FastClustering.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Fast spatial clustering implementation for topology clustering.
 */

#include "FastClustering.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Clustering.hh"
#include "ConstraintEvaluator.hh"
#include "LinearClustering.hh"
#include "LinearClusteringTypes.hh"
#include "Log.hh"
#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"

namespace icts {
namespace {

constexpr std::size_t kDefaultPackingFanout = 32;
constexpr std::size_t kMergeRoundCount = 2;
constexpr std::size_t kSplitCandidateWindow = 4;
constexpr std::size_t kMaxMergeNeighborCandidates = 12;
constexpr std::size_t kBoundaryPolishRoundCount = 2;
constexpr std::size_t kMaxBoundaryNeighborCandidates = 8;
constexpr std::size_t kMaxBoundaryEntryCandidates = 8;
constexpr double kSplitRoutingCapBalanceWeight = 0.25;
constexpr double kMergeRoutingCapBalanceWeight = 0.18;
constexpr double kBoundaryRoutingCapBalanceWeight = 0.35;
constexpr double kScoreEpsilon = 1e-9;

struct LoadEntry
{
  Pin* pin = nullptr;
  Point<int> location = Point<int>(0, 0);
  std::size_t original_index = 0;
};

struct Bounds
{
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
};

struct ClusterDraft
{
  std::vector<std::size_t> entry_ids;
  Bounds bounds;
  double routing_cap_proxy = 0.0;
  bool active = true;
};

struct DraftAggregate
{
  std::size_t active_count = 0;
  double total_routing_cap_proxy = 0.0;
};

auto IsEmpty(const Bounds& bounds) -> bool
{
  return bounds.min_x > bounds.max_x || bounds.min_y > bounds.max_y;
}

auto ExtendBounds(Bounds bounds, const Point<int>& point) -> Bounds
{
  bounds.min_x = std::min(bounds.min_x, point.get_x());
  bounds.min_y = std::min(bounds.min_y, point.get_y());
  bounds.max_x = std::max(bounds.max_x, point.get_x());
  bounds.max_y = std::max(bounds.max_y, point.get_y());
  return bounds;
}

auto CalcDiameter(const Bounds& bounds) -> int
{
  if (IsEmpty(bounds)) {
    return 0;
  }
  return (bounds.max_x - bounds.min_x) + (bounds.max_y - bounds.min_y);
}

auto CalcClusterBounds(const std::vector<Pin*>& cluster) -> Bounds
{
  Bounds bounds;
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    bounds = ExtendBounds(bounds, pin->get_location());
  }
  return bounds;
}

auto CalcClusterBounds(const std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries) -> Bounds
{
  Bounds bounds;
  for (const auto entry_id : entry_ids) {
    bounds = ExtendBounds(bounds, entries.at(entry_id).location);
  }
  return bounds;
}

auto CalcManhattanDistance(const Point<int>& lhs, const Point<int>& rhs) -> double
{
  const auto dx = std::abs(static_cast<long long>(lhs.get_x()) - static_cast<long long>(rhs.get_x()));
  const auto dy = std::abs(static_cast<long long>(lhs.get_y()) - static_cast<long long>(rhs.get_y()));
  return static_cast<double>(dx + dy);
}

auto CalcEntryCenter(const std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries) -> Point<int>
{
  if (entry_ids.empty()) {
    return {0, 0};
  }

  long long x_sum = 0;
  long long y_sum = 0;
  for (const auto entry_id : entry_ids) {
    const auto location = entries.at(entry_id).location;
    x_sum += location.get_x();
    y_sum += location.get_y();
  }
  return {static_cast<int>(std::lround(static_cast<double>(x_sum) / static_cast<double>(entry_ids.size()))),
          static_cast<int>(std::lround(static_cast<double>(y_sum) / static_cast<double>(entry_ids.size())))};
}

auto CalcEntryMedian(const std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries) -> Point<int>
{
  std::vector<int> x_coords;
  std::vector<int> y_coords;
  x_coords.reserve(entry_ids.size());
  y_coords.reserve(entry_ids.size());
  for (const auto entry_id : entry_ids) {
    const auto location = entries.at(entry_id).location;
    x_coords.push_back(location.get_x());
    y_coords.push_back(location.get_y());
  }
  if (x_coords.empty()) {
    return {0, 0};
  }

  const auto middle = static_cast<std::ptrdiff_t>(x_coords.size() / 2U);
  auto x_middle = x_coords.begin() + middle;
  auto y_middle = y_coords.begin() + middle;
  std::nth_element(x_coords.begin(), x_middle, x_coords.end());
  std::nth_element(y_coords.begin(), y_middle, y_coords.end());
  return {*x_middle, *y_middle};
}

auto ResolveDraftRoot(const std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries,
                      const LinearClusteringConfig& config) -> Point<int>
{
  return config.root_policy == LinearRootPolicy::kCenter ? CalcEntryCenter(entry_ids, entries) : CalcEntryMedian(entry_ids, entries);
}

auto CalcRoutingCapProxy(const std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries,
                         const LinearClusteringConfig& config) -> double
{
  if (entry_ids.size() <= 1U) {
    return 0.0;
  }

  const auto root = ResolveDraftRoot(entry_ids, entries, config);
  double proxy = 0.0;
  for (const auto entry_id : entry_ids) {
    proxy += CalcManhattanDistance(root, entries.at(entry_id).location);
  }
  return proxy;
}

auto BuildDraft(std::vector<std::size_t> entry_ids, const std::vector<LoadEntry>& entries, const LinearClusteringConfig& config)
    -> ClusterDraft
{
  auto bounds = CalcClusterBounds(entry_ids, entries);
  auto routing_cap_proxy = CalcRoutingCapProxy(entry_ids, entries, config);
  return ClusterDraft{
      .entry_ids = std::move(entry_ids),
      .bounds = bounds,
      .routing_cap_proxy = routing_cap_proxy,
  };
}

auto CalcCenter(const std::vector<Pin*>& cluster) -> Point<int>
{
  if (cluster.empty()) {
    return {0, 0};
  }

  long long x_sum = 0;
  long long y_sum = 0;
  std::size_t pin_count = 0;
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    x_sum += location.get_x();
    y_sum += location.get_y();
    ++pin_count;
  }
  if (pin_count == 0U) {
    return {0, 0};
  }
  return {static_cast<int>(std::lround(static_cast<double>(x_sum) / static_cast<double>(pin_count))),
          static_cast<int>(std::lround(static_cast<double>(y_sum) / static_cast<double>(pin_count)))};
}

auto CalcMedian(const std::vector<Pin*>& cluster) -> Point<int>
{
  std::vector<int> x_coords;
  std::vector<int> y_coords;
  x_coords.reserve(cluster.size());
  y_coords.reserve(cluster.size());
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    x_coords.push_back(location.get_x());
    y_coords.push_back(location.get_y());
  }
  if (x_coords.empty()) {
    return {0, 0};
  }

  const auto middle = static_cast<std::ptrdiff_t>(x_coords.size() / 2U);
  auto x_middle = x_coords.begin() + middle;
  auto y_middle = y_coords.begin() + middle;
  std::nth_element(x_coords.begin(), x_middle, x_coords.end());
  std::nth_element(y_coords.begin(), y_middle, y_coords.end());
  return {*x_middle, *y_middle};
}

auto ResolveEvaluationRoot(const std::vector<Pin*>& cluster, const LinearClusteringConfig& config) -> Point<int>
{
  return config.root_policy == LinearRootPolicy::kCenter ? CalcCenter(cluster) : CalcMedian(cluster);
}

auto ResolvePackingFanoutLimit(const LinearClusteringConfig& config, std::size_t load_count) -> std::size_t
{
  if (load_count == 0U) {
    return 0U;
  }
  if (config.max_fanout > 0U) {
    return std::min(config.max_fanout, load_count);
  }
  return std::min<std::size_t>(load_count, kDefaultPackingFanout);
}

auto IsFanoutLegal(std::size_t fanout, const LinearClusteringConfig& config) -> bool
{
  return config.max_fanout == 0U || fanout <= config.max_fanout;
}

auto IsDiameterLegal(const Bounds& bounds, const LinearClusteringConfig& config) -> bool
{
  return config.max_diameter <= 0 || CalcDiameter(bounds) <= config.max_diameter;
}

auto IsDraftGeometryLegal(const ClusterDraft& draft, const LinearClusteringConfig& config) -> bool
{
  return IsFanoutLegal(draft.entry_ids.size(), config) && IsDiameterLegal(draft.bounds, config);
}

auto ClusterScoreProxy(const ClusterDraft& draft, const LinearClusteringConfig& config) -> double
{
  if (draft.entry_ids.empty()) {
    return std::numeric_limits<double>::infinity();
  }

  const auto diameter = CalcDiameter(draft.bounds);
  if (config.scoring_strategy == LinearScoringStrategy::kTotalWirelength) {
    return config.wirelength_weight * static_cast<double>(diameter);
  }
  if (diameter > 0) {
    return static_cast<double>(diameter);
  }
  return config.max_diameter > 0 ? static_cast<double>(config.max_diameter) : 0.0;
}

auto CalcRoutingCapVariancePenalty(double routing_cap_proxy, double target_routing_cap_proxy) -> double
{
  const auto safe_target = std::max(1.0, target_routing_cap_proxy);
  const auto delta = routing_cap_proxy - target_routing_cap_proxy;
  return delta * delta / safe_target;
}

auto DraftObjective(const ClusterDraft& draft, const LinearClusteringConfig& config, double target_routing_cap_proxy,
                    double routing_cap_balance_weight) -> double
{
  return ClusterScoreProxy(draft, config)
         + routing_cap_balance_weight * CalcRoutingCapVariancePenalty(draft.routing_cap_proxy, target_routing_cap_proxy);
}

auto CalcDraftAggregate(const std::vector<ClusterDraft>& drafts) -> DraftAggregate
{
  DraftAggregate aggregate;
  for (const auto& draft : drafts) {
    if (!draft.active || draft.entry_ids.empty()) {
      continue;
    }
    ++aggregate.active_count;
    aggregate.total_routing_cap_proxy += draft.routing_cap_proxy;
  }
  return aggregate;
}

auto CalcMeanRoutingCapProxy(const DraftAggregate& aggregate) -> double
{
  if (aggregate.active_count == 0U) {
    return 0.0;
  }
  return aggregate.total_routing_cap_proxy / static_cast<double>(aggregate.active_count);
}

auto CalcBoundsDistance(const Bounds& lhs, const Bounds& rhs) -> long long
{
  if (IsEmpty(lhs) || IsEmpty(rhs)) {
    return 0;
  }

  long long dx = 0;
  if (lhs.max_x < rhs.min_x) {
    dx = static_cast<long long>(rhs.min_x) - lhs.max_x;
  } else if (rhs.max_x < lhs.min_x) {
    dx = static_cast<long long>(lhs.min_x) - rhs.max_x;
  }

  long long dy = 0;
  if (lhs.max_y < rhs.min_y) {
    dy = static_cast<long long>(rhs.min_y) - lhs.max_y;
  } else if (rhs.max_y < lhs.min_y) {
    dy = static_cast<long long>(lhs.min_y) - rhs.max_y;
  }
  return dx + dy;
}

auto CollectEntries(const std::vector<Pin*>& loads) -> std::vector<LoadEntry>
{
  std::vector<LoadEntry> entries;
  entries.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    entries.push_back(LoadEntry{.pin = pin, .location = pin->get_location(), .original_index = index});
  }
  return entries;
}

auto DistanceToBounds(const Point<int>& point, const Bounds& bounds) -> long long
{
  if (IsEmpty(bounds)) {
    return 0;
  }

  long long dx = 0;
  if (point.get_x() < bounds.min_x) {
    dx = static_cast<long long>(bounds.min_x) - point.get_x();
  } else if (point.get_x() > bounds.max_x) {
    dx = static_cast<long long>(point.get_x()) - bounds.max_x;
  }

  long long dy = 0;
  if (point.get_y() < bounds.min_y) {
    dy = static_cast<long long>(bounds.min_y) - point.get_y();
  } else if (point.get_y() > bounds.max_y) {
    dy = static_cast<long long>(point.get_y()) - bounds.max_y;
  }
  return dx + dy;
}

auto SortEntryIdsByLongestAxis(std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries, const Bounds& bounds) -> void
{
  const bool split_by_x = (bounds.max_x - bounds.min_x) >= (bounds.max_y - bounds.min_y);
  std::ranges::sort(entry_ids, [&entries, split_by_x](std::size_t lhs, std::size_t rhs) -> bool {
    const auto lhs_location = entries.at(lhs).location;
    const auto rhs_location = entries.at(rhs).location;
    const auto lhs_primary = split_by_x ? lhs_location.get_x() : lhs_location.get_y();
    const auto rhs_primary = split_by_x ? rhs_location.get_x() : rhs_location.get_y();
    if (lhs_primary != rhs_primary) {
      return lhs_primary < rhs_primary;
    }
    const auto lhs_secondary = split_by_x ? lhs_location.get_y() : lhs_location.get_x();
    const auto rhs_secondary = split_by_x ? rhs_location.get_y() : rhs_location.get_x();
    if (lhs_secondary != rhs_secondary) {
      return lhs_secondary < rhs_secondary;
    }
    return entries.at(lhs).original_index < entries.at(rhs).original_index;
  });
}

auto ResolveTargetClusterCount(std::size_t entry_count, std::size_t fanout_limit) -> std::size_t
{
  const auto safe_fanout = std::max<std::size_t>(1U, fanout_limit);
  return (entry_count + safe_fanout - 1U) / safe_fanout;
}

auto ResolveRecursiveChildClusterCount(std::size_t entry_count, std::size_t fanout_limit, const Bounds& bounds,
                                       const LinearClusteringConfig& config) -> std::size_t
{
  auto target_cluster_count = ResolveTargetClusterCount(entry_count, fanout_limit);
  if (target_cluster_count <= 1U && !IsDiameterLegal(bounds, config)) {
    target_cluster_count = 2U;
  }
  return target_cluster_count;
}

auto CalcSizeDistance(std::size_t lhs, std::size_t rhs) -> std::size_t
{
  return lhs > rhs ? lhs - rhs : rhs - lhs;
}

auto ResolveRecursiveSplitSize(const std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries, std::size_t fanout_limit,
                               const Bounds& bounds, const LinearClusteringConfig& config) -> std::size_t
{
  const auto entry_count = entry_ids.size();
  auto target_cluster_count = ResolveRecursiveChildClusterCount(entry_count, fanout_limit, bounds, config);

  const auto left_cluster_count = std::max<std::size_t>(1U, target_cluster_count / 2U);
  const auto ideal_split_size = std::clamp<std::size_t>(
      (entry_count * left_cluster_count + target_cluster_count - 1U) / target_cluster_count, 1U, entry_count - 1U);
  const auto split_begin = ideal_split_size > kSplitCandidateWindow ? ideal_split_size - kSplitCandidateWindow : 1U;
  const auto split_end = std::min(entry_count - 1U, ideal_split_size + kSplitCandidateWindow);

  std::size_t best_split_size = ideal_split_size;
  std::size_t best_split_distance = std::numeric_limits<std::size_t>::max();
  double best_score = std::numeric_limits<double>::infinity();
  for (auto split_size = split_begin; split_size <= split_end; ++split_size) {
    std::vector<std::size_t> lhs_ids(entry_ids.begin(), entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size));
    std::vector<std::size_t> rhs_ids(entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size), entry_ids.end());
    const auto lhs = BuildDraft(std::move(lhs_ids), entries, config);
    const auto rhs = BuildDraft(std::move(rhs_ids), entries, config);
    const auto lhs_child_count = ResolveRecursiveChildClusterCount(lhs.entry_ids.size(), fanout_limit, lhs.bounds, config);
    const auto rhs_child_count = ResolveRecursiveChildClusterCount(rhs.entry_ids.size(), fanout_limit, rhs.bounds, config);
    const auto child_count = std::max<std::size_t>(1U, lhs_child_count + rhs_child_count);
    const auto target_routing_cap_proxy = (lhs.routing_cap_proxy + rhs.routing_cap_proxy) / static_cast<double>(child_count);
    const auto lhs_avg_proxy = lhs.routing_cap_proxy / static_cast<double>(std::max<std::size_t>(1U, lhs_child_count));
    const auto rhs_avg_proxy = rhs.routing_cap_proxy / static_cast<double>(std::max<std::size_t>(1U, rhs_child_count));
    const auto score
        = ClusterScoreProxy(lhs, config) + ClusterScoreProxy(rhs, config)
          + kSplitRoutingCapBalanceWeight
                * (static_cast<double>(lhs_child_count) * CalcRoutingCapVariancePenalty(lhs_avg_proxy, target_routing_cap_proxy)
                   + static_cast<double>(rhs_child_count) * CalcRoutingCapVariancePenalty(rhs_avg_proxy, target_routing_cap_proxy));
    const auto split_distance = CalcSizeDistance(split_size, ideal_split_size);
    if (score + kScoreEpsilon < best_score || (std::abs(score - best_score) <= kScoreEpsilon && split_distance < best_split_distance)) {
      best_score = score;
      best_split_size = split_size;
      best_split_distance = split_distance;
    }
  }
  return best_split_size;
}

// NOLINTNEXTLINE(misc-no-recursion): recursive bisection depth is bounded by load count and keeps split logic local.
auto BuildSpatialRecursiveClusters(std::vector<std::size_t> entry_ids, const std::vector<LoadEntry>& entries,
                                   const LinearClusteringConfig& config, std::size_t fanout_limit, std::vector<ClusterDraft>& clusters)
    -> void
{
  if (entry_ids.empty()) {
    return;
  }

  const auto bounds = CalcClusterBounds(entry_ids, entries);
  if (entry_ids.size() <= fanout_limit && IsDiameterLegal(bounds, config)) {
    clusters.push_back(BuildDraft(std::move(entry_ids), entries, config));
    return;
  }

  if (entry_ids.size() == 1U) {
    clusters.push_back(BuildDraft(std::move(entry_ids), entries, config));
    return;
  }

  SortEntryIdsByLongestAxis(entry_ids, entries, bounds);
  const auto split_size = ResolveRecursiveSplitSize(entry_ids, entries, fanout_limit, bounds, config);
  std::vector<std::size_t> lhs(entry_ids.begin(), entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size));
  std::vector<std::size_t> rhs(entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size), entry_ids.end());
  BuildSpatialRecursiveClusters(std::move(lhs), entries, config, fanout_limit, clusters);
  BuildSpatialRecursiveClusters(std::move(rhs), entries, config, fanout_limit, clusters);
}

auto BuildSpatialRecursiveClusters(const std::vector<LoadEntry>& entries, const LinearClusteringConfig& config) -> std::vector<ClusterDraft>
{
  const auto fanout_limit = ResolvePackingFanoutLimit(config, entries.size());
  std::vector<std::size_t> entry_ids(entries.size());
  std::iota(entry_ids.begin(), entry_ids.end(), 0U);

  std::vector<ClusterDraft> clusters;
  clusters.reserve(ResolveTargetClusterCount(entries.size(), fanout_limit));
  BuildSpatialRecursiveClusters(std::move(entry_ids), entries, config, fanout_limit, clusters);
  return clusters;
}

auto BuildMergedDraft(const ClusterDraft& lhs, const ClusterDraft& rhs, const std::vector<LoadEntry>& entries,
                      const LinearClusteringConfig& config) -> ClusterDraft
{
  std::vector<std::size_t> merged_entry_ids;
  merged_entry_ids.reserve(lhs.entry_ids.size() + rhs.entry_ids.size());
  merged_entry_ids.insert(merged_entry_ids.end(), lhs.entry_ids.begin(), lhs.entry_ids.end());
  merged_entry_ids.insert(merged_entry_ids.end(), rhs.entry_ids.begin(), rhs.entry_ids.end());
  return BuildDraft(std::move(merged_entry_ids), entries, config);
}

auto SelectNearestActiveNeighbors(std::size_t cluster_id, const std::vector<ClusterDraft>& clusters, std::size_t max_candidate_count)
    -> std::vector<std::size_t>
{
  struct NeighborCandidate
  {
    std::size_t cluster_id = 0;
    long long distance = 0;
  };

  std::vector<NeighborCandidate> candidates;
  candidates.reserve(clusters.size());
  const auto& cluster = clusters.at(cluster_id);
  for (std::size_t neighbor_id = 0; neighbor_id < clusters.size(); ++neighbor_id) {
    if (neighbor_id == cluster_id || !clusters.at(neighbor_id).active || clusters.at(neighbor_id).entry_ids.empty()) {
      continue;
    }
    candidates.push_back(NeighborCandidate{
        .cluster_id = neighbor_id,
        .distance = CalcBoundsDistance(cluster.bounds, clusters.at(neighbor_id).bounds),
    });
  }

  std::ranges::sort(candidates, [](const NeighborCandidate& lhs, const NeighborCandidate& rhs) -> bool {
    if (lhs.distance != rhs.distance) {
      return lhs.distance < rhs.distance;
    }
    return lhs.cluster_id < rhs.cluster_id;
  });

  std::vector<std::size_t> neighbor_ids;
  neighbor_ids.reserve(std::min(max_candidate_count, candidates.size()));
  for (const auto& candidate : candidates) {
    if (neighbor_ids.size() == max_candidate_count) {
      break;
    }
    neighbor_ids.push_back(candidate.cluster_id);
  }
  return neighbor_ids;
}

auto CanUseMergedDraft(const ClusterDraft& lhs, const ClusterDraft& rhs, const ClusterDraft& merged, const LinearClusteringConfig& config)
    -> bool
{
  return lhs.active && rhs.active && !lhs.entry_ids.empty() && !rhs.entry_ids.empty() && IsDraftGeometryLegal(merged, config);
}

auto MergeDrafts(ClusterDraft& target, ClusterDraft& source, const std::vector<LoadEntry>& entries, const LinearClusteringConfig& config)
    -> void
{
  auto merged = BuildMergedDraft(target, source, entries, config);
  target.entry_ids = std::move(merged.entry_ids);
  target.bounds = merged.bounds;
  target.routing_cap_proxy = merged.routing_cap_proxy;
  target.active = true;
  source.entry_ids.clear();
  source.routing_cap_proxy = 0.0;
  source.active = false;
}

auto TryBuildDraftAfterMove(const ClusterDraft& source, const ClusterDraft& target, std::size_t moved_entry_id,
                            const std::vector<LoadEntry>& entries, const LinearClusteringConfig& config, ClusterDraft& source_after,
                            ClusterDraft& target_after) -> bool
{
  if (source.entry_ids.size() <= 1U) {
    return false;
  }

  bool found_source_entry = false;
  std::vector<std::size_t> source_ids;
  source_ids.reserve(source.entry_ids.size() - 1U);
  for (const auto entry_id : source.entry_ids) {
    if (entry_id == moved_entry_id) {
      found_source_entry = true;
      continue;
    }
    source_ids.push_back(entry_id);
  }
  if (!found_source_entry || source_ids.empty()) {
    return false;
  }

  std::vector<std::size_t> target_ids = target.entry_ids;
  target_ids.push_back(moved_entry_id);
  source_after = BuildDraft(std::move(source_ids), entries, config);
  target_after = BuildDraft(std::move(target_ids), entries, config);
  return IsDraftGeometryLegal(source_after, config) && IsDraftGeometryLegal(target_after, config);
}

auto PairObjective(const ClusterDraft& lhs, const ClusterDraft& rhs, const LinearClusteringConfig& config, double target_routing_cap_proxy,
                   double routing_cap_balance_weight) -> double
{
  return DraftObjective(lhs, config, target_routing_cap_proxy, routing_cap_balance_weight)
         + DraftObjective(rhs, config, target_routing_cap_proxy, routing_cap_balance_weight);
}

auto BuildBoundaryEntryCandidates(const ClusterDraft& source, const ClusterDraft& target, const std::vector<LoadEntry>& entries,
                                  const LinearClusteringConfig& config) -> std::vector<std::size_t>
{
  struct EntryCandidate
  {
    std::size_t entry_id = 0;
    long long distance_to_target = 0;
    double distance_from_source_root = 0.0;
    std::size_t original_index = 0;
  };

  const auto source_root = ResolveDraftRoot(source.entry_ids, entries, config);
  std::vector<EntryCandidate> candidates;
  candidates.reserve(source.entry_ids.size());
  for (const auto entry_id : source.entry_ids) {
    const auto location = entries.at(entry_id).location;
    candidates.push_back(EntryCandidate{
        .entry_id = entry_id,
        .distance_to_target = DistanceToBounds(location, target.bounds),
        .distance_from_source_root = CalcManhattanDistance(location, source_root),
        .original_index = entries.at(entry_id).original_index,
    });
  }

  std::ranges::sort(candidates, [](const EntryCandidate& lhs, const EntryCandidate& rhs) -> bool {
    if (lhs.distance_to_target != rhs.distance_to_target) {
      return lhs.distance_to_target < rhs.distance_to_target;
    }
    if (std::abs(lhs.distance_from_source_root - rhs.distance_from_source_root) > kScoreEpsilon) {
      return lhs.distance_from_source_root > rhs.distance_from_source_root;
    }
    return lhs.original_index < rhs.original_index;
  });

  std::vector<std::size_t> entry_ids;
  entry_ids.reserve(std::min(kMaxBoundaryEntryCandidates, candidates.size()));
  for (const auto& candidate : candidates) {
    if (entry_ids.size() == kMaxBoundaryEntryCandidates) {
      break;
    }
    entry_ids.push_back(candidate.entry_id);
  }
  return entry_ids;
}

struct BoundaryMove
{
  std::size_t target_id = 0;
  ClusterDraft source_after;
  ClusterDraft target_after;
  double score_delta = 0.0;
};

auto FindBestBoundaryMove(std::size_t source_id, const std::vector<ClusterDraft>& clusters, const std::vector<LoadEntry>& entries,
                          const LinearClusteringConfig& config, const DraftAggregate& aggregate) -> std::optional<BoundaryMove>
{
  const auto& source = clusters.at(source_id);
  const auto target_routing_cap_proxy = CalcMeanRoutingCapProxy(aggregate);
  if (!source.active || source.entry_ids.size() <= 1U || source.routing_cap_proxy <= target_routing_cap_proxy) {
    return std::nullopt;
  }

  std::optional<BoundaryMove> best_move;
  const auto neighbor_ids = SelectNearestActiveNeighbors(source_id, clusters, kMaxBoundaryNeighborCandidates);
  for (const auto target_id : neighbor_ids) {
    const auto& target = clusters.at(target_id);
    if (!target.active || target.entry_ids.empty() || target.routing_cap_proxy >= target_routing_cap_proxy) {
      continue;
    }

    const auto entry_candidates = BuildBoundaryEntryCandidates(source, target, entries, config);
    for (const auto moved_entry_id : entry_candidates) {
      ClusterDraft source_after;
      ClusterDraft target_after;
      if (!TryBuildDraftAfterMove(source, target, moved_entry_id, entries, config, source_after, target_after)) {
        continue;
      }

      const auto before_score = PairObjective(source, target, config, target_routing_cap_proxy, kBoundaryRoutingCapBalanceWeight);
      const auto after_total_routing_cap_proxy = aggregate.total_routing_cap_proxy - source.routing_cap_proxy - target.routing_cap_proxy
                                                 + source_after.routing_cap_proxy + target_after.routing_cap_proxy;
      const auto after_target_routing_cap_proxy
          = aggregate.active_count == 0U ? 0.0 : after_total_routing_cap_proxy / static_cast<double>(aggregate.active_count);
      const auto after_score
          = PairObjective(source_after, target_after, config, after_target_routing_cap_proxy, kBoundaryRoutingCapBalanceWeight);
      const auto score_delta = after_score - before_score;
      if (score_delta + kScoreEpsilon >= 0.0) {
        continue;
      }

      if (!best_move.has_value() || score_delta < best_move->score_delta) {
        best_move = BoundaryMove{
            .target_id = target_id,
            .source_after = std::move(source_after),
            .target_after = std::move(target_after),
            .score_delta = score_delta,
        };
      }
    }
  }

  return best_move;
}

auto BuildCapHeavyClusterOrder(const std::vector<ClusterDraft>& clusters) -> std::vector<std::size_t>
{
  std::vector<std::size_t> cluster_order(clusters.size());
  std::iota(cluster_order.begin(), cluster_order.end(), 0U);
  std::ranges::sort(cluster_order, [&clusters](std::size_t lhs, std::size_t rhs) -> bool {
    if (std::abs(clusters.at(lhs).routing_cap_proxy - clusters.at(rhs).routing_cap_proxy) > kScoreEpsilon) {
      return clusters.at(lhs).routing_cap_proxy > clusters.at(rhs).routing_cap_proxy;
    }
    return lhs < rhs;
  });
  return cluster_order;
}

auto PolishBoundaryLoads(std::vector<ClusterDraft>& clusters, const std::vector<LoadEntry>& entries, const LinearClusteringConfig& config)
    -> void
{
  for (std::size_t round = 0; round < kBoundaryPolishRoundCount; ++round) {
    bool changed = false;
    const auto cluster_order = BuildCapHeavyClusterOrder(clusters);
    for (const auto source_id : cluster_order) {
      if (source_id >= clusters.size() || !clusters.at(source_id).active || clusters.at(source_id).entry_ids.size() <= 1U) {
        continue;
      }

      const auto aggregate = CalcDraftAggregate(clusters);
      auto best_move = FindBestBoundaryMove(source_id, clusters, entries, config, aggregate);
      if (!best_move.has_value()) {
        continue;
      }

      clusters.at(source_id) = std::move(best_move->source_after);
      clusters.at(best_move->target_id) = std::move(best_move->target_after);
      changed = true;
    }
    if (!changed) {
      break;
    }
  }
}

auto MergeDraftsIfUseful(std::size_t cluster_id, std::vector<ClusterDraft>& clusters, const std::vector<LoadEntry>& entries,
                         const LinearClusteringConfig& config) -> bool
{
  const auto aggregate = CalcDraftAggregate(clusters);
  const auto before_target_proxy = CalcMeanRoutingCapProxy(aggregate);
  const auto& cluster = clusters.at(cluster_id);
  const auto neighbor_ids = SelectNearestActiveNeighbors(cluster_id, clusters, kMaxMergeNeighborCandidates);

  std::optional<std::size_t> best_neighbor;
  double best_delta = std::numeric_limits<double>::infinity();
  for (const auto neighbor_id : neighbor_ids) {
    const auto& neighbor = clusters.at(neighbor_id);
    const auto merged = BuildMergedDraft(neighbor, cluster, entries, config);
    if (!CanUseMergedDraft(cluster, neighbor, merged, config)) {
      continue;
    }

    const auto after_count = aggregate.active_count > 1U ? aggregate.active_count - 1U : aggregate.active_count;
    const auto after_total_proxy
        = aggregate.total_routing_cap_proxy - cluster.routing_cap_proxy - neighbor.routing_cap_proxy + merged.routing_cap_proxy;
    const auto after_target_proxy = after_count == 0U ? 0.0 : after_total_proxy / static_cast<double>(after_count);
    const auto separate_score = PairObjective(cluster, neighbor, config, before_target_proxy, kMergeRoutingCapBalanceWeight);
    const auto merged_score = DraftObjective(merged, config, after_target_proxy, kMergeRoutingCapBalanceWeight);
    const auto forced_small_merge = cluster.entry_ids.size() == 1U || neighbor.entry_ids.size() == 1U;
    if (!forced_small_merge && merged_score > separate_score + kScoreEpsilon) {
      continue;
    }

    const auto score_delta = merged_score - separate_score;
    if (score_delta < best_delta) {
      best_delta = score_delta;
      best_neighbor = neighbor_id;
    }
  }

  if (!best_neighbor.has_value()) {
    return false;
  }

  MergeDrafts(clusters.at(*best_neighbor), clusters.at(cluster_id), entries, config);
  return true;
}

auto PolishSmallClusters(std::vector<ClusterDraft>& clusters, const std::vector<LoadEntry>& entries, const LinearClusteringConfig& config)
    -> void
{
  for (std::size_t round = 0; round < kMergeRoundCount; ++round) {
    bool changed = false;
    std::vector<std::size_t> cluster_order(clusters.size());
    std::iota(cluster_order.begin(), cluster_order.end(), 0U);
    std::ranges::sort(cluster_order, [&clusters](std::size_t lhs, std::size_t rhs) -> bool {
      if (clusters.at(lhs).entry_ids.size() != clusters.at(rhs).entry_ids.size()) {
        return clusters.at(lhs).entry_ids.size() < clusters.at(rhs).entry_ids.size();
      }
      return lhs < rhs;
    });

    for (const auto cluster_id : cluster_order) {
      if (!clusters.at(cluster_id).active || clusters.at(cluster_id).entry_ids.empty()) {
        continue;
      }
      changed = MergeDraftsIfUseful(cluster_id, clusters, entries, config) || changed;
    }
    if (!changed) {
      break;
    }
  }

  PolishBoundaryLoads(clusters, entries, config);

  const auto inactive_tail
      = std::ranges::remove_if(clusters, [](const ClusterDraft& cluster) -> bool { return !cluster.active || cluster.entry_ids.empty(); });
  clusters.erase(inactive_tail.begin(), inactive_tail.end());
}

auto MaterializeCluster(const ClusterDraft& draft, const std::vector<LoadEntry>& entries) -> std::vector<Pin*>
{
  std::vector<Pin*> cluster;
  cluster.reserve(draft.entry_ids.size());
  for (const auto entry_id : draft.entry_ids) {
    auto* pin = entries.at(entry_id).pin;
    if (pin != nullptr) {
      cluster.push_back(pin);
    }
  }
  std::ranges::sort(cluster, [](const Pin* lhs, const Pin* rhs) -> bool {
    if (lhs == nullptr || rhs == nullptr) {
      return lhs < rhs;
    }
    return lhs->get_name() < rhs->get_name();
  });
  return cluster;
}

auto NeedExactCap(const LinearClusteringConfig& config) -> bool
{
  return config.enable_exact_cap;
}

auto ToElectricalSummary(const ConstraintEvaluation& evaluation) -> ClusterElectricalSummary
{
  const auto& metrics = evaluation.metrics;
  return ClusterElectricalSummary{
      .exact = metrics.electrical.exact,
      .route_success = metrics.electrical.route_success,
      .sink_count = metrics.fanout,
      .diameter_dbu = metrics.diameter,
      .pin_cap_pf = metrics.electrical.pin_cap,
      .wire_cap_pf = metrics.electrical.wire_cap,
      .total_cap_pf = metrics.electrical.total_cap,
      .wirelength_dbu = metrics.wirelength,
  };
}

auto SplitClusterByLongestAxis(std::vector<Pin*> cluster) -> std::pair<std::vector<Pin*>, std::vector<Pin*>>
{
  const auto bounds = CalcClusterBounds(cluster);
  const bool split_by_x = (bounds.max_x - bounds.min_x) >= (bounds.max_y - bounds.min_y);
  std::ranges::sort(cluster, [split_by_x](const Pin* lhs, const Pin* rhs) -> bool {
    const auto lhs_location = lhs->get_location();
    const auto rhs_location = rhs->get_location();
    const auto lhs_primary = split_by_x ? lhs_location.get_x() : lhs_location.get_y();
    const auto rhs_primary = split_by_x ? rhs_location.get_x() : rhs_location.get_y();
    if (lhs_primary != rhs_primary) {
      return lhs_primary < rhs_primary;
    }
    const auto lhs_secondary = split_by_x ? lhs_location.get_y() : lhs_location.get_x();
    const auto rhs_secondary = split_by_x ? rhs_location.get_y() : rhs_location.get_x();
    if (lhs_secondary != rhs_secondary) {
      return lhs_secondary < rhs_secondary;
    }
    return lhs->get_name() < rhs->get_name();
  });

  const auto middle = cluster.begin() + static_cast<std::ptrdiff_t>(cluster.size() / 2U);
  std::vector<Pin*> lhs(cluster.begin(), middle);
  std::vector<Pin*> rhs(middle, cluster.end());
  return {std::move(lhs), std::move(rhs)};
}

// NOLINTNEXTLINE(misc-no-recursion): repair splitting strictly reduces cluster size and terminates at singleton leaves.
auto AppendFinalCluster(const std::vector<Pin*>& cluster, const LinearClusteringConfig& config, ConstraintEvaluator& evaluator,
                        ClusterResult& result) -> bool
{
  if (cluster.empty()) {
    return true;
  }

  const auto root = ResolveEvaluationRoot(cluster, config);
  const auto evaluation = evaluator.evaluateLoads(cluster, root, config, NeedExactCap(config));
  if (evaluation.legal) {
    result.clusters.push_back(cluster);
    result.centers.push_back(CalcCenter(cluster));
    result.electrical_summaries.push_back(ToElectricalSummary(evaluation));
    return true;
  }

  if (cluster.size() <= 1U) {
    LOG_WARNING << "Fast clustering could not legalize singleton cluster: violation=" << static_cast<int>(evaluation.violation)
                << ", pin=" << (cluster.front() == nullptr ? std::string("<null>") : cluster.front()->get_name());
    return false;
  }

  auto [lhs, rhs] = SplitClusterByLongestAxis(cluster);
  if (lhs.empty() || rhs.empty()) {
    return false;
  }
  return AppendFinalCluster(lhs, config, evaluator, result) && AppendFinalCluster(rhs, config, evaluator, result);
}

auto FinalizeClusters(const std::vector<ClusterDraft>& drafts, const std::vector<LoadEntry>& entries, const LinearClusteringConfig& config)
    -> std::optional<ClusterResult>
{
  ClusterResult result;
  result.clusters.reserve(drafts.size());
  result.centers.reserve(drafts.size());
  result.electrical_summaries.reserve(drafts.size());

  ConstraintEvaluator evaluator;
  for (const auto& draft : drafts) {
    auto cluster = MaterializeCluster(draft, entries);
    if (!AppendFinalCluster(cluster, config, evaluator, result)) {
      return std::nullopt;
    }
  }
  return result;
}

auto CountAssignedLoads(const ClusterResult& result) -> std::size_t
{
  std::size_t count = 0;
  for (const auto& cluster : result.clusters) {
    count += cluster.size();
  }
  return count;
}

}  // namespace

auto FastClustering::buildElectricalBaseConfig(std::size_t max_fanout, double max_cap) -> LinearClusteringConfig
{
  return LinearClustering::buildElectricalBaseConfig(max_fanout, max_cap);
}

auto FastClustering::runDefault(const std::vector<Pin*>& loads, const LinearClusteringConfig& base_config) -> ClusterResult
{
  return run(loads, base_config);
}

auto FastClustering::run(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> ClusterResult
{
  ClusterResult result;
  if (loads.empty()) {
    return result;
  }

  auto entries = CollectEntries(loads);
  if (entries.empty()) {
    LOG_WARNING << "Fast clustering skipped: no valid load pins.";
    return result;
  }

  auto drafts = BuildSpatialRecursiveClusters(entries, config);
  PolishSmallClusters(drafts, entries, config);

  auto finalized = FinalizeClusters(drafts, entries, config);
  if (!finalized.has_value() || CountAssignedLoads(*finalized) != entries.size()) {
    LOG_WARNING << "Fast clustering failed to produce a legal complete partition. Falling back to linear clustering.";
    return LinearClustering::run(loads, config);
  }

  LOG_INFO << "Fast clustering done: loads=" << entries.size() << ", clusters=" << finalized->clusters.size()
           << ", strategy=recursive_spatial_bisect";
  return *finalized;
}

}  // namespace icts
