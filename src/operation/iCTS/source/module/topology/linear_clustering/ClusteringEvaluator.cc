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
 * @file ClusteringEvaluator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Cost evaluation for legal linear clustering partitions.
 */

#include "ClusteringEvaluator.hh"

#include <limits>
#include <vector>

#include "LinearClusteringTypes.hh"
#include "TopologyConfig.hh"

namespace icts {
namespace {

auto ScoreMaxDiameter(const ClusterConstraintMetrics& metrics, const LinearClusteringConfig& config, ClusterScoreBreakdown& score) -> void
{
  // max-diameter budget so singleton clusters do not become artificially free under kMaxDiameter.
  if (metrics.diameter > 0) {
    score.total_score = static_cast<double>(metrics.diameter);
    return;
  }

  score.total_score = config.max_diameter > 0 ? static_cast<double>(config.max_diameter) : 0.0;
}

auto ScoreTotalWirelength(const ClusterConstraintMetrics& metrics, const LinearClusteringConfig& config, ClusterScoreBreakdown& score)
    -> void
{
  // Score is the routed wirelength of the cluster (from RCTree routing).
  // Falls back to diameter-based estimate if wirelength is not available (no exact cap eval).
  const auto wirelength = metrics.wirelength;
  if (wirelength > 0.0) {
    score.total_score = config.wirelength_weight * wirelength;
  } else {
    // Fallback: use diameter as a proxy for wirelength when routing is not performed.
    score.total_score = config.wirelength_weight * static_cast<double>(metrics.diameter);
  }
}

}  // namespace

auto ClusteringEvaluator::evaluateSegment(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment,
                                          const LinearClusteringConfig& config, bool need_exact_cap) -> ClusterScoreBreakdown
{
  ClusterScoreBreakdown score;
  score.constraint = _constraint_evaluator.evaluate(ordered_loads, segment, config, need_exact_cap);
  score.legal = score.constraint.legal;
  if (!score.legal) {
    return score;
  }

  const auto& metrics = score.constraint.metrics;
  switch (config.scoring_strategy) {
    case LinearScoringStrategy::kMaxDiameter:
      ScoreMaxDiameter(metrics, config, score);
      break;
    case LinearScoringStrategy::kTotalWirelength:
      ScoreTotalWirelength(metrics, config, score);
      break;
  }

  return score;
}

auto ClusteringEvaluator::evaluatePartition(const std::vector<OrderedLoad>& ordered_loads, const std::vector<SegmentRange>& segments,
                                            const LinearClusteringConfig& config, bool need_exact_cap) -> PartitionScore
{
  PartitionScore partition;
  partition.segments = segments;
  if (segments.empty()) {
    return partition;
  }

  partition.legal = true;
  partition.total_score = 0.0;
  partition.clusters.reserve(segments.size());

  for (const auto& segment : segments) {
    auto segment_score = evaluateSegment(ordered_loads, segment, config, need_exact_cap);
    partition.clusters.push_back(segment_score);
    if (!segment_score.legal) {
      partition.legal = false;
      partition.total_score = std::numeric_limits<double>::infinity();
      return partition;
    }
    partition.total_score += segment_score.total_score;
  }

  return partition;
}

}  // namespace icts
