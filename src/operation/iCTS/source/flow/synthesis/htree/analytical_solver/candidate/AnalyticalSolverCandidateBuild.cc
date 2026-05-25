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
 * @file AnalyticalSolverCandidateBuild.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree solver beam candidate construction.
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AnalyticalModel.hh"
#include "HTreeTopologyChar.hh"
#include "PatternId.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/HTreeContracts.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalCandidate.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalHTreeCandidateSearch.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::htree::analytical_solver {
namespace {

auto EvaluatePartialRootToLeaf(const AnalyticalHTreeSolveProblem& solve_problem, PartialAnalyticalCandidate& partial,
                               AnalyticalSolverBuild& result) -> bool
{
  if (partial.level_segment_pattern_ids.size() != solve_problem.levels->size()
      || partial.level_load_caps_pf.size() != solve_problem.levels->size()) {
    return false;
  }
  if (solve_problem.config.use_functional_unit_compose && partial.level_unit_pattern_ids.size() != solve_problem.levels->size()) {
    return false;
  }

  double current_slew_ns = ResolveAnalyticalRootProbeSlewNs(solve_problem);
  double accumulated_delay_ns = 0.0;
  double accumulated_power_w = 0.0;
  double conservative_delay_ns = 0.0;
  double conservative_power_w = 0.0;
  double root_source_cap_pf = 0.0;
  std::vector<AnalyticalSegmentChoice> trace;
  trace.reserve(partial.level_segment_pattern_ids.size());

  for (std::size_t level_index = 0U; level_index < partial.level_segment_pattern_ids.size(); ++level_index) {
    const auto pattern_id = partial.level_segment_pattern_ids.at(level_index);
    const auto length_idx = solve_problem.levels->at(level_index).aligned_length_idx;
    const double load_cap_pf = partial.level_load_caps_pf.at(level_index);
    std::optional<ScoredSegment> scored;
    if (solve_problem.config.use_functional_unit_compose) {
      scored = ScoreFunctionalUnitSequence(solve_problem, partial.level_unit_pattern_ids.at(level_index), pattern_id, length_idx,
                                           current_slew_ns, load_cap_pf, false, result);
    } else {
      const auto* model_set = solve_problem.model_catalog->find(AnalyticalModelKey{
          .pattern_id = pattern_id,
          .length_idx = length_idx,
      });
      if (model_set == nullptr || !model_set->isComplete()) {
        ++result.summary.missing_model_count;
        return false;
      }
      scored = ScoreModelSet(pattern_id, length_idx, *model_set, current_slew_ns, load_cap_pf, false);
    }
    if (!scored.has_value()) {
      if (!solve_problem.config.use_functional_unit_compose) {
        ++result.summary.metric_evaluation_rejected_count;
      }
      return false;
    }

    if (level_index == 0U) {
      root_source_cap_pf = scored->source_cap_pf;
    }
    trace.push_back(MakeSegmentChoice(level_index, *scored));
    accumulated_delay_ns += scored->delay_ns;
    accumulated_power_w = AccumulateHTreePower(accumulated_power_w, level_index, *scored);
    conservative_delay_ns += scored->delay_upper_ns;
    conservative_power_w = AccumulateHTreePower(conservative_power_w, level_index, *scored);
    current_slew_ns = scored->output_slew_ns;
  }

  partial.current_slew_ns = current_slew_ns;
  partial.root_source_cap_pf = root_source_cap_pf;
  partial.accumulated_delay_ns = accumulated_delay_ns;
  partial.accumulated_power_w = accumulated_power_w;
  partial.conservative_delay_ns = conservative_delay_ns;
  partial.conservative_power_w = conservative_power_w;
  partial.trace = std::move(trace);
  return true;
}

auto BuildCandidateFromPartial(const AnalyticalHTreeSolveProblem& solve_problem, PartialAnalyticalCandidate partial,
                               AnalyticalSolverBuild& result) -> std::optional<AnalyticalCandidate>
{
  ++result.summary.materialization_attempt_count;
  if (!EvaluatePartialRootToLeaf(solve_problem, partial, result)) {
    return std::nullopt;
  }

  AnalyticalCandidate candidate;
  candidate.depth = static_cast<unsigned>(solve_problem.levels->size());
  candidate.leaf_load_cap_pf = solve_problem.config.representative_leaf_load_cap_pf;
  candidate.root_input_slew_ns = solve_problem.config.root_input_slew_ns;
  candidate.leaf_count = candidate.depth >= sizeof(std::size_t) * 8U ? 0U : (std::size_t{1U} << candidate.depth);
  candidate.level_segment_pattern_ids = std::move(partial.level_segment_pattern_ids);
  candidate.trace = std::move(partial.trace);
  candidate.output_slew_ns = partial.current_slew_ns;
  candidate.root_source_cap_pf = partial.root_source_cap_pf;
  candidate.raw_delay_ns = partial.accumulated_delay_ns;
  candidate.raw_power_w = partial.accumulated_power_w;
  candidate.conservative_slew_ns = partial.current_slew_ns;
  candidate.conservative_delay_ns = partial.conservative_delay_ns;
  candidate.conservative_power_w = partial.conservative_power_w;
  candidate.branch_buffer_legal = true;
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(solve_problem);
  if (segment_pattern_library == nullptr) {
    candidate.rejection_reason = "missing_segment_pattern_library";
    return std::nullopt;
  }
  auto topology_pattern_library = BuildAnalyticalTopologyPattern(candidate.level_segment_pattern_ids, *segment_pattern_library,
                                                                 solve_problem.fanout_config.max_fanout);
  if (!topology_pattern_library.has_value()) {
    candidate.rejection_reason = "topology_pattern_composition_illegal";
    ++result.summary.root_fanout_rejected_count;
    return std::nullopt;
  }
  candidate.topology_pattern_library = std::move(*topology_pattern_library);
  const PatternId topology_pattern_id = PatternId::topology(
      candidate.topology_pattern_library.nodes.empty() ? 0U : static_cast<unsigned>(candidate.topology_pattern_library.nodes.size() - 1U));
  const auto composition_state = candidate.topology_pattern_library.getCompositionState(topology_pattern_id);
  candidate.fanout_legal = IsBinarySourceFanoutLegal(composition_state.source_exposed_load_count, solve_problem.fanout_config.max_fanout);
  if (!candidate.fanout_legal) {
    candidate.rejection_reason = "root_fanout_illegal";
    ++result.summary.root_fanout_rejected_count;
    return std::nullopt;
  }
  candidate.materialized_char = MaterializeAnalyticalTopologyChar(candidate, solve_problem.slew_lattice, solve_problem.cap_lattice);
  if (!candidate.materialized_char.has_value()) {
    candidate.rejection_reason = "materialized_char_out_of_lattice";
    ++result.summary.lattice_rejected_count;
    return std::nullopt;
  }
  return candidate;
}

auto BuildDiagnosticDirectCandidate(const AnalyticalHTreeSolveProblem& solve_problem, FunctionalComposeContext& functional_context,
                                    AnalyticalSolverBuild& result) -> std::optional<AnalyticalCandidate>
{
  if (!solve_problem.config.use_functional_unit_compose || solve_problem.config.diagnostic_segment_pattern_ids.empty()
      || solve_problem.levels == nullptr || solve_problem.config.diagnostic_segment_pattern_ids.size() != solve_problem.levels->size()) {
    return std::nullopt;
  }

  PartialAnalyticalCandidate partial;
  partial.current_slew_ns = ResolveAnalyticalRootProbeSlewNs(solve_problem);
  partial.upstream_load_cap_pf = solve_problem.config.representative_leaf_load_cap_pf;
  for (std::size_t reverse_level_index = solve_problem.levels->size(); reverse_level_index > 0U; --reverse_level_index) {
    const std::size_t level_index = reverse_level_index - 1U;
    const auto pattern_id = solve_problem.config.diagnostic_segment_pattern_ids.at(level_index);
    const auto unit_pattern_ids = DecomposePatternToUnitSequence(pattern_id, solve_problem, functional_context);
    if (unit_pattern_ids.empty()) {
      return std::nullopt;
    }
    auto composition_state = TryPrependCompositionState(solve_problem, partial, pattern_id);
    if (!composition_state.has_value()) {
      return std::nullopt;
    }
    partial.has_composition_state = true;
    partial.composition_state = *composition_state;
    partial.level_segment_pattern_ids.insert(partial.level_segment_pattern_ids.begin(), pattern_id);
    partial.level_unit_pattern_ids.insert(partial.level_unit_pattern_ids.begin(), unit_pattern_ids);
    partial.level_load_caps_pf.insert(partial.level_load_caps_pf.begin(), partial.upstream_load_cap_pf);
    auto scored
        = ScoreFunctionalUnitSequence(solve_problem, unit_pattern_ids, pattern_id, solve_problem.levels->at(level_index).aligned_length_idx,
                                      ResolveAnalyticalRootProbeSlewNs(solve_problem), partial.upstream_load_cap_pf,
                                      solve_problem.config.use_conservative_scoring, result);
    if (!scored.has_value()) {
      return std::nullopt;
    }
    partial.upstream_load_cap_pf = scored->source_cap_pf * 2.0;
  }

  auto candidate = BuildCandidateFromPartial(solve_problem, std::move(partial), result);
  if (!candidate.has_value() || !candidate->materialized_char.has_value()) {
    return std::nullopt;
  }
  ++result.summary.diagnostic_direct_candidate_count;
  result.summary.diagnostic_direct_delay_ns = candidate->materialized_char->get_delay();
  result.summary.diagnostic_direct_power_w = candidate->materialized_char->get_power();
  result.summary.diagnostic_direct_root_cap_pf = candidate->root_source_cap_pf;
  result.summary.diagnostic_direct_input_slew_idx = candidate->materialized_char->get_input_slew_idx();
  result.summary.diagnostic_direct_output_slew_idx = candidate->materialized_char->get_output_slew_idx();
  result.summary.diagnostic_direct_driven_cap_idx = candidate->materialized_char->get_driven_cap_idx();
  return candidate;
}

}  // namespace

auto BuildBeamCandidates(const AnalyticalHTreeSolveProblem& solve_problem, AnalyticalSolverBuild& result)
    -> std::vector<AnalyticalCandidate>
{
  const double root_probe_slew_ns = ResolveAnalyticalRootProbeSlewNs(solve_problem);
  FunctionalComposeContext functional_context;
  FunctionalComposeContext* functional_context_ptr = nullptr;
  if (solve_problem.config.use_functional_unit_compose) {
    functional_context.unit_models = CollectUnitModelRefs(solve_problem);
    if (functional_context.unit_models.empty()) {
      result.summary.first_empty_reason = "empty_unit_model_catalog";
      return {};
    }
    functional_context.unit_pattern_by_cell_master_and_terminal_semantic
        = BuildUnitPatternByCellMaster(solve_problem, functional_context.unit_models);
    const auto* segment_pattern_library = ResolveSegmentPatternLibrary(solve_problem);
    functional_context.next_segment_pattern_id
        = segment_pattern_library == nullptr ? 0U : ResolveNextSegmentPatternId(*segment_pattern_library);
    functional_context_ptr = &functional_context;
    (void) BuildDiagnosticDirectCandidate(solve_problem, functional_context, result);
  }
  PartialAnalyticalCandidate seed_candidate;
  seed_candidate.current_slew_ns = root_probe_slew_ns;
  seed_candidate.upstream_load_cap_pf = solve_problem.config.representative_leaf_load_cap_pf;
  std::vector<PartialAnalyticalCandidate> beam = {std::move(seed_candidate)};

  const std::size_t beam_width = std::max<std::size_t>(1U, solve_problem.config.top_k_per_depth);
  for (std::size_t reverse_level_index = solve_problem.levels->size(); reverse_level_index > 0U; --reverse_level_index) {
    const std::size_t level_index = reverse_level_index - 1U;
    const auto& level = solve_problem.levels->at(level_index);
    std::vector<PartialAnalyticalCandidate> next_beam;
    for (const auto& partial : beam) {
      const double level_load_cap_pf = partial.upstream_load_cap_pf;
      auto shortlist = ShortlistSegmentsForLevel(solve_problem, level, root_probe_slew_ns, level_index, level_load_cap_pf,
                                                 functional_context_ptr, result);
      if (shortlist.empty()) {
        ++result.summary.empty_shortlist_count;
        if (result.summary.first_empty_reason.empty()) {
          result.summary.first_empty_level_index = static_cast<unsigned>(level_index);
          result.summary.first_empty_length_idx = level.aligned_length_idx;
          result.summary.first_empty_reason = "empty_level_shortlist";
        }
      }
      for (const auto& selected : shortlist) {
        auto composition_state = TryPrependCompositionState(solve_problem, partial, selected.pattern_id);
        if (!composition_state.has_value()) {
          ++result.summary.root_fanout_rejected_count;
          continue;
        }
        auto expanded = partial;
        expanded.has_composition_state = true;
        expanded.composition_state = *composition_state;
        expanded.level_segment_pattern_ids.insert(expanded.level_segment_pattern_ids.begin(), selected.pattern_id);
        if (solve_problem.config.use_functional_unit_compose) {
          expanded.level_unit_pattern_ids.insert(expanded.level_unit_pattern_ids.begin(), selected.unit_pattern_ids);
        }
        expanded.level_load_caps_pf.insert(expanded.level_load_caps_pf.begin(), level_load_cap_pf);
        expanded.accumulated_delay_ns += selected.delay_ns;
        expanded.accumulated_power_w += selected.power_w;
        expanded.conservative_delay_ns += selected.delay_upper_ns;
        expanded.conservative_power_w += selected.power_upper_w;
        expanded.current_slew_ns = selected.output_slew_ns;
        expanded.upstream_load_cap_pf = selected.source_cap_pf * 2.0;
        next_beam.push_back(std::move(expanded));
      }
    }
    beam = TrimPartialCandidates(std::move(next_beam), beam_width);
    if (beam.empty()) {
      return {};
    }
  }

  std::vector<AnalyticalCandidate> candidates;
  candidates.reserve(beam.size());
  for (auto& partial : beam) {
    auto candidate = BuildCandidateFromPartial(solve_problem, std::move(partial), result);
    if (candidate.has_value()) {
      if (candidate->level_segment_pattern_ids == solve_problem.config.diagnostic_segment_pattern_ids) {
        ++result.summary.diagnostic_generated_candidate_count;
      }
      candidates.push_back(std::move(*candidate));
      ++result.summary.generated_candidate_count;
    }
  }
  std::ranges::sort(candidates, PreferAnalyticalCandidate);
  if (solve_problem.config.top_k_per_depth > 0U && candidates.size() > solve_problem.config.top_k_per_depth) {
    candidates.resize(solve_problem.config.top_k_per_depth);
  }
  return candidates;
}

}  // namespace icts::htree::analytical_solver
