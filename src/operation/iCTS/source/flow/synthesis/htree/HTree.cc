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
 * @file HTree.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief H-tree topology-family synthesis entry implementation.
 */

#include "synthesis/htree/HTree.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharBuilder.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Log.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "STAAdapter.hh"
#include "SegmentChar.hh"
#include "TopologyConfig.hh"
#include "Tree.hh"
#include "ValueLattice.hh"
#include "analytical_characterization/AnalyticalCharacterization.hh"
#include "analytical_characterization/AnalyticalModel.hh"
#include "config/Config.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "synthesis/htree/analytical_solver/AnalyticalCandidate.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/analytical_solver/AnalyticalValidation.hh"
#include "synthesis/htree/characterization/Characterization.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/embedding/Embedding.hh"
#include "synthesis/htree/plan/DepthPlan.hh"
#include "synthesis/htree/plan/Plan.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/segment_pruning/SegmentLibrary.hh"
#include "synthesis/htree/segment_pruning/SegmentPruning.hh"
#include "synthesis/htree/solution/SolutionReport.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"
#include "topology/TopologyGen.hh"

namespace icts {

namespace {

constexpr double kRootDriverCompensationClockPeriodNs = 10.0;
constexpr std::size_t kAnalyticalPerLevelShortlistSize = 128U;
constexpr std::size_t kAnalyticalTopKPerDepth = 128U;
constexpr std::size_t kAnalyticalUnitComposeBeamSize = 128U;
constexpr unsigned kAnalyticalUnitLengthIdx = 1U;
constexpr double kAnalyticalParetoPowerSlackRatio = 0.20;

struct AnalyticalHTreeAttempt
{
  bool selected = false;
  std::string fallback_reason;
  htree::CandidateBuildEvaluation selected_evaluation;
  htree::DepthSummary selected_summary;
  htree::RootDriverCompensationDetail selected_compensation_detail;
  htree::SinkLoadRegionLegalityResult selected_sink_load_region_legality;
  htree::RootDriverCompensationStats root_driver_compensation_stats;
  std::size_t model_set_count = 0U;
  std::size_t rejected_fit_count = 0U;
  std::size_t structural_cap_operator_count = 0U;
  std::size_t evaluated_segment_count = 0U;
  std::size_t generated_candidate_count = 0U;
  std::size_t validated_candidate_count = 0U;
  std::size_t scored_segment_count = 0U;
  std::size_t missing_model_count = 0U;
  std::size_t decomposition_rejected_count = 0U;
  std::size_t metric_evaluation_rejected_count = 0U;
  std::size_t domain_slew_rejected_count = 0U;
  std::size_t domain_cap_rejected_count = 0U;
  std::size_t domain_slew_floor_count = 0U;
  std::size_t domain_cap_floor_count = 0U;
  double max_domain_rejected_cap_pf = 0.0;
  std::size_t empty_shortlist_count = 0U;
  std::size_t materialization_attempt_count = 0U;
  std::size_t root_fanout_rejected_count = 0U;
  std::size_t lattice_rejected_count = 0U;
  std::size_t diagnostic_library_hit_count = 0U;
  std::size_t diagnostic_frontier_hit_count = 0U;
  std::size_t diagnostic_decomposed_count = 0U;
  std::size_t diagnostic_scored_count = 0U;
  std::size_t diagnostic_shortlisted_count = 0U;
  std::size_t diagnostic_generated_candidate_count = 0U;
  std::size_t diagnostic_direct_candidate_count = 0U;
  double diagnostic_direct_delay_ns = 0.0;
  double diagnostic_direct_power_w = 0.0;
  double diagnostic_direct_root_cap_pf = 0.0;
  unsigned diagnostic_direct_input_slew_idx = 0U;
  unsigned diagnostic_direct_output_slew_idx = 0U;
  unsigned diagnostic_direct_driven_cap_idx = 0U;
  std::size_t validated_pareto_count = 0U;
  std::size_t selected_pareto_power_rank = 0U;
  double validated_delay_min_ns = 0.0;
  double validated_delay_median_ns = 0.0;
  double validated_delay_max_ns = 0.0;
  double validated_power_min_w = 0.0;
  double validated_power_median_w = 0.0;
  double validated_power_max_w = 0.0;
  unsigned first_empty_level_index = 0U;
  unsigned first_empty_length_idx = 0U;
  std::string first_empty_reason;
};

struct AnalyticalValidatedCandidate
{
  htree::analytical_solver::AnalyticalCandidate candidate;
  htree::analytical_solver::AnalyticalValidationResult validation;
  std::size_t original_index = 0U;
};

auto ResolveSelectedRootDriverCellMaster(const std::vector<HTree::LevelPlan>& levels) -> std::string
{
  for (const auto& level : levels) {
    if (!level.selected_leaf_buffer_cell_master.empty()) {
      return level.selected_leaf_buffer_cell_master;
    }
    if (!level.selected_terminal_cell_master.empty()) {
      return level.selected_terminal_cell_master;
    }
  }
  return {};
}

auto ResolveTopologyLevelMultiplicity(std::size_t level_index) -> std::size_t
{
  if (level_index >= static_cast<std::size_t>(std::numeric_limits<std::size_t>::digits - 1)) {
    return std::numeric_limits<std::size_t>::max();
  }
  return std::size_t{1U} << level_index;
}

auto SaturatingMultiply(std::size_t lhs, std::size_t rhs) -> std::size_t
{
  if (lhs == 0U || rhs == 0U) {
    return 0U;
  }
  if (lhs > std::numeric_limits<std::size_t>::max() / rhs) {
    return std::numeric_limits<std::size_t>::max();
  }
  return lhs * rhs;
}

auto CalcCellMastersAreaUm2(const std::vector<std::string>& cell_masters) -> double
{
  double area_um2 = 0.0;
  for (const auto& cell_master : cell_masters) {
    area_um2 += std::max(0.0, STA_ADAPTER_INST.queryCellAreaUm2(cell_master));
  }
  return area_um2;
}

auto ResolveRootDriverCompensationInputSlewNs(const HTree::BuildOptions& options, double max_slew_ns) -> double
{
  if (options.min_top_input_slew_ns.has_value() && *options.min_top_input_slew_ns >= 0.0) {
    return *options.min_top_input_slew_ns;
  }
  return max_slew_ns > 0.0 ? max_slew_ns * 0.5 : 0.0;
}

auto ResolvePatternSearchBoundaryConstraints(const htree::BoundaryConstraints& base_constraints, bool strict_root_boundary_closure)
    -> htree::BoundaryConstraints
{
  auto constraints = base_constraints;
  if (strict_root_boundary_closure) {
    constraints.top_input_slew_covering_idx = std::nullopt;
  }
  return constraints;
}

auto ResolveRootDriverClockPeriod(const HTree::BuildOptions& options) -> std::pair<double, std::string>
{
  if (options.clock_period_ns > 0.0) {
    return {options.clock_period_ns, options.clock_period_source.empty() ? "caller" : options.clock_period_source};
  }
  return {kRootDriverCompensationClockPeriodNs, "default_10ns"};
}

auto ApplyRootDriverCompensationResult(HTree::BuildResult& result, const htree::DepthSearchResult& exploration,
                                       const htree::RootDriverCompensationDetail& compensation_detail,
                                       const HTreeTopologyChar& selected_entry) -> void
{
  auto& report = result.root_driver_compensation;
  report.enabled = exploration.root_driver_compensation_stats.enabled;
  report.valid = compensation_detail.valid;
  report.method = compensation_detail.method.empty() ? exploration.root_driver_compensation_stats.method : compensation_detail.method;
  report.cell_master = compensation_detail.cell_master;
  report.load_source
      = compensation_detail.load_source.empty() ? exploration.root_driver_compensation_stats.load_source : compensation_detail.load_source;
  report.route_estimator = compensation_detail.route_estimator;
  report.input_slew_ns = compensation_detail.input_slew_ns > 0.0 ? compensation_detail.input_slew_ns
                                                                 : exploration.root_driver_compensation_stats.input_slew_ns;
  report.load_bucket_idx = compensation_detail.load_bucket_idx;
  report.load_cap_pf = compensation_detail.load_cap_pf;
  report.source_boundary_bucket_idx = compensation_detail.source_boundary_bucket_idx;
  report.source_boundary_load_cap_pf = compensation_detail.source_boundary_load_cap_pf;
  report.source_boundary_branch_count = compensation_detail.source_boundary_branch_count;
  report.terminal_pin_cap_pf = compensation_detail.terminal_pin_cap_pf;
  report.wire_cap_pf = compensation_detail.wire_cap_pf;
  report.routed_wirelength_um = compensation_detail.routed_wirelength_um;
  report.terminal_count = compensation_detail.terminal_count;
  report.clock_period_ns = compensation_detail.clock_period_ns > 0.0 ? compensation_detail.clock_period_ns
                                                                     : exploration.root_driver_compensation_stats.clock_period_ns;
  report.output_slew_ns = compensation_detail.output_slew_ns;
  report.output_slew_bucket_idx = compensation_detail.output_slew_bucket_idx;
  report.cell_delay_ns = compensation_detail.cell_delay_ns;
  report.internal_power_w = compensation_detail.internal_power_w;
  report.leakage_power_w = compensation_detail.leakage_power_w;
  report.cell_power_w = compensation_detail.cell_power_w;
  report.raw_delay_ns = selected_entry.get_raw_delay();
  report.raw_power_w = selected_entry.get_raw_power();
  report.compensated_delay_ns = selected_entry.get_delay();
  report.compensated_power_w = selected_entry.get_power();
}

auto ApplySelectedPatternToLevelPlans(HTree::BuildResult& result, const htree::BufferPatternLibrary& segment_pattern_library) -> void
{
  LOG_FATAL_IF(!result.best_pattern.has_value()) << "HTree: selected topology pattern is missing.";
  const auto& best_level_segment_pattern_ids = result.best_pattern->get_level_segment_pattern_ids();
  LOG_FATAL_IF(best_level_segment_pattern_ids.size() != result.levels.size())
      << "HTree: best H-tree pattern level count does not match selected depth.";

  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    auto& level = result.levels.at(level_index);
    const auto segment_pattern_id = best_level_segment_pattern_ids.at(level_index);
    level.segment_pattern_id = segment_pattern_id;
    const auto* segment_pattern = segment_pattern_library.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTree: selected segment pattern metadata is missing.";

    const auto& cell_masters = segment_pattern->get_cell_masters();
    const auto level_multiplicity = ResolveTopologyLevelMultiplicity(level_index);
    level.selected_has_any_buffer = !cell_masters.empty();
    level.selected_leaf_buffer_cell_master = cell_masters.empty() ? "" : cell_masters.back();
    level.selected_has_terminal_branch_buffer = segment_pattern->hasTerminalBranchBuffer();
    level.selected_terminal_cell_master = segment_pattern->hasTerminalBranchBuffer() && !cell_masters.empty() ? cell_masters.back() : "";
    level.selected_buffer_count = cell_masters.size();
    level.selected_buffer_area_um2 = CalcCellMastersAreaUm2(cell_masters);
    level.selected_weighted_buffer_count = SaturatingMultiply(level_multiplicity, level.selected_buffer_count);
    level.selected_weighted_buffer_area_um2 = static_cast<double>(level_multiplicity) * level.selected_buffer_area_um2;
  }
}

auto ResolveAnalyticalRepresentativeLeafLoadCapPf(const UniformValueLattice& cap_lattice) -> double
{
  if (!cap_lattice.isValid()) {
    return 0.0;
  }
  return cap_lattice.valueForIndex(1U);
}

auto ShouldPreferAnalyticalCandidate(const htree::analytical_solver::AnalyticalCandidate& lhs,
                                     const htree::analytical_solver::AnalyticalCandidate& rhs) -> bool
{
  if (lhs.materialized_char.has_value() && rhs.materialized_char.has_value()) {
    const auto& lhs_char = *lhs.materialized_char;
    const auto& rhs_char = *rhs.materialized_char;
    if (lhs_char.get_delay() != rhs_char.get_delay()) {
      return lhs_char.get_delay() < rhs_char.get_delay();
    }
    if (lhs_char.get_power() != rhs_char.get_power()) {
      return lhs_char.get_power() < rhs_char.get_power();
    }
  }
  return htree::analytical_solver::PreferAnalyticalCandidate(lhs, rhs);
}

auto PreferAnalyticalPowerOrder(const htree::analytical_solver::AnalyticalCandidate& lhs,
                                const htree::analytical_solver::AnalyticalCandidate& rhs) -> bool
{
  if (lhs.materialized_char.has_value() && rhs.materialized_char.has_value()) {
    const auto& lhs_char = *lhs.materialized_char;
    const auto& rhs_char = *rhs.materialized_char;
    if (lhs_char.get_power() != rhs_char.get_power()) {
      return lhs_char.get_power() < rhs_char.get_power();
    }
    if (lhs_char.get_delay() != rhs_char.get_delay()) {
      return lhs_char.get_delay() < rhs_char.get_delay();
    }
    if (lhs_char.get_driven_cap_idx() != rhs_char.get_driven_cap_idx()) {
      return lhs_char.get_driven_cap_idx() < rhs_char.get_driven_cap_idx();
    }
    if (lhs_char.get_output_slew_idx() != rhs_char.get_output_slew_idx()) {
      return lhs_char.get_output_slew_idx() < rhs_char.get_output_slew_idx();
    }
  }
  return htree::analytical_solver::PreferAnalyticalCandidate(lhs, rhs);
}

auto RequireMaterializedAnalyticalChar(const AnalyticalValidatedCandidate& candidate) -> const HTreeTopologyChar&
{
  LOG_FATAL_IF(!candidate.candidate.materialized_char.has_value()) << "HTree: missing materialized analytical topology char.";
  return *candidate.candidate.materialized_char;
}

auto CollectAnalyticalDelayPowerParetoRefs(const std::vector<AnalyticalValidatedCandidate>& candidates)
    -> std::vector<const AnalyticalValidatedCandidate*>
{
  std::vector<const AnalyticalValidatedCandidate*> sorted_candidates;
  sorted_candidates.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    if (candidate.candidate.materialized_char.has_value()) {
      sorted_candidates.push_back(&candidate);
    }
  }

  std::ranges::sort(sorted_candidates, [](const AnalyticalValidatedCandidate* lhs, const AnalyticalValidatedCandidate* rhs) -> bool {
    LOG_FATAL_IF(lhs == nullptr || rhs == nullptr) << "HTree: null analytical candidate during Pareto sorting.";
    return ShouldPreferAnalyticalCandidate(lhs->candidate, rhs->candidate);
  });

  std::vector<const AnalyticalValidatedCandidate*> pareto_front;
  pareto_front.reserve(sorted_candidates.size());
  double best_power_before_delay = std::numeric_limits<double>::infinity();
  std::size_t delay_group_begin = 0U;
  while (delay_group_begin < sorted_candidates.size()) {
    std::size_t delay_group_end = delay_group_begin + 1U;
    const auto& delay_group_anchor_char = RequireMaterializedAnalyticalChar(*sorted_candidates.at(delay_group_begin));
    while (delay_group_end < sorted_candidates.size()) {
      const auto& delay_group_next_char = RequireMaterializedAnalyticalChar(*sorted_candidates.at(delay_group_end));
      if (delay_group_next_char.get_delay() != delay_group_anchor_char.get_delay()) {
        break;
      }
      ++delay_group_end;
    }

    double delay_group_min_power = std::numeric_limits<double>::infinity();
    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      const auto& materialized_char = RequireMaterializedAnalyticalChar(*sorted_candidates.at(index));
      delay_group_min_power = std::min(delay_group_min_power, materialized_char.get_power());
    }

    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      const auto* candidate = sorted_candidates.at(index);
      const auto& materialized_char = RequireMaterializedAnalyticalChar(*candidate);
      if (best_power_before_delay <= materialized_char.get_power() || delay_group_min_power < materialized_char.get_power()) {
        continue;
      }
      pareto_front.push_back(candidate);
    }

    best_power_before_delay = std::min(best_power_before_delay, delay_group_min_power);
    delay_group_begin = delay_group_end;
  }

  std::ranges::sort(pareto_front, [](const AnalyticalValidatedCandidate* lhs, const AnalyticalValidatedCandidate* rhs) -> bool {
    LOG_FATAL_IF(lhs == nullptr || rhs == nullptr) << "HTree: null analytical candidate during Pareto delay ordering.";
    return ShouldPreferAnalyticalCandidate(lhs->candidate, rhs->candidate);
  });
  return pareto_front;
}

auto SelectAnalyticalParetoPowerGuardedMinDelay(const std::vector<AnalyticalValidatedCandidate>& candidates,
                                                AnalyticalHTreeAttempt& attempt) -> const AnalyticalValidatedCandidate*
{
  auto pareto_front = CollectAnalyticalDelayPowerParetoRefs(candidates);
  attempt.validated_pareto_count = pareto_front.size();
  if (pareto_front.empty()) {
    return nullptr;
  }

  double min_pareto_power_w = std::numeric_limits<double>::infinity();
  for (const auto* candidate : pareto_front) {
    LOG_FATAL_IF(candidate == nullptr || !candidate->candidate.materialized_char.has_value())
        << "HTree: invalid analytical Pareto candidate during power guard calculation.";
    min_pareto_power_w = std::min(min_pareto_power_w, candidate->candidate.materialized_char->get_power());
  }

  const auto* selected_candidate = pareto_front.front();
  const double guarded_power_limit_w = min_pareto_power_w * (1.0 + kAnalyticalParetoPowerSlackRatio);
  if (std::isfinite(guarded_power_limit_w) && guarded_power_limit_w > 0.0) {
    for (const auto* candidate : pareto_front) {
      LOG_FATAL_IF(candidate == nullptr || !candidate->candidate.materialized_char.has_value())
          << "HTree: invalid analytical Pareto candidate during power-guarded delay selection.";
      if (candidate->candidate.materialized_char->get_power() <= guarded_power_limit_w) {
        selected_candidate = candidate;
        break;
      }
    }
  }

  auto power_ordered_pareto_front = pareto_front;
  std::ranges::sort(
      power_ordered_pareto_front, [](const AnalyticalValidatedCandidate* lhs, const AnalyticalValidatedCandidate* rhs) -> bool {
        LOG_FATAL_IF(lhs == nullptr || rhs == nullptr) << "HTree: null analytical candidate during selected power rank calculation.";
        return PreferAnalyticalPowerOrder(lhs->candidate, rhs->candidate);
      });
  const auto selected_rank_it = std::ranges::find(power_ordered_pareto_front, selected_candidate);
  attempt.selected_pareto_power_rank
      = selected_rank_it == power_ordered_pareto_front.end()
            ? 0U
            : static_cast<std::size_t>(std::distance(power_ordered_pareto_front.begin(), selected_rank_it)) + 1U;
  return selected_candidate;
}

auto RecordAnalyticalValidatedDistribution(const std::vector<AnalyticalValidatedCandidate>& candidates, AnalyticalHTreeAttempt& attempt)
    -> void
{
  std::vector<double> delays;
  std::vector<double> powers;
  delays.reserve(candidates.size());
  powers.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    if (!candidate.candidate.materialized_char.has_value()) {
      continue;
    }
    delays.push_back(candidate.candidate.materialized_char->get_delay());
    powers.push_back(candidate.candidate.materialized_char->get_power());
  }
  if (delays.empty() || powers.empty()) {
    return;
  }

  std::ranges::sort(delays);
  std::ranges::sort(powers);
  const std::size_t median_index = (delays.size() - 1U) / 2U;
  attempt.validated_delay_min_ns = delays.front();
  attempt.validated_delay_median_ns = delays.at(median_index);
  attempt.validated_delay_max_ns = delays.back();
  attempt.validated_power_min_w = powers.front();
  attempt.validated_power_median_w = powers.at(median_index);
  attempt.validated_power_max_w = powers.back();
}

auto MakeAnalyticalCandidateEvaluation(const htree::analytical_solver::AnalyticalCandidate& candidate,
                                       const std::vector<HTree::LevelPlan>& levels, const htree::BoundaryConstraints& boundary_constraints,
                                       std::size_t final_frontier_count) -> htree::CandidateBuildEvaluation
{
  htree::CandidateBuildEvaluation evaluation;
  evaluation.depth = candidate.depth;
  evaluation.leaf_count = candidate.leaf_count;
  evaluation.boundary_constraints = boundary_constraints;
  evaluation.levels = levels;
  evaluation.success = candidate.materialized_char.has_value();
  evaluation.failure_reason = evaluation.success ? "" : "missing_analytical_materialized_char";
  evaluation.final_frontier_count = final_frontier_count;
  evaluation.candidate_solution_count = final_frontier_count;
  evaluation.feasible_solution_count = final_frontier_count;
  if (candidate.materialized_char.has_value()) {
    evaluation.candidate_frontier_entries = {*candidate.materialized_char};
    evaluation.feasible_frontier_entries = {*candidate.materialized_char};
    evaluation.best_char = *candidate.materialized_char;
  }
  evaluation.topology_pattern_library = candidate.topology_pattern_library;
  return evaluation;
}

auto MakeAnalyticalDepthSummary(const htree::CandidateBuildEvaluation& evaluation, const htree::SinkLoadRegionLegalityResult& sink_legality)
    -> htree::DepthSummary
{
  return htree::DepthSummary{
      .depth = evaluation.depth,
      .leaf_count = evaluation.leaf_count,
      .success = evaluation.success,
      .selected = true,
      .used_explicit_target_depth = false,
      .failure_reason = evaluation.failure_reason,
      .htree_load_group_count = sink_legality.cap_distribution.group_count,
      .htree_load_cap_min_pf = sink_legality.cap_distribution.cap_min_pf,
      .htree_load_cap_max_pf = sink_legality.cap_distribution.cap_max_pf,
      .htree_load_cap_mean_pf = sink_legality.cap_distribution.cap_mean_pf,
      .htree_load_cap_median_pf = sink_legality.cap_distribution.cap_median_pf,
      .final_frontier_count = evaluation.final_frontier_count,
      .candidate_solution_count = evaluation.candidate_solution_count,
      .candidate_frontier_entry_count = evaluation.candidate_frontier_entries.size(),
      .feasible_solution_count = evaluation.feasible_solution_count,
      .feasible_frontier_entry_count = evaluation.feasible_frontier_entries.size(),
      .used_boundary_fallback = false,
      .selected_power_w = evaluation.best_char.has_value() ? evaluation.best_char->get_power() : 0.0,
      .selected_delay_ns = evaluation.best_char.has_value() ? evaluation.best_char->get_delay() : 0.0,
  };
}

auto CollectAnalyticalUnitSegmentChars(const CharBuilder& char_builder, unsigned unit_length_idx) -> std::vector<SegmentChar>
{
  std::vector<SegmentChar> segment_chars;
  for (const auto& segment_char : char_builder.get_segment_chars()) {
    if (segment_char.get_length_idx() == unit_length_idx) {
      segment_chars.push_back(segment_char);
    }
  }
  return segment_chars;
}

auto BuildAnalyticalModelCatalog(const std::vector<SegmentChar>& segment_chars, const std::vector<BufferingPattern>& buffering_patterns,
                                 const CharBuilder& char_builder, AnalyticalHTreeAttempt& attempt)
    -> analytical::AnalyticalCharacterizationResult
{
  analytical::AnalyticalCharacterizationOptions analytical_options;
  analytical_options.require_monotonic_output_slew = false;
  analytical_options.require_monotonic_delay = false;
  analytical_options.require_monotonic_power = false;
  analytical_options.require_monotonic_source_boundary_power = false;
  analytical_options.allow_sparse_constant_fallback = false;
  analytical_options.prefer_exact_structural_cap = true;
  analytical_options.length_unit_um = char_builder.get_wirelength_unit_um();
  analytical_options.routing_layer = char_builder.get_routing_layer();
  analytical_options.wire_width = char_builder.get_wire_width();

  auto char_result = analytical::AnalyticalCharacterization::buildFromSegmentChars(
      segment_chars, buffering_patterns, char_builder.get_slew_lattice(), char_builder.get_cap_lattice(), analytical_options);
  attempt.model_set_count = char_result.model_set_count;
  attempt.rejected_fit_count = char_result.rejected_fit_count;
  attempt.structural_cap_operator_count = char_result.structural_cap_operator_count;
  return char_result;
}

auto TrySolveAnalyticalHTree(const Tree& topology, const std::vector<HTree::LevelPlan>& full_level_plans,
                             const std::vector<unsigned>& depth_candidates, const htree::SegmentFrontierCatalog& segment_frontier_catalog,
                             htree::BufferPatternLibrary& segment_pattern_library,
                             const htree::BoundaryConstraints& search_boundary_constraints,
                             const htree::HTreeFanoutPruningOptions& fanout_pruning_options,
                             const htree::RootDriverCompensationOptions& root_driver_compensation_options, const CharBuilder& char_builder,
                             unsigned char_slew_steps) -> AnalyticalHTreeAttempt
{
  AnalyticalHTreeAttempt attempt;
  if (depth_candidates.empty()) {
    attempt.fallback_reason = "empty_depth_candidates";
    return attempt;
  }

  const auto analytical_segment_chars = CollectAnalyticalUnitSegmentChars(char_builder, kAnalyticalUnitLengthIdx);
  const auto& analytical_buffering_patterns = char_builder.get_buffering_patterns();
  auto char_result = BuildAnalyticalModelCatalog(analytical_segment_chars, analytical_buffering_patterns, char_builder, attempt);
  if (char_result.catalog.empty()) {
    attempt.fallback_reason = char_result.failures.empty() ? "empty_analytical_model_catalog" : char_result.failures.front().reason;
    return attempt;
  }

  htree::SinkLoadRegionLegalityContext sink_load_region_legality_context{
      .result_by_signature = {},
      .max_monotone_failed_level = std::numeric_limits<int>::min(),
      .cap_lattice = char_builder.get_cap_lattice(),
  };
  htree::RootDriverCompensationPass compensation_pass(root_driver_compensation_options);
  std::vector<AnalyticalValidatedCandidate> legal_candidates;
  std::string first_solver_failure;
  std::string first_validation_failure;

  for (const unsigned depth : depth_candidates) {
    auto candidate_levels = htree::MakeCandidateLevelPlans(full_level_plans, depth);
    htree::analytical_solver::AnalyticalSolverRequest solver_request;
    solver_request.levels = &candidate_levels;
    solver_request.segment_frontier_catalog = &segment_frontier_catalog;
    solver_request.segment_pattern_library = &segment_pattern_library;
    solver_request.mutable_segment_pattern_library = &segment_pattern_library;
    solver_request.model_catalog = &char_result.catalog;
    solver_request.boundary_constraints = search_boundary_constraints;
    solver_request.fanout_options = fanout_pruning_options;
    solver_request.slew_lattice = char_builder.get_slew_lattice();
    solver_request.cap_lattice = char_builder.get_cap_lattice();
    solver_request.options.per_level_shortlist_size = kAnalyticalPerLevelShortlistSize;
    solver_request.options.top_k_per_depth = kAnalyticalTopKPerDepth;
    solver_request.options.unit_compose_beam_size = kAnalyticalUnitComposeBeamSize;
    solver_request.options.root_input_slew_ns = root_driver_compensation_options.input_slew_ns;
    solver_request.options.representative_leaf_load_cap_pf = ResolveAnalyticalRepresentativeLeafLoadCapPf(char_builder.get_cap_lattice());
    solver_request.options.use_functional_unit_compose = true;
    solver_request.options.unit_length_idx = kAnalyticalUnitLengthIdx;

    auto solver_result = htree::analytical_solver::SolveAnalyticalHTreeCandidates(solver_request);
    attempt.evaluated_segment_count += solver_result.evaluated_segment_count;
    attempt.generated_candidate_count += solver_result.generated_candidate_count;
    attempt.scored_segment_count += solver_result.scored_segment_count;
    attempt.missing_model_count += solver_result.missing_model_count;
    attempt.decomposition_rejected_count += solver_result.decomposition_rejected_count;
    attempt.metric_evaluation_rejected_count += solver_result.metric_evaluation_rejected_count;
    attempt.domain_slew_rejected_count += solver_result.domain_slew_rejected_count;
    attempt.domain_cap_rejected_count += solver_result.domain_cap_rejected_count;
    attempt.domain_slew_floor_count += solver_result.domain_slew_floor_count;
    attempt.domain_cap_floor_count += solver_result.domain_cap_floor_count;
    attempt.max_domain_rejected_cap_pf = std::max(attempt.max_domain_rejected_cap_pf, solver_result.max_domain_rejected_cap_pf);
    attempt.empty_shortlist_count += solver_result.empty_shortlist_count;
    attempt.materialization_attempt_count += solver_result.materialization_attempt_count;
    attempt.root_fanout_rejected_count += solver_result.root_fanout_rejected_count;
    attempt.lattice_rejected_count += solver_result.lattice_rejected_count;
    attempt.diagnostic_library_hit_count += solver_result.diagnostic_library_hit_count;
    attempt.diagnostic_frontier_hit_count += solver_result.diagnostic_frontier_hit_count;
    attempt.diagnostic_decomposed_count += solver_result.diagnostic_decomposed_count;
    attempt.diagnostic_scored_count += solver_result.diagnostic_scored_count;
    attempt.diagnostic_shortlisted_count += solver_result.diagnostic_shortlisted_count;
    attempt.diagnostic_generated_candidate_count += solver_result.diagnostic_generated_candidate_count;
    attempt.diagnostic_direct_candidate_count += solver_result.diagnostic_direct_candidate_count;
    if (solver_result.diagnostic_direct_candidate_count > 0U) {
      attempt.diagnostic_direct_delay_ns = solver_result.diagnostic_direct_delay_ns;
      attempt.diagnostic_direct_power_w = solver_result.diagnostic_direct_power_w;
      attempt.diagnostic_direct_root_cap_pf = solver_result.diagnostic_direct_root_cap_pf;
      attempt.diagnostic_direct_input_slew_idx = solver_result.diagnostic_direct_input_slew_idx;
      attempt.diagnostic_direct_output_slew_idx = solver_result.diagnostic_direct_output_slew_idx;
      attempt.diagnostic_direct_driven_cap_idx = solver_result.diagnostic_direct_driven_cap_idx;
    }
    if (attempt.first_empty_reason.empty() && !solver_result.first_empty_reason.empty()) {
      attempt.first_empty_level_index = solver_result.first_empty_level_index;
      attempt.first_empty_length_idx = solver_result.first_empty_length_idx;
      attempt.first_empty_reason = solver_result.first_empty_reason;
    }
    if (!solver_result.success) {
      if (first_solver_failure.empty()) {
        first_solver_failure = solver_result.failure_reason.empty() ? "analytical_solver_failed" : solver_result.failure_reason;
      }
      continue;
    }

    for (auto& candidate : solver_result.candidates) {
      htree::RootDriverCompensationPass candidate_compensation_pass(root_driver_compensation_options);
      auto validation = htree::analytical_solver::ValidateAnalyticalCandidate(
          candidate, htree::analytical_solver::AnalyticalValidationRequest{
                         .topology = &topology,
                         .segment_pattern_library = &segment_pattern_library,
                         .sink_load_region_legality_context = &sink_load_region_legality_context,
                         .root_driver_compensation_pass = &candidate_compensation_pass,
                         .validate_sink_load_region = true,
                         .validate_root_driver_compensation = root_driver_compensation_options.enabled,
                     });
      if (!validation.legal) {
        if (first_validation_failure.empty()) {
          first_validation_failure
              = validation.failure_reason.empty() ? "analytical_candidate_validation_failed" : validation.failure_reason;
        }
        continue;
      }
      ++attempt.validated_candidate_count;
      if (candidate.materialized_char.has_value() && validation.root_driver_compensation.enabled) {
        candidate.materialized_char->set_root_driver_compensation(validation.root_driver_compensation.cell_delay_ns,
                                                                  validation.root_driver_compensation.cell_power_w);
      }
      legal_candidates.push_back(AnalyticalValidatedCandidate{
          .candidate = std::move(candidate),
          .validation = std::move(validation),
          .original_index = legal_candidates.size(),
      });
    }
  }

  attempt.root_driver_compensation_stats = compensation_pass.get_stats();
  if (legal_candidates.empty()) {
    if (!first_validation_failure.empty()) {
      attempt.fallback_reason = first_validation_failure;
    } else if (!first_solver_failure.empty()) {
      attempt.fallback_reason = first_solver_failure;
    } else {
      attempt.fallback_reason = "no_legal_analytical_candidate";
    }
    return attempt;
  }

  RecordAnalyticalValidatedDistribution(legal_candidates, attempt);
  const auto* selected_candidate = SelectAnalyticalParetoPowerGuardedMinDelay(legal_candidates, attempt);
  if (selected_candidate == nullptr || !selected_candidate->candidate.materialized_char.has_value()) {
    attempt.fallback_reason = "missing_best_analytical_candidate";
    return attempt;
  }

  attempt.selected = true;
  attempt.selected_evaluation = MakeAnalyticalCandidateEvaluation(
      selected_candidate->candidate, htree::MakeCandidateLevelPlans(full_level_plans, selected_candidate->candidate.depth),
      search_boundary_constraints, attempt.validated_pareto_count);
  attempt.selected_sink_load_region_legality = selected_candidate->validation.sink_load_region;
  attempt.selected_compensation_detail = selected_candidate->validation.root_driver_compensation;
  attempt.selected_summary = MakeAnalyticalDepthSummary(attempt.selected_evaluation, attempt.selected_sink_load_region_legality);
  attempt.root_driver_compensation_stats = compensation_pass.get_stats();
  (void) char_slew_steps;
  return attempt;
}

auto ApplyAnalyticalRootDriverStats(htree::DepthSearchResult& exploration, const AnalyticalHTreeAttempt& attempt,
                                    const htree::RootDriverCompensationOptions& compensation_options) -> void
{
  exploration.root_driver_compensation_stats = attempt.root_driver_compensation_stats;
  exploration.root_driver_compensation_stats.enabled = compensation_options.enabled;
  exploration.root_driver_compensation_stats.input_slew_ns = compensation_options.input_slew_ns;
  exploration.root_driver_compensation_stats.clock_period_ns = compensation_options.clock_period_ns;
  exploration.root_driver_compensation_stats.method = compensation_options.enabled ? "direct" : "disabled";
}

}  // namespace

auto HTree::build(Net& root_net) -> BuildResult
{
  return build(root_net, BuildOptions{});
}

auto HTree::build(Net& root_net, const BuildOptions& options) -> BuildResult
{
  BuildResult result;
  result.log_context = options.log_context;
  result.object_name_prefix = options.object_name_prefix;
  result.root_net = &root_net;
  result.root_output_pin = root_net.get_driver();
  result.root_inst = result.root_output_pin == nullptr ? nullptr : result.root_output_pin->get_inst();
  if (result.root_output_pin == nullptr) {
    result.failure_reason = "missing_root_driver_pin";
    LOG_WARNING << "HTree: build skipped because root net " << root_net.get_name() << " has no driver pin.";
    return result;
  }

  const auto loads = root_net.get_loads();
  if (loads.empty()) {
    result.failure_reason = "empty_root_net_loads";
    LOG_WARNING << "HTree: build skipped because root net " << root_net.get_name() << " has no loads.";
    return result;
  }
  auto build_stage = SCHEMA_WRITER_INST.beginStage("HTree", "build");
  const int32_t dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});

  BiPartitionConfig topology_config;
  topology_config.htree_topology_tolerance
      = std::max(0.0, options.htree_topology_tolerance.value_or(CONFIG_INST.get_htree_topology_tolerance()));
  topology_config.max_leaf_load_count = CONFIG_INST.get_max_fanout();
  result.topology
      = TopologyGen::build(loads, TopologyGen::BuildOptions{
                                      .partition_config = topology_config,
                                      .target_depth = std::nullopt,
                                      .fixed_root_location = options.fixed_topology_root_location,
                                      .dbu_per_um = dbu_per_um,
                                      .load_count_kind = options.topology_loads_are_local_buffers ? TopologyGen::LoadCountKind::kLocalBuffer
                                                                                                  : TopologyGen::LoadCountKind::kSink,
                                      .clock_name = options.log_context.clock_name,
                                      .clock_net_name = options.log_context.clock_net_name,
                                      .sink_domain = options.log_context.sink_domain,
                                      .stage = options.log_context.stage,
                                  });
  const auto levels = result.topology.levels();
  if (levels.size() <= 1U) {
    LOG_WARNING << "HTree: topology has no H-tree levels after generation.";
    build_stage.skip({{"reason", "no_h_tree_levels"}});
    return result;
  }

  build_stage.markRunning("characterization");
  CharacterizationLibrary local_char_library;
  auto* char_library = options.characterization_library == nullptr ? &local_char_library : options.characterization_library;
  const auto char_options = CharacterizationLibrary::buildRuntimeOptions();
  const auto char_flow = htree::RunCharacterizationFlow(result.topology, dbu_per_um, char_options, result, *char_library, options);
  if (!char_flow.success) {
    build_stage.failed({{"reason", char_flow.failure_reason}});
    return result;
  }
  const auto& char_builder = char_library->getCharBuilder();

  const auto base_boundary_constraints = htree::ResolveBoundaryConstraints(options, char_builder);
  result.force_branch_buffer = base_boundary_constraints.force_branch_buffer;
  result.root_driver_sizing_enabled = options.enable_root_driver_sizing;
  result.target_depth = options.target_depth;
  const bool strict_root_boundary_closure = options.enable_root_driver_sizing;
  const auto search_boundary_constraints = ResolvePatternSearchBoundaryConstraints(base_boundary_constraints, strict_root_boundary_closure);

  const auto full_level_plans = htree::BuildLevelPlans(result.topology, char_flow.length_step_um, dbu_per_um);
  if (full_level_plans.empty()) {
    LOG_WARNING << "HTree: failed to derive H-tree level plans from topology.";
    build_stage.failed({{"reason", "empty_level_plans"}});
    return result;
  }

  const auto max_depth = static_cast<unsigned>(full_level_plans.size());
  const auto depth_candidates = htree::ResolveDepthCandidates(max_depth, options);
  if (depth_candidates.empty()) {
    LOG_WARNING << "HTree: no depth candidates were resolved from topology.";
    build_stage.failed({{"reason", "empty_depth_candidates"}});
    return result;
  }
  result.depth_explore_window = static_cast<unsigned>(depth_candidates.size());

  htree::BufferPatternLibrary segment_pattern_library;
  for (const auto& pattern : char_builder.get_buffering_patterns()) {
    segment_pattern_library.add(pattern);
  }

  auto segment_frontier_request
      = htree::MakeHTreeSegmentFrontierRequest(htree::CollectRequiredLengthIndices(full_level_plans), search_boundary_constraints);
  htree::SegmentFrontierCatalog segment_frontier_catalog;
  {
    auto segment_frontier_stage = SCHEMA_WRITER_INST.beginStage(
        "HTree", "Synthesize segment frontiers",
        {
            {"segment_chars", std::to_string(char_builder.get_segment_chars().size())},
            {"required_length_indices", std::to_string(segment_frontier_request.required_length_indices.size())},
        });
    segment_frontier_catalog
        = htree::SynthesizeSegmentFrontiers(char_builder.get_segment_chars(), segment_pattern_library, segment_frontier_request);
    if (segment_frontier_catalog.empty()) {
      LOG_WARNING << "HTree: segment frontier synthesis failed for the required aligned lengths.";
      segment_frontier_stage.failed({{"reason", "missing_required_segment_frontiers"}});
      build_stage.failed({{"reason", "missing_required_segment_frontiers"}});
      return result;
    }
    segment_frontier_stage.finished({
        {"length_sets", std::to_string(segment_frontier_catalog.lengthCount())},
        {"frontier_entries", std::to_string(segment_frontier_catalog.countEntries(segment_frontier_request.required_kinds))},
    });
  }

  const auto [root_driver_clock_period_ns, root_driver_clock_period_source] = ResolveRootDriverClockPeriod(options);
  const htree::RootDriverCompensationOptions root_driver_compensation_options{
      .enabled = options.enable_root_driver_sizing,
      .input_slew_ns = ResolveRootDriverCompensationInputSlewNs(options, char_builder.get_max_slew()),
      .clock_period_ns = root_driver_clock_period_ns,
      .cap_lattice = char_builder.get_cap_lattice(),
      .slew_lattice = char_builder.get_slew_lattice(),
      .fallback_cell_master = result.root_inst != nullptr ? result.root_inst->get_cell_master() : "",
      .strict_boundary_closure = strict_root_boundary_closure,
  };
  const htree::HTreeFanoutPruningOptions fanout_pruning_options{
      .max_fanout = CONFIG_INST.get_max_fanout(),
  };
  result.analytical_mode_enabled = options.enable_analytical_solver;
  if (options.enable_analytical_solver) {
    auto analytical_stage = SCHEMA_WRITER_INST.beginStage("HTree", "Try analytical topology candidates",
                                                          {
                                                              {"depth_candidates", std::to_string(depth_candidates.size())},
                                                              {"max_depth", std::to_string(max_depth)},
                                                              {"per_level_shortlist", std::to_string(kAnalyticalPerLevelShortlistSize)},
                                                              {"top_k_per_depth", std::to_string(kAnalyticalTopKPerDepth)},
                                                          });
    const auto analytical_attempt = TrySolveAnalyticalHTree(result.topology, full_level_plans, depth_candidates, segment_frontier_catalog,
                                                            segment_pattern_library, search_boundary_constraints, fanout_pruning_options,
                                                            root_driver_compensation_options, char_builder, result.char_slew_steps);
    result.analytical_model_set_count = analytical_attempt.model_set_count;
    result.analytical_rejected_fit_count = analytical_attempt.rejected_fit_count;
    result.analytical_structural_cap_operator_count = analytical_attempt.structural_cap_operator_count;
    result.analytical_evaluated_segment_count = analytical_attempt.evaluated_segment_count;
    result.analytical_generated_candidate_count = analytical_attempt.generated_candidate_count;
    result.analytical_validated_candidate_count = analytical_attempt.validated_candidate_count;
    result.analytical_validated_pareto_count = analytical_attempt.validated_pareto_count;
    result.analytical_selected_pareto_power_rank = analytical_attempt.selected_pareto_power_rank;
    result.analytical_validated_delay_min_ns = analytical_attempt.validated_delay_min_ns;
    result.analytical_validated_delay_median_ns = analytical_attempt.validated_delay_median_ns;
    result.analytical_validated_delay_max_ns = analytical_attempt.validated_delay_max_ns;
    result.analytical_validated_power_min_w = analytical_attempt.validated_power_min_w;
    result.analytical_validated_power_median_w = analytical_attempt.validated_power_median_w;
    result.analytical_validated_power_max_w = analytical_attempt.validated_power_max_w;

    if (analytical_attempt.selected && analytical_attempt.selected_evaluation.best_char.has_value()) {
      result.analytical_mode_selected = true;
      analytical_stage.finished({
          {"selected_depth", std::to_string(analytical_attempt.selected_evaluation.depth)},
          {"model_sets", std::to_string(analytical_attempt.model_set_count)},
          {"rejected_fits", std::to_string(analytical_attempt.rejected_fit_count)},
          {"evaluated_segments", std::to_string(analytical_attempt.evaluated_segment_count)},
          {"scored_segments", std::to_string(analytical_attempt.scored_segment_count)},
          {"missing_models", std::to_string(analytical_attempt.missing_model_count)},
          {"decomposition_rejections", std::to_string(analytical_attempt.decomposition_rejected_count)},
          {"metric_rejections", std::to_string(analytical_attempt.metric_evaluation_rejected_count)},
          {"domain_slew_rejections", std::to_string(analytical_attempt.domain_slew_rejected_count)},
          {"domain_cap_rejections", std::to_string(analytical_attempt.domain_cap_rejected_count)},
          {"domain_slew_floors", std::to_string(analytical_attempt.domain_slew_floor_count)},
          {"domain_cap_floors", std::to_string(analytical_attempt.domain_cap_floor_count)},
          {"max_domain_rejected_cap_pf", std::to_string(analytical_attempt.max_domain_rejected_cap_pf)},
          {"empty_shortlists", std::to_string(analytical_attempt.empty_shortlist_count)},
          {"materialization_attempts", std::to_string(analytical_attempt.materialization_attempt_count)},
          {"root_fanout_rejections", std::to_string(analytical_attempt.root_fanout_rejected_count)},
          {"lattice_rejections", std::to_string(analytical_attempt.lattice_rejected_count)},
          {"diagnostic_library_hits", std::to_string(analytical_attempt.diagnostic_library_hit_count)},
          {"diagnostic_frontier_hits", std::to_string(analytical_attempt.diagnostic_frontier_hit_count)},
          {"diagnostic_decomposed", std::to_string(analytical_attempt.diagnostic_decomposed_count)},
          {"diagnostic_scored", std::to_string(analytical_attempt.diagnostic_scored_count)},
          {"diagnostic_shortlisted", std::to_string(analytical_attempt.diagnostic_shortlisted_count)},
          {"diagnostic_generated_candidates", std::to_string(analytical_attempt.diagnostic_generated_candidate_count)},
          {"diagnostic_direct_candidates", std::to_string(analytical_attempt.diagnostic_direct_candidate_count)},
          {"diagnostic_direct_delay_ns", std::to_string(analytical_attempt.diagnostic_direct_delay_ns)},
          {"diagnostic_direct_power_w", std::to_string(analytical_attempt.diagnostic_direct_power_w)},
          {"diagnostic_direct_root_cap_pf", std::to_string(analytical_attempt.diagnostic_direct_root_cap_pf)},
          {"diagnostic_direct_input_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_input_slew_idx)},
          {"diagnostic_direct_output_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_output_slew_idx)},
          {"diagnostic_direct_driven_cap_idx", std::to_string(analytical_attempt.diagnostic_direct_driven_cap_idx)},
          {"generated_candidates", std::to_string(analytical_attempt.generated_candidate_count)},
          {"validated_candidates", std::to_string(analytical_attempt.validated_candidate_count)},
          {"validated_pareto_candidates", std::to_string(analytical_attempt.validated_pareto_count)},
          {"selected_pareto_power_rank", std::to_string(analytical_attempt.selected_pareto_power_rank)},
          {"validated_delay_min_ns", std::to_string(analytical_attempt.validated_delay_min_ns)},
          {"validated_delay_median_ns", std::to_string(analytical_attempt.validated_delay_median_ns)},
          {"validated_delay_max_ns", std::to_string(analytical_attempt.validated_delay_max_ns)},
          {"validated_power_min_w", std::to_string(analytical_attempt.validated_power_min_w)},
          {"validated_power_median_w", std::to_string(analytical_attempt.validated_power_median_w)},
          {"validated_power_max_w", std::to_string(analytical_attempt.validated_power_max_w)},
      });

      auto selected_evaluation = analytical_attempt.selected_evaluation;
      auto selected_summary = analytical_attempt.selected_summary;
      result.depth_candidate_count = depth_candidates.size();
      result.selected_depth = selected_evaluation.depth;
      result.best_char = *selected_evaluation.best_char;

      htree::DepthSearchResult analytical_exploration;
      ApplyAnalyticalRootDriverStats(analytical_exploration, analytical_attempt, root_driver_compensation_options);
      ApplyRootDriverCompensationResult(result, analytical_exploration, analytical_attempt.selected_compensation_detail, *result.best_char);
      result.root_driver_compensation.clock_period_source = root_driver_clock_period_source;
      result.levels = selected_evaluation.levels;
      result.selected_final_frontier_count = selected_summary.final_frontier_count;
      result.selected_candidate_solution_count = selected_summary.candidate_solution_count;
      result.selected_candidate_frontier_entry_count = selected_summary.candidate_frontier_entry_count;
      result.selected_feasible_solution_count = selected_summary.feasible_solution_count;
      result.selected_feasible_frontier_entry_count = selected_summary.feasible_frontier_entry_count;
      result.min_top_input_slew_ns = selected_evaluation.boundary_constraints.min_top_input_slew_ns;
      result.top_input_slew_covering_idx = selected_evaluation.boundary_constraints.top_input_slew_covering_idx;
      result.htree_load_group_count = selected_summary.htree_load_group_count;
      result.htree_load_cap_min_pf = selected_summary.htree_load_cap_min_pf;
      result.htree_load_cap_max_pf = selected_summary.htree_load_cap_max_pf;
      result.htree_load_cap_mean_pf = selected_summary.htree_load_cap_mean_pf;
      result.htree_load_cap_median_pf = selected_summary.htree_load_cap_median_pf;

      result.best_pattern = selected_evaluation.topology_pattern_library.materialize(result.best_char->get_pattern_id());
      ApplySelectedPatternToLevelPlans(result, segment_pattern_library);
      const std::string selected_root_driver_cell_master = ResolveSelectedRootDriverCellMaster(result.levels);
      if (options.enable_root_driver_sizing && !htree::ValidateRootDriverSizing(result, selected_root_driver_cell_master)) {
        result.failure_reason = "root_driver_sizing_precheck_failed";
        build_stage.failed({{"reason", result.failure_reason}});
        return result;
      }

      {
        auto embedding_stage = SCHEMA_WRITER_INST.beginStage("HTree", "Build selected embedding",
                                                             {
                                                                 {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
                                                                 {"selected_levels", std::to_string(result.levels.size())},
                                                                 {"selection_engine", "analytical"},
                                                             });
        htree::BuildEmbedding(result, segment_pattern_library);
        result.success = result.failure_reason.empty() && result.best_char.has_value() && result.best_pattern.has_value()
                         && result.root_output_pin != nullptr && result.root_net != nullptr;
        if (result.success && options.enable_root_driver_sizing) {
          LOG_FATAL_IF(!htree::ApplyRootDriverSizing(result, selected_root_driver_cell_master))
              << "HTree: prevalidated root-driver sizing failed during analytical embedding construction.";
        } else if (result.success && result.root_inst != nullptr) {
          result.selected_root_driver_cell_master = result.root_inst->get_cell_master();
        }
        if (result.success) {
          embedding_stage.finished({
              {"inserted_insts", std::to_string(result.inserted_insts.size())},
              {"inserted_nets", std::to_string(result.inserted_nets.size())},
              {"pruned_leaf_single_load_buffers", std::to_string(result.pruned_leaf_single_load_buffers)},
          });
        } else {
          embedding_stage.failed({{"reason", result.failure_reason.empty() ? "incomplete_embedding_build" : result.failure_reason}});
        }
      }

      {
        auto summary_stage = SCHEMA_WRITER_INST.beginStage("HTree", "Emit synthesis summary");
        htree::LogSynthesisSummary(result, selected_evaluation, selected_summary);
        summary_stage.finished();
      }
      if (result.success) {
        build_stage.finished({{"selection_engine", "analytical"}});
      } else {
        build_stage.failed({{"reason", result.failure_reason.empty() ? "incomplete_embedding_build" : result.failure_reason},
                            {"selection_engine", "analytical"}});
      }
      return result;
    }

    result.analytical_fallback_reason
        = analytical_attempt.fallback_reason.empty() ? "analytical_candidate_unavailable" : analytical_attempt.fallback_reason;
    analytical_stage.failed({
        {"selected_depth", "none"},
        {"reason", result.analytical_fallback_reason},
        {"model_sets", std::to_string(analytical_attempt.model_set_count)},
        {"rejected_fits", std::to_string(analytical_attempt.rejected_fit_count)},
        {"evaluated_segments", std::to_string(analytical_attempt.evaluated_segment_count)},
        {"scored_segments", std::to_string(analytical_attempt.scored_segment_count)},
        {"missing_models", std::to_string(analytical_attempt.missing_model_count)},
        {"decomposition_rejections", std::to_string(analytical_attempt.decomposition_rejected_count)},
        {"metric_rejections", std::to_string(analytical_attempt.metric_evaluation_rejected_count)},
        {"domain_slew_rejections", std::to_string(analytical_attempt.domain_slew_rejected_count)},
        {"domain_cap_rejections", std::to_string(analytical_attempt.domain_cap_rejected_count)},
        {"domain_slew_floors", std::to_string(analytical_attempt.domain_slew_floor_count)},
        {"domain_cap_floors", std::to_string(analytical_attempt.domain_cap_floor_count)},
        {"max_domain_rejected_cap_pf", std::to_string(analytical_attempt.max_domain_rejected_cap_pf)},
        {"empty_shortlists", std::to_string(analytical_attempt.empty_shortlist_count)},
        {"materialization_attempts", std::to_string(analytical_attempt.materialization_attempt_count)},
        {"root_fanout_rejections", std::to_string(analytical_attempt.root_fanout_rejected_count)},
        {"lattice_rejections", std::to_string(analytical_attempt.lattice_rejected_count)},
        {"diagnostic_library_hits", std::to_string(analytical_attempt.diagnostic_library_hit_count)},
        {"diagnostic_frontier_hits", std::to_string(analytical_attempt.diagnostic_frontier_hit_count)},
        {"diagnostic_decomposed", std::to_string(analytical_attempt.diagnostic_decomposed_count)},
        {"diagnostic_scored", std::to_string(analytical_attempt.diagnostic_scored_count)},
        {"diagnostic_shortlisted", std::to_string(analytical_attempt.diagnostic_shortlisted_count)},
        {"diagnostic_generated_candidates", std::to_string(analytical_attempt.diagnostic_generated_candidate_count)},
        {"diagnostic_direct_candidates", std::to_string(analytical_attempt.diagnostic_direct_candidate_count)},
        {"diagnostic_direct_delay_ns", std::to_string(analytical_attempt.diagnostic_direct_delay_ns)},
        {"diagnostic_direct_power_w", std::to_string(analytical_attempt.diagnostic_direct_power_w)},
        {"diagnostic_direct_root_cap_pf", std::to_string(analytical_attempt.diagnostic_direct_root_cap_pf)},
        {"diagnostic_direct_input_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_input_slew_idx)},
        {"diagnostic_direct_output_slew_idx", std::to_string(analytical_attempt.diagnostic_direct_output_slew_idx)},
        {"diagnostic_direct_driven_cap_idx", std::to_string(analytical_attempt.diagnostic_direct_driven_cap_idx)},
        {"first_empty_level",
         analytical_attempt.first_empty_reason.empty() ? "none" : std::to_string(analytical_attempt.first_empty_level_index)},
        {"first_empty_length_idx",
         analytical_attempt.first_empty_reason.empty() ? "none" : std::to_string(analytical_attempt.first_empty_length_idx)},
        {"first_empty_reason", analytical_attempt.first_empty_reason.empty() ? "none" : analytical_attempt.first_empty_reason},
        {"generated_candidates", std::to_string(analytical_attempt.generated_candidate_count)},
        {"validated_candidates", std::to_string(analytical_attempt.validated_candidate_count)},
        {"validated_pareto_candidates", std::to_string(analytical_attempt.validated_pareto_count)},
        {"selected_pareto_power_rank", std::to_string(analytical_attempt.selected_pareto_power_rank)},
        {"validated_delay_min_ns", std::to_string(analytical_attempt.validated_delay_min_ns)},
        {"validated_delay_median_ns", std::to_string(analytical_attempt.validated_delay_median_ns)},
        {"validated_delay_max_ns", std::to_string(analytical_attempt.validated_delay_max_ns)},
        {"validated_power_min_w", std::to_string(analytical_attempt.validated_power_min_w)},
        {"validated_power_median_w", std::to_string(analytical_attempt.validated_power_median_w)},
        {"validated_power_max_w", std::to_string(analytical_attempt.validated_power_max_w)},
    });
    schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "HTree",
                           "analytical H-tree candidate selection did not produce a validated candidate.",
                           {
                               {"reason", result.analytical_fallback_reason},
                               {"model_sets", std::to_string(result.analytical_model_set_count)},
                               {"generated_candidates", std::to_string(result.analytical_generated_candidate_count)},
                               {"validated_candidates", std::to_string(result.analytical_validated_candidate_count)},
                           });
    result.failure_reason = result.analytical_fallback_reason;
    build_stage.failed({{"reason", result.failure_reason}, {"selection_engine", "analytical"}});
    return result;
  }

  htree::DepthSearchResult exploration;
  {
    auto depth_search_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Search topology depth candidates",
                                        {
                                            {"depth_candidates", std::to_string(depth_candidates.size())},
                                            {"max_depth", std::to_string(max_depth)},
                                            {"segment_frontier_length_sets", std::to_string(segment_frontier_catalog.lengthCount())},
                                        });
    exploration = htree::SearchTopologyDepthCandidates(result.topology, full_level_plans, depth_candidates, segment_frontier_catalog,
                                                       segment_pattern_library, search_boundary_constraints, char_builder.get_cap_lattice(),
                                                       result.char_slew_steps, options.target_depth.has_value(),
                                                       root_driver_compensation_options, fanout_pruning_options);
    depth_search_stage.finished({
        {"evaluated_depths", std::to_string(exploration.depth_summaries.size())},
        {"global_feasible_refs", std::to_string(exploration.global_feasible_pool.size())},
        {"global_candidate_refs", std::to_string(exploration.global_candidate_pool.size())},
        {"compensated_candidates", std::to_string(exploration.root_driver_compensation_stats.compensated_candidate_count)},
    });
  }
  result.depth_candidate_count = exploration.depth_summaries.size();

  htree::CandidateCharRefFilterResult covered_global_feasible_pool;
  htree::CandidateCharRefFilterResult covered_global_candidate_pool;
  {
    auto coverage_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Filter global sink-load coverage",
                                        {
                                            {"global_feasible_refs", std::to_string(exploration.global_feasible_pool.size())},
                                            {"global_candidate_refs", std::to_string(exploration.global_candidate_pool.size())},
                                        });
    covered_global_feasible_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
        exploration.global_feasible_pool, exploration.candidate_evaluations, result.topology, segment_pattern_library,
        exploration.sink_load_region_legality_context);
    covered_global_candidate_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
        exploration.global_candidate_pool, exploration.candidate_evaluations, result.topology, segment_pattern_library,
        exploration.sink_load_region_legality_context);
    coverage_stage.finished({
        {"covered_feasible_refs", std::to_string(covered_global_feasible_pool.entries.size())},
        {"covered_candidate_refs", std::to_string(covered_global_candidate_pool.entries.size())},
        {"first_feasible_failure",
         covered_global_feasible_pool.first_failure_reason.empty() ? "none" : covered_global_feasible_pool.first_failure_reason},
        {"first_candidate_failure",
         covered_global_candidate_pool.first_failure_reason.empty() ? "none" : covered_global_candidate_pool.first_failure_reason},
    });
  }

  std::vector<htree::CandidateCharRef> per_depth_feasible_pareto_pool;
  std::optional<htree::CandidateCharRef> selected_feasible_ref;
  std::optional<htree::CandidateCharRef> selected_fallback_ref;
  {
    auto selection_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Select global topology",
                                        {
                                            {"covered_feasible_refs", std::to_string(covered_global_feasible_pool.entries.size())},
                                            {"covered_candidate_refs", std::to_string(covered_global_candidate_pool.entries.size())},
                                        });
    per_depth_feasible_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_feasible_pool.entries);
    selected_feasible_ref = htree::SelectBestGlobalEntry(per_depth_feasible_pareto_pool);
    std::size_t per_depth_candidate_pareto_count = 0U;
    if (!selected_feasible_ref.has_value()) {
      const auto per_depth_candidate_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_candidate_pool.entries);
      per_depth_candidate_pareto_count = per_depth_candidate_pareto_pool.size();
      selected_fallback_ref = htree::SelectBestGlobalEntry(per_depth_candidate_pareto_pool);
    }
    std::string selected_from = "none";
    if (selected_feasible_ref.has_value()) {
      selected_from = "strict_feasible";
    } else if (selected_fallback_ref.has_value()) {
      selected_from = "fallback";
    }
    selection_stage.finished({
        {"feasible_pareto_refs", std::to_string(per_depth_feasible_pareto_pool.size())},
        {"candidate_pareto_refs", std::to_string(per_depth_candidate_pareto_count)},
        {"selected_from", selected_from},
    });
  }
  const auto selected_ref = selected_feasible_ref.has_value() ? selected_feasible_ref : selected_fallback_ref;
  if (!selected_ref.has_value() || selected_ref->entry == nullptr) {
    if (!covered_global_candidate_pool.first_failure_reason.empty()) {
      result.failure_reason = covered_global_candidate_pool.first_failure_reason;
    } else {
      result.failure_reason = exploration.global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    }
    LOG_WARNING << "HTree: failed to select a best H-tree characterization entry across depth candidates.";
    build_stage.failed({{"reason", result.failure_reason}, {"depth_candidates", std::to_string(depth_candidates.size())}});
    return result;
  }

  const std::size_t selected_candidate_index = selected_ref->candidate_index;
  auto& selected_evaluation = exploration.candidate_evaluations.at(selected_candidate_index);
  auto& selected_summary = exploration.depth_summaries.at(selected_candidate_index);
  selected_summary.selected = true;
  selected_summary.selected_power_w = selected_ref->entry->get_power();
  selected_summary.selected_delay_ns = selected_ref->entry->get_delay();
  htree::SinkLoadRegionLegalityResult selected_sink_load_region_legality;
  {
    auto selected_legality_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Resolve selected sink-load legality",
                                        {
                                            {"selected_depth", std::to_string(selected_evaluation.depth)},
                                            {"selected_pattern_id", std::to_string(selected_ref->entry->get_pattern_id().pack())},
                                        });
    selected_sink_load_region_legality = htree::ResolveSinkLoadRegionLegality(
        result.topology, selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library, segment_pattern_library,
        exploration.sink_load_region_legality_context);
    selected_legality_stage.finished({
        {"legal", selected_sink_load_region_legality.legal ? "true" : "false"},
        {"required_leaf_load_cap_idx", selected_sink_load_region_legality.required_leaf_load_cap_covering_idx.has_value()
                                           ? std::to_string(*selected_sink_load_region_legality.required_leaf_load_cap_covering_idx)
                                           : "none"},
        {"failure_reason",
         selected_sink_load_region_legality.failure_reason.empty() ? "none" : selected_sink_load_region_legality.failure_reason},
    });
  }
  if (!selected_sink_load_region_legality.legal) {
    result.failure_reason = "sink_load_region_legality_missing";
    LOG_WARNING << "HTree: selected global frontier entry is missing sink-load-region legality coverage.";
    build_stage.failed({{"reason", result.failure_reason}});
    return result;
  }
  selected_summary.htree_load_group_count = selected_sink_load_region_legality.cap_distribution.group_count;
  selected_summary.htree_load_cap_min_pf = selected_sink_load_region_legality.cap_distribution.cap_min_pf;
  selected_summary.htree_load_cap_max_pf = selected_sink_load_region_legality.cap_distribution.cap_max_pf;
  selected_summary.htree_load_cap_mean_pf = selected_sink_load_region_legality.cap_distribution.cap_mean_pf;
  selected_summary.htree_load_cap_median_pf = selected_sink_load_region_legality.cap_distribution.cap_median_pf;

  result.selected_depth = selected_evaluation.depth;
  result.best_char = *selected_ref->entry;
  htree::RootDriverCompensationPass selected_compensation_pass(root_driver_compensation_options);
  htree::RootDriverCompensationDetail selected_compensation_detail;
  {
    auto selected_compensation_stage
        = SCHEMA_WRITER_INST.beginStage("HTree", "Resolve selected root-driver compensation",
                                        {
                                            {"selected_pattern_id", std::to_string(selected_ref->entry->get_pattern_id().pack())},
                                            {"root_driver_sizing_enabled", options.enable_root_driver_sizing ? "true" : "false"},
                                        });
    selected_compensation_detail = selected_compensation_pass.evaluate(
        selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library, segment_pattern_library, result.topology);
    selected_compensation_stage.finished({
        {"valid", selected_compensation_detail.valid ? "true" : "false"},
        {"cell_master", selected_compensation_detail.cell_master.empty() ? "none" : selected_compensation_detail.cell_master},
        {"load_cap_pf", std::to_string(selected_compensation_detail.load_cap_pf)},
    });
  }
  ApplyRootDriverCompensationResult(result, exploration, selected_compensation_detail, *selected_ref->entry);
  result.root_driver_compensation.clock_period_source = root_driver_clock_period_source;
  result.levels = selected_evaluation.levels;
  result.selected_final_frontier_count = selected_summary.final_frontier_count;
  result.selected_candidate_solution_count = selected_summary.candidate_solution_count;
  result.selected_candidate_frontier_entry_count = selected_summary.candidate_frontier_entry_count;
  result.selected_feasible_solution_count = selected_summary.feasible_solution_count;
  result.selected_feasible_frontier_entry_count = selected_summary.feasible_frontier_entry_count;
  result.min_top_input_slew_ns = selected_evaluation.boundary_constraints.min_top_input_slew_ns;
  result.top_input_slew_covering_idx = selected_evaluation.boundary_constraints.top_input_slew_covering_idx;
  result.htree_load_group_count = selected_summary.htree_load_group_count;
  result.htree_load_cap_min_pf = selected_summary.htree_load_cap_min_pf;
  result.htree_load_cap_max_pf = selected_summary.htree_load_cap_max_pf;
  result.htree_load_cap_mean_pf = selected_summary.htree_load_cap_mean_pf;
  result.htree_load_cap_median_pf = selected_summary.htree_load_cap_median_pf;

  if (!selected_feasible_ref.has_value()) {
    result.used_boundary_fallback = true;
    result.boundary_fallback_reason = "no_strict_boundary_feasible_solution_any_depth";
    result.boundary_fallback_score
        = htree::CalcBoundaryFallbackScore(*result.best_char, selected_evaluation.boundary_constraints, result.char_slew_steps);

    schema::EmitDiagnostic(
        schema::DiagnosticLevel::kFallback, "HTree",
        "no depth candidate satisfied caller boundary constraints; selected fallback solution from the global candidate pool.",
        {
            {"reason", result.boundary_fallback_reason},
            {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
            {"fallback_score", std::to_string(result.boundary_fallback_score.value_or(0.0))},
            {"selected_top_input_slew_idx", std::to_string(result.best_char->get_input_slew_idx())},
            {"selected_leaf_load_cap_idx", std::to_string(result.best_char->get_leaf_load_cap_idx())},
        });
  }

  result.best_pattern = selected_evaluation.topology_pattern_library.materialize(result.best_char->get_pattern_id());
  ApplySelectedPatternToLevelPlans(result, segment_pattern_library);
  const std::string selected_root_driver_cell_master = ResolveSelectedRootDriverCellMaster(result.levels);
  if (options.enable_root_driver_sizing && !htree::ValidateRootDriverSizing(result, selected_root_driver_cell_master)) {
    result.failure_reason = "root_driver_sizing_precheck_failed";
    build_stage.failed({{"reason", result.failure_reason}});
    return result;
  }

  {
    auto embedding_stage = SCHEMA_WRITER_INST.beginStage("HTree", "Build selected embedding",
                                                         {
                                                             {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
                                                             {"selected_levels", std::to_string(result.levels.size())},
                                                         });
    htree::BuildEmbedding(result, segment_pattern_library);
    result.success = result.failure_reason.empty() && result.best_char.has_value() && result.best_pattern.has_value()
                     && result.root_output_pin != nullptr && result.root_net != nullptr;
    if (result.success && options.enable_root_driver_sizing) {
      LOG_FATAL_IF(!htree::ApplyRootDriverSizing(result, selected_root_driver_cell_master))
          << "HTree: prevalidated root-driver sizing failed during embedding construction.";
    } else if (result.success && result.root_inst != nullptr) {
      result.selected_root_driver_cell_master = result.root_inst->get_cell_master();
    }
    if (result.success) {
      embedding_stage.finished({
          {"inserted_insts", std::to_string(result.inserted_insts.size())},
          {"inserted_nets", std::to_string(result.inserted_nets.size())},
          {"pruned_leaf_single_load_buffers", std::to_string(result.pruned_leaf_single_load_buffers)},
      });
    } else {
      embedding_stage.failed({{"reason", result.failure_reason.empty() ? "incomplete_embedding_build" : result.failure_reason}});
    }
  }

  {
    auto summary_stage = SCHEMA_WRITER_INST.beginStage("HTree", "Emit synthesis summary");
    htree::LogSynthesisSummary(result, selected_evaluation, selected_summary);
    summary_stage.finished();
  }
  if (result.success) {
    build_stage.finished();
  } else {
    build_stage.failed({{"reason", result.failure_reason.empty() ? "incomplete_embedding_build" : result.failure_reason}});
  }
  return result;
}

}  // namespace icts
