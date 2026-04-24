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
 * @file HTreeActualLoad.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Actual-load legality and boundary-cap coverage checks for H-tree candidates.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Log.hh"
#include "PatternId.hh"
#include "Point.hh"
#include "TopologyConfig.hh"
#include "Tree.hh"
#include "clustering/Clustering.hh"
#include "config/Config.hh"
#include "htree/HTreeBuilderInternal.hh"
#include "topology/TopologyGen.hh"

namespace icts {
class UniformValueLattice;
}  // namespace icts

namespace icts::htree_builder {
namespace {

auto CalcMedianCapPf(std::vector<double> caps_pf) -> double
{
  if (caps_pf.empty()) {
    return 0.0;
  }

  std::ranges::sort(caps_pf);
  const std::size_t middle = caps_pf.size() / 2U;
  if ((caps_pf.size() & 1U) == 1U) {
    return caps_pf.at(middle);
  }
  return (caps_pf.at(middle - 1U) + caps_pf.at(middle)) * 0.5;
}

auto BuildCapDistributionStats(const std::vector<double>& caps_pf) -> CapDistributionStats
{
  CapDistributionStats stats;
  if (caps_pf.empty()) {
    return stats;
  }

  stats.group_count = caps_pf.size();
  stats.cap_min_pf = *std::ranges::min_element(caps_pf);
  stats.cap_max_pf = *std::ranges::max_element(caps_pf);

  double total_cap_pf = 0.0;
  for (const double cap_pf : caps_pf) {
    total_cap_pf += cap_pf;
  }
  stats.cap_mean_pf = total_cap_pf / static_cast<double>(caps_pf.size());
  stats.cap_median_pf = CalcMedianCapPf(caps_pf);
  return stats;
}

auto BuildLeafElectricalConfig() -> ClusterConfig
{
  const double max_cap = CONFIG_INST.has_max_cap() ? CONFIG_INST.get_max_cap() : std::numeric_limits<double>::infinity();
  auto config = TopologyGen::buildFastClusteringElectricalConfig(CONFIG_INST.get_max_fanout(), max_cap);
  config.enable_exact_cap = true;
  config.always_build_exact_cap = true;
  config.scoring_strategy = ClusterScoringStrategy::kTotalWirelength;
  return config;
}

auto ResolveBottomMostBufferedLevel(const HTreeTopologyPattern& topology_pattern, const BufferPatternRegistry& segment_pattern_registry)
    -> int
{
  const auto& level_segment_pattern_ids = topology_pattern.get_level_segment_pattern_ids();
  for (int level = static_cast<int>(level_segment_pattern_ids.size()) - 1; level >= 0; --level) {
    const auto segment_pattern_id = level_segment_pattern_ids.at(static_cast<std::size_t>(level));
    const auto* segment_pattern = segment_pattern_registry.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTreeBuilder: missing segment pattern metadata while resolving actual-load boundary.";
    if (!segment_pattern->get_buffer_positions().empty()) {
      return level;
    }
  }
  return -1;
}

auto ResolveActualLoadLegalitySignature(const HTreeTopologyPattern& topology_pattern, const BufferPatternRegistry& segment_pattern_registry)
    -> ActualLoadLegalitySignature
{
  const int bottom_most_buffered_level = ResolveBottomMostBufferedLevel(topology_pattern, segment_pattern_registry);
  ActualLoadLegalitySignature signature;
  signature.bottom_most_buffered_level = bottom_most_buffered_level;
  if (bottom_most_buffered_level >= 0) {
    signature.segment_pattern_id
        = topology_pattern.get_level_segment_pattern_ids().at(static_cast<std::size_t>(bottom_most_buffered_level));
  }
  return signature;
}

auto BuildActualLoadFeasibilityReason(std::size_t node_id, const Point<int>& anchor, const std::string& detail) -> std::string
{
  std::ostringstream stream;
  stream << "htree_load_group_node_" << node_id << " anchor=(" << anchor.get_x() << "," << anchor.get_y() << ") " << detail;
  return stream.str();
}

auto CollectActualLoadBoundaryGroups(const Tree& topology, const ActualLoadLegalitySignature& signature,
                                     const BufferPatternRegistry& segment_pattern_registry)
    -> std::pair<std::vector<ActualLoadBoundaryGroup>, std::string>
{
  std::vector<ActualLoadBoundaryGroup> groups;

  const auto* root_node = topology.get_node(topology.get_root());
  if (root_node == nullptr) {
    return {std::move(groups), "missing_topology_root"};
  }

  if (signature.bottom_most_buffered_level < 0) {
    if (root_node->get_loads().empty()) {
      return {std::move(groups), "empty_root_load_group"};
    }
    groups.push_back(ActualLoadBoundaryGroup{
        .node_id = root_node->get_id(),
        .anchor = root_node->get_position(),
        .loads = &root_node->get_loads(),
    });
    return {std::move(groups), {}};
  }

  const auto topology_levels = topology.levels();
  const std::size_t boundary_level = static_cast<std::size_t>(signature.bottom_most_buffered_level) + 1U;
  if (boundary_level >= topology_levels.size()) {
    return {std::move(groups), "missing_actual_load_boundary_level"};
  }

  const auto* segment_pattern = segment_pattern_registry.find(signature.segment_pattern_id);
  if (segment_pattern == nullptr) {
    return {std::move(groups), "missing_boundary_segment_pattern"};
  }
  if (segment_pattern->get_buffer_positions().empty()) {
    return {std::move(groups), "missing_boundary_buffer_position"};
  }

  const double last_buffer_position = segment_pattern->get_buffer_positions().back();
  groups.reserve(topology_levels.at(boundary_level).size());
  for (const auto node_id : topology_levels.at(boundary_level)) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr) {
      return {std::move(groups), "missing_boundary_topology_node"};
    }
    if (node->get_loads().empty()) {
      continue;
    }

    const auto* parent_node = topology.get_node(node->get_parent());
    if (parent_node == nullptr) {
      return {std::move(groups), "missing_boundary_parent_node"};
    }

    groups.push_back(ActualLoadBoundaryGroup{
        .node_id = node_id,
        .anchor = InterpolateManhattanPoint(parent_node->get_position(), node->get_position(), last_buffer_position),
        .loads = &node->get_loads(),
    });
  }

  if (groups.empty()) {
    return {std::move(groups), "empty_actual_load_groups"};
  }
  return {std::move(groups), {}};
}

auto EvaluateActualLoadLegality(const Tree& topology, const ActualLoadLegalitySignature& signature,
                                const BufferPatternRegistry& segment_pattern_registry, const UniformValueLattice& cap_lattice)
    -> ActualLoadLegalityResult
{
  ActualLoadLegalityResult result;
  result.bottom_most_buffered_level = signature.bottom_most_buffered_level;
  result.segment_pattern_id = signature.segment_pattern_id;

  const auto [groups, collection_error] = CollectActualLoadBoundaryGroups(topology, signature, segment_pattern_registry);
  if (!collection_error.empty()) {
    result.failure_reason = collection_error;
    if (collection_error == "missing_topology_root") {
      result.violation = ActualLoadViolation::kMissingTopologyRoot;
    } else if (collection_error == "missing_actual_load_boundary_level") {
      result.violation = ActualLoadViolation::kMissingTopologyLevel;
    } else if (collection_error == "missing_boundary_segment_pattern") {
      result.violation = ActualLoadViolation::kMissingSegmentPattern;
    } else if (collection_error == "missing_boundary_buffer_position") {
      result.violation = ActualLoadViolation::kMissingBufferPosition;
    } else if (collection_error == "empty_root_load_group" || collection_error == "empty_actual_load_groups") {
      result.violation = ActualLoadViolation::kEmptyLoadGroup;
    } else {
      result.violation = ActualLoadViolation::kMissingTopologyNode;
    }
    return result;
  }

  const auto electrical_config = BuildLeafElectricalConfig();
  const std::size_t max_fanout = CONFIG_INST.get_max_fanout();
  for (const auto& group : groups) {
    const auto* loads = group.loads;
    if (loads == nullptr || loads->empty()) {
      result.violation = ActualLoadViolation::kEmptyLoadGroup;
      result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "empty_group_loads");
      return result;
    }
    if (max_fanout > 0U && loads->size() > max_fanout) {
      std::ostringstream detail;
      detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
      result.violation = ActualLoadViolation::kFanout;
      result.monotone_hard_fail = true;
      result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      return result;
    }

    const auto lower_bound = Clustering::evaluateClusterElectrical(*loads, group.anchor, electrical_config, false);
    if (!lower_bound.legal) {
      if (lower_bound.violation == ClusterElectricalViolation::kFanout) {
        std::ostringstream detail;
        detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
        result.violation = ActualLoadViolation::kFanout;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else if (lower_bound.violation == ClusterElectricalViolation::kCapacitance) {
        std::ostringstream detail;
        detail << "pin_cap_lower_bound_violation total_cap_pf=" << lower_bound.summary.total_cap_pf;
        if (CONFIG_INST.has_max_cap()) {
          detail << ", max_cap_pf=" << CONFIG_INST.get_max_cap();
        }
        result.violation = ActualLoadViolation::kPinCapLowerBound;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else {
        result.violation = ActualLoadViolation::kEmptyLoadGroup;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "lower_bound_evaluation_failed");
      }
      return result;
    }
  }

  std::vector<double> total_caps_pf;
  total_caps_pf.reserve(groups.size());
  for (const auto& group : groups) {
    const auto* loads = group.loads;
    LOG_FATAL_IF(loads == nullptr || loads->empty()) << "HTreeBuilder: actual-load boundary group lost its load set.";

    const auto exact = Clustering::evaluateClusterElectrical(*loads, group.anchor, electrical_config, true);
    if (!exact.legal) {
      if (exact.violation == ClusterElectricalViolation::kRoutingFailed) {
        result.violation = ActualLoadViolation::kRoutingFailed;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "routing_failed");
      } else if (exact.violation == ClusterElectricalViolation::kCapacitance) {
        std::ostringstream detail;
        detail << "cap_violation total_cap_pf=" << exact.summary.total_cap_pf;
        if (CONFIG_INST.has_max_cap()) {
          detail << ", max_cap_pf=" << CONFIG_INST.get_max_cap();
        }
        result.violation = ActualLoadViolation::kCapacitance;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else if (exact.violation == ClusterElectricalViolation::kFanout) {
        std::ostringstream detail;
        detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
        result.violation = ActualLoadViolation::kFanout;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else {
        result.violation = ActualLoadViolation::kEmptyLoadGroup;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "exact_evaluation_failed");
      }
      return result;
    }

    total_caps_pf.push_back(exact.summary.total_cap_pf);
  }

  result.cap_distribution = BuildCapDistributionStats(total_caps_pf);
  result.required_leaf_load_cap_pf = result.cap_distribution.cap_max_pf;
  result.required_leaf_load_cap_covering_idx = CoveringBoundaryIndex(result.required_leaf_load_cap_pf, cap_lattice);
  result.violation = ActualLoadViolation::kNone;
  result.legal = true;
  return result;
}

}  // namespace

auto ResolveActualLoadLegality(const Tree& topology, PatternId topology_pattern_id, const TopologyPatternRegistry& topology_registry,
                               const BufferPatternRegistry& segment_pattern_registry, ActualLoadLegalityContext& legality_context)
    -> ActualLoadLegalityResult
{
  const auto topology_pattern = topology_registry.materialize(topology_pattern_id);
  const auto signature = ResolveActualLoadLegalitySignature(topology_pattern, segment_pattern_registry);
  if (signature.bottom_most_buffered_level <= legality_context.max_monotone_failed_level) {
    ActualLoadLegalityResult result;
    result.bottom_most_buffered_level = signature.bottom_most_buffered_level;
    result.segment_pattern_id = signature.segment_pattern_id;
    result.violation = ActualLoadViolation::kFanout;
    std::ostringstream detail;
    detail << "monotone_pruned_by_bottom_most_buffered_level threshold=" << legality_context.max_monotone_failed_level
           << ", candidate_level=" << signature.bottom_most_buffered_level;
    result.failure_reason = detail.str();
    return result;
  }

  const auto cache_it = legality_context.result_by_signature.find(signature);
  if (cache_it != legality_context.result_by_signature.end()) {
    return cache_it->second;
  }

  auto evaluated = EvaluateActualLoadLegality(topology, signature, segment_pattern_registry, legality_context.cap_lattice);
  if (!evaluated.legal && evaluated.monotone_hard_fail) {
    legality_context.max_monotone_failed_level = std::max(legality_context.max_monotone_failed_level, signature.bottom_most_buffered_level);
  }
  return legality_context.result_by_signature.emplace(signature, std::move(evaluated)).first->second;
}

auto FilterActualLoadLegalEntries(const std::vector<HTreeTopologyChar>& entries, const Tree& topology,
                                  const TopologyPatternRegistry& topology_registry, const BufferPatternRegistry& segment_pattern_registry,
                                  ActualLoadLegalityContext& legality_context) -> ActualLoadEntryFilterResult
{
  ActualLoadEntryFilterResult result;
  result.entries.reserve(entries.size());
  for (const auto& entry : entries) {
    const auto legality
        = ResolveActualLoadLegality(topology, entry.get_pattern_id(), topology_registry, segment_pattern_registry, legality_context);
    if (!legality.legal) {
      if (result.first_failure_reason.empty()) {
        result.first_failure_reason = legality.failure_reason;
      }
      continue;
    }
    result.entries.push_back(entry);
  }
  return result;
}

auto FilterGlobalEntriesByActualBoundaryCoverage(const std::vector<CandidateCharRef>& entries,
                                                 const std::vector<CandidateBuildEvaluation>& evaluations, const Tree& topology,
                                                 const BufferPatternRegistry& segment_pattern_registry,
                                                 ActualLoadLegalityContext& legality_context) -> CandidateCharRefFilterResult
{
  CandidateCharRefFilterResult result;
  result.entries.reserve(entries.size());
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry == nullptr || entry_ref.candidate_index >= evaluations.size()) {
      if (result.first_failure_reason.empty()) {
        result.first_failure_reason = "invalid_global_candidate_ref";
      }
      continue;
    }

    const auto& evaluation = evaluations[entry_ref.candidate_index];
    const auto legality = ResolveActualLoadLegality(topology, entry_ref.entry->get_pattern_id(), evaluation.topology_pattern_registry,
                                                    segment_pattern_registry, legality_context);
    if (!legality.legal) {
      if (result.first_failure_reason.empty()) {
        result.first_failure_reason = legality.failure_reason;
      }
      continue;
    }

    if (legality.required_leaf_load_cap_covering_idx.has_value()
        && entry_ref.entry->get_leaf_load_cap_idx() < *legality.required_leaf_load_cap_covering_idx) {
      if (result.first_failure_reason.empty()) {
        std::ostringstream detail;
        detail << "actual_boundary_load_coverage_violation required_leaf_load_cap_idx=" << *legality.required_leaf_load_cap_covering_idx
               << ", entry_leaf_load_cap_idx=" << entry_ref.entry->get_leaf_load_cap_idx()
               << ", max_real_load_cap_pf=" << legality.required_leaf_load_cap_pf;
        result.first_failure_reason = detail.str();
      }
      continue;
    }

    result.entries.push_back(entry_ref);
  }
  return result;
}

}  // namespace icts::htree_builder
