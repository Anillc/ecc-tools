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
 * @file AnalyticalSolverModel.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree solver model scoring and functional unit composition.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AnalyticalModel.hh"
#include "BufferingPattern.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalHTreeCandidateSearch.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"

namespace icts::htree::analytical_solver {

auto ResolveAnalyticalRootProbeSlewNs(const AnalyticalHTreeSolveProblem& solve_problem) -> double
{
  return std::max(solve_problem.config.root_input_slew_ns, solve_problem.slew_lattice.stepValue());
}

namespace {

auto EvaluateMetric(const AnalyticalModelSet& model_set, AnalyticalMetric metric, double input_slew_ns, double load_cap_pf,
                    bool conservative) -> std::optional<double>
{
  const auto* model = model_set.findMetric(metric);
  if (model == nullptr) {
    return std::nullopt;
  }
  return conservative ? model->evaluateConservativeUpper(input_slew_ns, load_cap_pf) : model->evaluate(input_slew_ns, load_cap_pf);
}

auto ResolveAnalyticalModelProbe(const AnalyticalModelSet& model_set, double input_slew_ns, double load_cap_pf) -> AnalyticalModelProbe
{
  AnalyticalModelProbe probe{
      .input_slew_ns = input_slew_ns,
      .load_cap_pf = load_cap_pf,
  };
  double slew_floor_ns = 0.0;
  double cap_floor_pf = 0.0;
  for (const auto metric : {AnalyticalMetric::kOutputSlew, AnalyticalMetric::kDelay, AnalyticalMetric::kPower,
                            AnalyticalMetric::kSourceBoundaryNetSwitchPower}) {
    const auto* model = model_set.findMetric(metric);
    if (model == nullptr || !model->domain.isValid()) {
      continue;
    }
    slew_floor_ns = std::max(slew_floor_ns, model->domain.slew_min_ns);
    cap_floor_pf = std::max(cap_floor_pf, model->domain.cap_min_pf);
  }
  if (probe.input_slew_ns > 0.0 && slew_floor_ns > 0.0 && probe.input_slew_ns < slew_floor_ns) {
    probe.input_slew_ns = slew_floor_ns;
    probe.slew_floored = true;
  }
  if (probe.load_cap_pf > 0.0 && cap_floor_pf > 0.0 && probe.load_cap_pf < cap_floor_pf) {
    probe.load_cap_pf = cap_floor_pf;
    probe.cap_floored = true;
  }
  return probe;
}

}  // namespace

auto ScoreModelSet(PatternId pattern_id, unsigned length_idx, const AnalyticalModelSet& model_set, double input_slew_ns,
                   double downstream_cap_pf, bool conservative) -> std::optional<ScoredSegment>
{
  if (!model_set.source_cap_operator.has_value()) {
    return std::nullopt;
  }

  const auto model_probe = ResolveAnalyticalModelProbe(model_set, input_slew_ns, downstream_cap_pf);
  const auto output_slew
      = EvaluateMetric(model_set, AnalyticalMetric::kOutputSlew, model_probe.input_slew_ns, model_probe.load_cap_pf, conservative);
  const auto delay = EvaluateMetric(model_set, AnalyticalMetric::kDelay, model_probe.input_slew_ns, model_probe.load_cap_pf, conservative);
  const auto power = EvaluateMetric(model_set, AnalyticalMetric::kPower, model_probe.input_slew_ns, model_probe.load_cap_pf, conservative);
  const auto source_boundary_power = EvaluateMetric(model_set, AnalyticalMetric::kSourceBoundaryNetSwitchPower, model_probe.input_slew_ns,
                                                    model_probe.load_cap_pf, conservative);
  if (!output_slew.has_value() || !delay.has_value() || !power.has_value() || !source_boundary_power.has_value()) {
    return std::nullopt;
  }

  ScoredSegment scored;
  scored.pattern_id = pattern_id;
  scored.length_idx = length_idx;
  scored.input_slew_ns = input_slew_ns;
  scored.downstream_load_cap_pf = downstream_cap_pf;
  scored.output_slew_ns = *output_slew;
  scored.source_cap_pf = model_set.source_cap_operator->apply(downstream_cap_pf);
  scored.delay_ns = *delay;
  scored.power_w = *power;
  scored.source_boundary_power_w = *source_boundary_power;
  scored.slew_upper_ns = *output_slew;
  scored.delay_upper_ns = *delay;
  scored.power_upper_w = *power;
  scored.score = scored.delay_upper_ns + scored.power_upper_w;
  return scored;
}

auto ScoreSegment(const SegmentChar& segment_char, const AnalyticalModelSet& model_set, double input_slew_ns, double downstream_cap_pf,
                  bool conservative) -> std::optional<ScoredSegment>
{
  return ScoreModelSet(segment_char.get_pattern_id(), segment_char.get_length_idx(), model_set, input_slew_ns, downstream_cap_pf,
                       conservative);
}

auto CollectUnitModelRefs(const AnalyticalHTreeSolveProblem& solve_problem) -> std::vector<UnitModelRef>
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(solve_problem);
  if (segment_pattern_library == nullptr || solve_problem.model_catalog == nullptr) {
    return {};
  }

  std::vector<UnitModelRef> unit_models;
  unit_models.reserve(solve_problem.model_catalog->size());
  for (const auto& [key, model_set] : solve_problem.model_catalog->get_model_sets()) {
    if (key.length_idx != solve_problem.config.unit_length_idx || !model_set.isComplete() || !model_set.source_cap_operator.has_value()) {
      continue;
    }
    const auto* pattern = segment_pattern_library->find(key.pattern_id);
    if (pattern == nullptr || pattern->get_length_idx() != solve_problem.config.unit_length_idx) {
      continue;
    }
    unit_models.push_back(UnitModelRef{
        .pattern_id = key.pattern_id,
        .model_set = &model_set,
        .composition_state = segment_pattern_library->getCompositionState(key.pattern_id),
    });
  }
  std::ranges::sort(unit_models,
                    [](const UnitModelRef& lhs, const UnitModelRef& rhs) -> bool { return lhs.pattern_id.pack() < rhs.pattern_id.pack(); });
  return unit_models;
}

auto BuildUnitPatternByCellMaster(const AnalyticalHTreeSolveProblem& solve_problem, const std::vector<UnitModelRef>& unit_models)
    -> std::unordered_map<std::string, PatternId>
{
  std::unordered_map<std::string, PatternId> unit_pattern_by_cell_master;
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(solve_problem);
  if (segment_pattern_library == nullptr) {
    return unit_pattern_by_cell_master;
  }
  for (const auto& unit_model : unit_models) {
    const auto* pattern = segment_pattern_library->find(unit_model.pattern_id);
    if (pattern == nullptr) {
      continue;
    }
    const std::string cell_master
        = pattern->isBufferPattern() && !pattern->get_cell_masters().empty() ? pattern->get_cell_masters().front() : std::string{};
    const std::string lookup_key = (pattern->hasTerminalBranchBuffer() ? "branch:" : "leaf:") + cell_master;
    auto [it, inserted] = unit_pattern_by_cell_master.emplace(lookup_key, unit_model.pattern_id);
    if (!inserted && unit_model.pattern_id.pack() < it->second.pack()) {
      it->second = unit_model.pattern_id;
    }
  }
  return unit_pattern_by_cell_master;
}

namespace {

auto MakeUnitPatternLookupKey(const std::string& cell_master, bool terminal_branch_buffer) -> std::string
{
  return (terminal_branch_buffer ? "branch:" : "leaf:") + cell_master;
}

auto FindUnitPatternByCellMasterAndTerminalSemantic(const FunctionalComposeContext& context, const std::string& cell_master,
                                                    bool terminal_branch_buffer) -> std::optional<PatternId>
{
  const auto exact_key = MakeUnitPatternLookupKey(cell_master, terminal_branch_buffer);
  if (const auto exact_it = context.unit_pattern_by_cell_master_and_terminal_semantic.find(exact_key);
      exact_it != context.unit_pattern_by_cell_master_and_terminal_semantic.end()) {
    return exact_it->second;
  }

  // Unit timing/cap models describe the same physical one-slot segment even when the
  // terminal branch semantic is only meaningful after composing the full segment.
  const auto compatible_key = MakeUnitPatternLookupKey(cell_master, !terminal_branch_buffer);
  if (const auto compatible_it = context.unit_pattern_by_cell_master_and_terminal_semantic.find(compatible_key);
      compatible_it != context.unit_pattern_by_cell_master_and_terminal_semantic.end()) {
    return compatible_it->second;
  }
  return std::nullopt;
}

}  // namespace

auto DecomposePatternToUnitSequence(PatternId pattern_id, const AnalyticalHTreeSolveProblem& solve_problem,
                                    FunctionalComposeContext& context) -> std::vector<PatternId>
{
  if (const auto it = context.decomposed_patterns.find(pattern_id); it != context.decomposed_patterns.end()) {
    return it->second;
  }

  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(solve_problem);
  if (segment_pattern_library == nullptr || solve_problem.config.unit_length_idx == 0U) {
    context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
    return {};
  }
  const auto* pattern = segment_pattern_library->find(pattern_id);
  if (pattern == nullptr || pattern->get_length_idx() == 0U || pattern->get_length_idx() % solve_problem.config.unit_length_idx != 0U) {
    context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
    return {};
  }

  const unsigned unit_count = pattern->get_length_idx() / solve_problem.config.unit_length_idx;
  std::vector<PatternId> unit_pattern_ids;
  unit_pattern_ids.reserve(unit_count);
  const auto& buffer_positions = pattern->get_buffer_positions();
  const auto& cell_masters = pattern->get_cell_masters();
  if (buffer_positions.size() != cell_masters.size()) {
    context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
    return {};
  }

  std::vector<std::string> unit_cell_masters(unit_count);
  std::vector<unsigned> unit_buffer_counts(unit_count, 0U);
  const auto unit_count_as_double = static_cast<double>(unit_count);

  for (std::size_t buffer_index = 0U; buffer_index < buffer_positions.size(); ++buffer_index) {
    const double normalized_position = buffer_positions.at(buffer_index);
    if (normalized_position <= 0.0 || normalized_position > 1.0 + 1e-9) {
      context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
      return {};
    }

    const double scaled_position = normalized_position * unit_count_as_double;
    const auto slot_boundary = static_cast<unsigned>(std::llround(scaled_position));
    if (slot_boundary == 0U || slot_boundary > unit_count) {
      context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
      return {};
    }
    const double expected_position = static_cast<double>(slot_boundary) / unit_count_as_double;
    const double lattice_tolerance = std::max(1e-6, std::numeric_limits<double>::epsilon() * unit_count_as_double * 16.0);
    if (std::abs(normalized_position - expected_position) > lattice_tolerance) {
      context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
      return {};
    }

    const unsigned unit_index = slot_boundary - 1U;
    ++unit_buffer_counts.at(unit_index);
    if (unit_buffer_counts.at(unit_index) > 1U) {
      context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
      return {};
    }
    unit_cell_masters.at(unit_index) = cell_masters.at(buffer_index);
  }

  for (unsigned unit_index = 0U; unit_index < unit_count; ++unit_index) {
    const bool terminal_branch_buffer
        = pattern->hasTerminalBranchBuffer() && unit_index + 1U == unit_count && unit_buffer_counts.at(unit_index) > 0U;
    const auto unit_pattern_id
        = FindUnitPatternByCellMasterAndTerminalSemantic(context, unit_cell_masters.at(unit_index), terminal_branch_buffer);
    if (!unit_pattern_id.has_value()) {
      context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
      return {};
    }
    unit_pattern_ids.push_back(*unit_pattern_id);
  }

  context.decomposed_patterns.emplace(pattern_id, unit_pattern_ids);
  return unit_pattern_ids;
}

namespace {

auto FindUnitModel(const AnalyticalHTreeSolveProblem& solve_problem, PatternId pattern_id) -> const AnalyticalModelSet*
{
  if (solve_problem.model_catalog == nullptr) {
    return nullptr;
  }
  return solve_problem.model_catalog->find(AnalyticalModelKey{
      .pattern_id = pattern_id,
      .length_idx = solve_problem.config.unit_length_idx,
  });
}

auto RecordMetricEvaluationRejection(const AnalyticalModelSet& model_set, double input_slew_ns, double load_cap_pf,
                                     AnalyticalSolverBuild& result) -> void
{
  ++result.summary.metric_evaluation_rejected_count;
  bool slew_rejected = false;
  bool cap_rejected = false;
  for (const auto metric : {AnalyticalMetric::kOutputSlew, AnalyticalMetric::kDelay, AnalyticalMetric::kPower,
                            AnalyticalMetric::kSourceBoundaryNetSwitchPower}) {
    const auto* model = model_set.findMetric(metric);
    if (model == nullptr || !model->domain.isValid()) {
      continue;
    }
    if (input_slew_ns + 1e-12 < model->domain.slew_min_ns || input_slew_ns > model->domain.slew_max_ns + 1e-12) {
      slew_rejected = true;
    }
    if (load_cap_pf + 1e-12 < model->domain.cap_min_pf || load_cap_pf > model->domain.cap_max_pf + 1e-12) {
      cap_rejected = true;
    }
  }
  if (slew_rejected) {
    ++result.summary.domain_slew_rejected_count;
  }
  if (cap_rejected) {
    ++result.summary.domain_cap_rejected_count;
    result.summary.max_domain_rejected_cap_pf = std::max(result.summary.max_domain_rejected_cap_pf, load_cap_pf);
  }
}

}  // namespace

auto ScoreFunctionalUnitSequence(const AnalyticalHTreeSolveProblem& solve_problem, const std::vector<PatternId>& unit_pattern_ids,
                                 PatternId materialized_pattern_id, unsigned length_idx, double input_slew_ns, double downstream_cap_pf,
                                 bool conservative, AnalyticalSolverBuild& result) -> std::optional<ScoredSegment>
{
  if (unit_pattern_ids.empty()) {
    return std::nullopt;
  }

  std::vector<const AnalyticalModelSet*> model_sets;
  model_sets.reserve(unit_pattern_ids.size());
  std::vector<double> unit_downstream_caps(unit_pattern_ids.size(), downstream_cap_pf);
  double source_cap_pf = downstream_cap_pf;
  for (std::size_t reverse_index = unit_pattern_ids.size(); reverse_index > 0U; --reverse_index) {
    const std::size_t index = reverse_index - 1U;
    const auto* model_set = FindUnitModel(solve_problem, unit_pattern_ids.at(index));
    if (model_set == nullptr || !model_set->isComplete() || !model_set->source_cap_operator.has_value()) {
      ++result.summary.missing_model_count;
      return std::nullopt;
    }
    model_sets.push_back(model_set);
    unit_downstream_caps.at(index) = source_cap_pf;
    source_cap_pf = model_set->source_cap_operator->apply(source_cap_pf);
  }
  std::ranges::reverse(model_sets);

  double current_slew_ns = input_slew_ns;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_power_w = 0.0;
  for (std::size_t index = 0U; index < unit_pattern_ids.size(); ++index) {
    const auto& model_set = *model_sets.at(index);
    const double unit_downstream_cap_pf = unit_downstream_caps.at(index);
    const auto model_probe = ResolveAnalyticalModelProbe(model_set, current_slew_ns, unit_downstream_cap_pf);
    if (model_probe.slew_floored) {
      ++result.summary.domain_slew_floor_count;
    }
    if (model_probe.cap_floored) {
      ++result.summary.domain_cap_floor_count;
    }
    const auto output_slew
        = EvaluateMetric(model_set, AnalyticalMetric::kOutputSlew, model_probe.input_slew_ns, model_probe.load_cap_pf, conservative);
    const auto delay
        = EvaluateMetric(model_set, AnalyticalMetric::kDelay, model_probe.input_slew_ns, model_probe.load_cap_pf, conservative);
    const auto power
        = EvaluateMetric(model_set, AnalyticalMetric::kPower, model_probe.input_slew_ns, model_probe.load_cap_pf, conservative);
    const auto source_boundary_power = EvaluateMetric(model_set, AnalyticalMetric::kSourceBoundaryNetSwitchPower, model_probe.input_slew_ns,
                                                      model_probe.load_cap_pf, conservative);
    if (!output_slew.has_value() || !delay.has_value() || !power.has_value() || !source_boundary_power.has_value()) {
      RecordMetricEvaluationRejection(model_set, model_probe.input_slew_ns, model_probe.load_cap_pf, result);
      return std::nullopt;
    }
    if (index == 0U) {
      source_boundary_power_w = *source_boundary_power;
      power_w += *power;
    } else {
      power_w += *power - *source_boundary_power;
    }
    delay_ns += *delay;
    current_slew_ns = *output_slew;
  }

  ScoredSegment scored;
  scored.pattern_id = materialized_pattern_id;
  scored.length_idx = length_idx;
  scored.input_slew_ns = input_slew_ns;
  scored.downstream_load_cap_pf = downstream_cap_pf;
  scored.output_slew_ns = current_slew_ns;
  scored.source_cap_pf = source_cap_pf;
  scored.delay_ns = delay_ns;
  scored.power_w = power_w;
  scored.source_boundary_power_w = source_boundary_power_w;
  scored.slew_upper_ns = current_slew_ns;
  scored.delay_upper_ns = delay_ns;
  scored.power_upper_w = power_w;
  scored.score = scored.delay_upper_ns + scored.power_upper_w;
  scored.unit_pattern_ids = unit_pattern_ids;
  return scored;
}

}  // namespace icts::htree::analytical_solver
