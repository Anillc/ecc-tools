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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file AnalyticalSolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Analytical H-tree shortlist solver implementation.
 */

#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "Frontier.hh"
#include "HTreeTopologyChar.hh"
#include "SegmentChar.hh"
#include "analytical_characterization/AnalyticalModel.hh"
#include "synthesis/htree/segment_pruning/SegmentLibrary.hh"

namespace icts::htree::analytical_solver {
namespace {

using icts::analytical::AnalyticalMetric;
using icts::analytical::AnalyticalModelKey;
using icts::analytical::AnalyticalModelSet;

struct ScoredSegment
{
  PatternId pattern_id = PatternId::segment(0U);
  unsigned length_idx = 0U;
  double score = 0.0;
  double input_slew_ns = 0.0;
  double downstream_load_cap_pf = 0.0;
  double output_slew_ns = 0.0;
  double source_cap_pf = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_power_w = 0.0;
  double slew_upper_ns = 0.0;
  double delay_upper_ns = 0.0;
  double power_upper_w = 0.0;
  std::vector<PatternId> unit_pattern_ids;
};

struct ScoredSegmentCacheKey
{
  unsigned length_idx = 0U;
  bool leaf_level = false;
  bool force_branch_buffer = false;
  double input_slew_ns = 0.0;
  double downstream_cap_pf = 0.0;

  auto operator==(const ScoredSegmentCacheKey& rhs) const -> bool = default;
};

struct ScoredSegmentCacheKeyHash
{
  auto operator()(const ScoredSegmentCacheKey& key) const noexcept -> std::size_t
  {
    std::size_t hash_value = std::hash<unsigned>{}(key.length_idx);
    hash_value ^= std::hash<bool>{}(key.leaf_level) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    hash_value ^= std::hash<bool>{}(key.force_branch_buffer) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    hash_value ^= std::hash<double>{}(key.input_slew_ns) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    hash_value ^= std::hash<double>{}(key.downstream_cap_pf) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    return hash_value;
  }
};

struct PartialAnalyticalCandidate
{
  double current_slew_ns = 0.0;
  double upstream_load_cap_pf = 0.0;
  double root_source_cap_pf = 0.0;
  double accumulated_delay_ns = 0.0;
  double accumulated_power_w = 0.0;
  double conservative_delay_ns = 0.0;
  double conservative_power_w = 0.0;
  bool has_composition_state = false;
  PatternCompositionState composition_state;
  std::vector<PatternId> level_segment_pattern_ids;
  std::vector<std::vector<PatternId>> level_unit_pattern_ids;
  std::vector<double> level_load_caps_pf;
  std::vector<AnalyticalSegmentChoice> trace;
};

struct UnitModelRef
{
  PatternId pattern_id = PatternId::segment(0U);
  const AnalyticalModelSet* model_set = nullptr;
  PatternCompositionState composition_state;
};

struct PatternSequenceKey
{
  std::vector<unsigned> pattern_ids;

  auto operator==(const PatternSequenceKey& rhs) const -> bool = default;
};

struct PatternSequenceKeyHash
{
  auto operator()(const PatternSequenceKey& key) const noexcept -> std::size_t
  {
    std::size_t hash_value = 0U;
    for (const unsigned pattern_id : key.pattern_ids) {
      hash_value ^= std::hash<unsigned>{}(pattern_id) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    }
    return hash_value;
  }
};

struct FunctionalComposeContext
{
  std::vector<UnitModelRef> unit_models;
  std::unordered_map<std::string, PatternId> unit_pattern_by_cell_master_and_terminal_semantic;
  std::unordered_map<PatternId, std::vector<PatternId>> decomposed_patterns;
  std::unordered_map<ScoredSegmentCacheKey, std::vector<ScoredSegment>, ScoredSegmentCacheKeyHash> scored_segments_by_key;
  std::unordered_map<PatternSequenceKey, PatternId, PatternSequenceKeyHash> materialized_patterns;
  unsigned next_segment_pattern_id = 0U;
};

struct ResolvedModelInputs
{
  double input_slew_ns = 0.0;
  double load_cap_pf = 0.0;
  bool slew_floored = false;
  bool cap_floored = false;
};

enum class DiagnosticPatternStage
{
  kFrontier,
  kDecomposed,
  kScored,
  kShortlisted,
};

auto MakeFailure(std::string reason) -> AnalyticalSolverResult
{
  AnalyticalSolverResult result;
  result.success = false;
  result.failure_reason = std::move(reason);
  return result;
}

auto ValidateRequest(const AnalyticalSolverRequest& request) -> std::string
{
  if (request.levels == nullptr || request.levels->empty()) {
    return "missing_levels";
  }
  if (!request.options.use_functional_unit_compose && request.segment_frontier_catalog == nullptr) {
    return "missing_segment_frontier_catalog";
  }
  if (request.segment_pattern_library == nullptr) {
    return "missing_segment_pattern_library";
  }
  if (request.options.use_functional_unit_compose && request.mutable_segment_pattern_library == nullptr) {
    return "missing_mutable_segment_pattern_library";
  }
  if (request.model_catalog == nullptr || request.model_catalog->empty()) {
    return "missing_analytical_model_catalog";
  }
  if (!request.slew_lattice.isValid() || !request.cap_lattice.isValid()) {
    return "invalid_lattice";
  }
  if (request.options.root_input_slew_ns < 0.0) {
    return "invalid_root_input_slew";
  }
  if (request.options.representative_leaf_load_cap_pf <= 0.0) {
    return "invalid_representative_leaf_load_cap";
  }
  return {};
}

auto ResolveNextSegmentPatternId(const BufferPatternLibrary& segment_pattern_library) -> unsigned
{
  unsigned next_id = 0U;
  for (const auto& [pattern_id, pattern] : segment_pattern_library.patterns) {
    (void) pattern;
    if (pattern_id.domain == PatternDomain::kSegmentPattern) {
      next_id = std::max(next_id, pattern_id.local_id + 1U);
    }
  }
  return next_id;
}

auto MakePatternSequenceKey(const std::vector<PatternId>& pattern_ids) -> PatternSequenceKey
{
  PatternSequenceKey key;
  key.pattern_ids.reserve(pattern_ids.size());
  for (const auto pattern_id : pattern_ids) {
    key.pattern_ids.push_back(pattern_id.pack());
  }
  return key;
}

auto ResolveSegmentPatternLibrary(const AnalyticalSolverRequest& request) -> const BufferPatternLibrary*
{
  return request.segment_pattern_library != nullptr ? request.segment_pattern_library : request.mutable_segment_pattern_library;
}

auto DiagnosticPatternIds(const AnalyticalSolverRequest& request, std::size_t level_index) -> std::span<const PatternId>
{
  if (request.options.diagnostic_segment_pattern_ids.empty() || request.levels == nullptr || request.levels->empty()) {
    return {};
  }
  if (request.options.diagnostic_segment_pattern_ids.size() != request.levels->size() || level_index >= request.levels->size()) {
    return {};
  }
  return std::span<const PatternId>(&request.options.diagnostic_segment_pattern_ids.at(level_index), 1U);
}

auto ContainsDiagnosticPattern(std::span<const PatternId> diagnostic_pattern_ids, PatternId pattern_id) -> bool
{
  return std::ranges::find(diagnostic_pattern_ids, pattern_id) != diagnostic_pattern_ids.end();
}

auto RecordDiagnosticPatternStage(std::span<const PatternId> diagnostic_pattern_ids, PatternId pattern_id, DiagnosticPatternStage stage,
                                  AnalyticalSolverResult& result) -> void
{
  if (!ContainsDiagnosticPattern(diagnostic_pattern_ids, pattern_id)) {
    return;
  }
  switch (stage) {
    case DiagnosticPatternStage::kFrontier:
      ++result.diagnostic_frontier_hit_count;
      break;
    case DiagnosticPatternStage::kDecomposed:
      ++result.diagnostic_decomposed_count;
      break;
    case DiagnosticPatternStage::kScored:
      ++result.diagnostic_scored_count;
      break;
    case DiagnosticPatternStage::kShortlisted:
      ++result.diagnostic_shortlisted_count;
      break;
  }
}

auto RecordDiagnosticLibraryHits(const AnalyticalSolverRequest& request, AnalyticalSolverResult& result) -> void
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
  if (segment_pattern_library == nullptr) {
    return;
  }
  for (const auto pattern_id : request.options.diagnostic_segment_pattern_ids) {
    if (segment_pattern_library->find(pattern_id) != nullptr) {
      ++result.diagnostic_library_hit_count;
    }
  }
}

auto MaterializeFunctionalSegmentPattern(const std::vector<PatternId>& unit_pattern_ids, FunctionalComposeContext& context,
                                         BufferPatternLibrary& segment_pattern_library) -> std::optional<PatternId>
{
  if (unit_pattern_ids.empty()) {
    return std::nullopt;
  }
  if (unit_pattern_ids.size() == 1U) {
    return unit_pattern_ids.front();
  }

  const auto key = MakePatternSequenceKey(unit_pattern_ids);
  if (const auto it = context.materialized_patterns.find(key); it != context.materialized_patterns.end()) {
    return it->second;
  }

  if (segment_pattern_library.find(unit_pattern_ids.front()) == nullptr) {
    return std::nullopt;
  }
  SegmentPatternLibraryCombiner combiner(segment_pattern_library, context.next_segment_pattern_id);
  PatternId merged_pattern_id = unit_pattern_ids.front();
  for (std::size_t index = 1U; index < unit_pattern_ids.size(); ++index) {
    const PatternId next_pattern_id = unit_pattern_ids.at(index);
    if (segment_pattern_library.find(next_pattern_id) == nullptr || !combiner.canCompose(merged_pattern_id, next_pattern_id)) {
      return std::nullopt;
    }
    merged_pattern_id = combiner.combine(merged_pattern_id, next_pattern_id);
  }

  context.next_segment_pattern_id = combiner.get_next_id();
  context.materialized_patterns.emplace(key, merged_pattern_id);
  return merged_pattern_id;
}

auto ResolveModelInputRootSlewNs(const AnalyticalSolverRequest& request) -> double
{
  return std::max(request.options.root_input_slew_ns, request.slew_lattice.stepValue());
}

auto EvaluateMetric(const AnalyticalModelSet& model_set, AnalyticalMetric metric, double input_slew_ns, double load_cap_pf,
                    bool conservative) -> std::optional<double>
{
  const auto* model = model_set.findMetric(metric);
  if (model == nullptr) {
    return std::nullopt;
  }
  return conservative ? model->evaluateConservativeUpper(input_slew_ns, load_cap_pf) : model->evaluate(input_slew_ns, load_cap_pf);
}

auto ResolveModelInputs(const AnalyticalModelSet& model_set, double input_slew_ns, double load_cap_pf) -> ResolvedModelInputs
{
  ResolvedModelInputs inputs{
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
  if (inputs.input_slew_ns > 0.0 && slew_floor_ns > 0.0 && inputs.input_slew_ns < slew_floor_ns) {
    inputs.input_slew_ns = slew_floor_ns;
    inputs.slew_floored = true;
  }
  if (inputs.load_cap_pf > 0.0 && cap_floor_pf > 0.0 && inputs.load_cap_pf < cap_floor_pf) {
    inputs.load_cap_pf = cap_floor_pf;
    inputs.cap_floored = true;
  }
  return inputs;
}

auto ScoreModelSet(PatternId pattern_id, unsigned length_idx, const AnalyticalModelSet& model_set, double input_slew_ns,
                   double downstream_cap_pf, bool conservative) -> std::optional<ScoredSegment>
{
  if (!model_set.source_cap_operator.has_value()) {
    return std::nullopt;
  }

  const auto model_inputs = ResolveModelInputs(model_set, input_slew_ns, downstream_cap_pf);
  const auto output_slew
      = EvaluateMetric(model_set, AnalyticalMetric::kOutputSlew, model_inputs.input_slew_ns, model_inputs.load_cap_pf, conservative);
  const auto delay
      = EvaluateMetric(model_set, AnalyticalMetric::kDelay, model_inputs.input_slew_ns, model_inputs.load_cap_pf, conservative);
  const auto power
      = EvaluateMetric(model_set, AnalyticalMetric::kPower, model_inputs.input_slew_ns, model_inputs.load_cap_pf, conservative);
  const auto source_boundary_power = EvaluateMetric(model_set, AnalyticalMetric::kSourceBoundaryNetSwitchPower, model_inputs.input_slew_ns,
                                                    model_inputs.load_cap_pf, conservative);
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

auto CollectUnitModelRefs(const AnalyticalSolverRequest& request) -> std::vector<UnitModelRef>
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
  if (segment_pattern_library == nullptr || request.model_catalog == nullptr) {
    return {};
  }

  std::vector<UnitModelRef> unit_models;
  unit_models.reserve(request.model_catalog->size());
  for (const auto& [key, model_set] : request.model_catalog->get_model_sets()) {
    if (key.length_idx != request.options.unit_length_idx || !model_set.isComplete() || !model_set.source_cap_operator.has_value()) {
      continue;
    }
    const auto* pattern = segment_pattern_library->find(key.pattern_id);
    if (pattern == nullptr || pattern->get_length_idx() != request.options.unit_length_idx) {
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

auto BuildUnitPatternByCellMaster(const AnalyticalSolverRequest& request, const std::vector<UnitModelRef>& unit_models)
    -> std::unordered_map<std::string, PatternId>
{
  std::unordered_map<std::string, PatternId> unit_pattern_by_cell_master;
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
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

auto DecomposePatternToUnitSequence(PatternId pattern_id, const AnalyticalSolverRequest& request, FunctionalComposeContext& context)
    -> std::vector<PatternId>
{
  if (const auto it = context.decomposed_patterns.find(pattern_id); it != context.decomposed_patterns.end()) {
    return it->second;
  }

  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
  if (segment_pattern_library == nullptr || request.options.unit_length_idx == 0U) {
    context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
    return {};
  }
  const auto* pattern = segment_pattern_library->find(pattern_id);
  if (pattern == nullptr || pattern->get_length_idx() == 0U || pattern->get_length_idx() % request.options.unit_length_idx != 0U) {
    context.decomposed_patterns.emplace(pattern_id, std::vector<PatternId>{});
    return {};
  }

  const unsigned unit_count = pattern->get_length_idx() / request.options.unit_length_idx;
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

auto FindUnitModel(const AnalyticalSolverRequest& request, PatternId pattern_id) -> const AnalyticalModelSet*
{
  if (request.model_catalog == nullptr) {
    return nullptr;
  }
  return request.model_catalog->find(AnalyticalModelKey{
      .pattern_id = pattern_id,
      .length_idx = request.options.unit_length_idx,
  });
}

auto RecordMetricEvaluationRejection(const AnalyticalModelSet& model_set, double input_slew_ns, double load_cap_pf,
                                     AnalyticalSolverResult& result) -> void
{
  ++result.metric_evaluation_rejected_count;
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
    ++result.domain_slew_rejected_count;
  }
  if (cap_rejected) {
    ++result.domain_cap_rejected_count;
    result.max_domain_rejected_cap_pf = std::max(result.max_domain_rejected_cap_pf, load_cap_pf);
  }
}

auto ScoreFunctionalUnitSequence(const AnalyticalSolverRequest& request, const std::vector<PatternId>& unit_pattern_ids,
                                 PatternId materialized_pattern_id, unsigned length_idx, double input_slew_ns, double downstream_cap_pf,
                                 bool conservative, AnalyticalSolverResult& result) -> std::optional<ScoredSegment>
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
    const auto* model_set = FindUnitModel(request, unit_pattern_ids.at(index));
    if (model_set == nullptr || !model_set->isComplete() || !model_set->source_cap_operator.has_value()) {
      ++result.missing_model_count;
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
    const auto model_inputs = ResolveModelInputs(model_set, current_slew_ns, unit_downstream_cap_pf);
    if (model_inputs.slew_floored) {
      ++result.domain_slew_floor_count;
    }
    if (model_inputs.cap_floored) {
      ++result.domain_cap_floor_count;
    }
    const auto output_slew
        = EvaluateMetric(model_set, AnalyticalMetric::kOutputSlew, model_inputs.input_slew_ns, model_inputs.load_cap_pf, conservative);
    const auto delay
        = EvaluateMetric(model_set, AnalyticalMetric::kDelay, model_inputs.input_slew_ns, model_inputs.load_cap_pf, conservative);
    const auto power
        = EvaluateMetric(model_set, AnalyticalMetric::kPower, model_inputs.input_slew_ns, model_inputs.load_cap_pf, conservative);
    const auto source_boundary_power = EvaluateMetric(model_set, AnalyticalMetric::kSourceBoundaryNetSwitchPower,
                                                      model_inputs.input_slew_ns, model_inputs.load_cap_pf, conservative);
    if (!output_slew.has_value() || !delay.has_value() || !power.has_value() || !source_boundary_power.has_value()) {
      RecordMetricEvaluationRejection(model_set, model_inputs.input_slew_ns, model_inputs.load_cap_pf, result);
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

auto PreferScoredSegment(const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool
{
  if (lhs.score != rhs.score) {
    return lhs.score < rhs.score;
  }
  if (lhs.delay_upper_ns != rhs.delay_upper_ns) {
    return lhs.delay_upper_ns < rhs.delay_upper_ns;
  }
  if (lhs.power_upper_w != rhs.power_upper_w) {
    return lhs.power_upper_w < rhs.power_upper_w;
  }
  return lhs.pattern_id.pack() < rhs.pattern_id.pack();
}

auto PreferPartialCandidate(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  if (lhs.conservative_delay_ns != rhs.conservative_delay_ns) {
    return lhs.conservative_delay_ns < rhs.conservative_delay_ns;
  }
  if (lhs.conservative_power_w != rhs.conservative_power_w) {
    return lhs.conservative_power_w < rhs.conservative_power_w;
  }
  if (lhs.upstream_load_cap_pf != rhs.upstream_load_cap_pf) {
    return lhs.upstream_load_cap_pf < rhs.upstream_load_cap_pf;
  }
  return LexicographicalPatternIdLess(lhs.level_segment_pattern_ids, rhs.level_segment_pattern_ids);
}

template <typename T, typename EqualFn>
auto PushUnique(std::vector<T>& target, T value, EqualFn equal) -> void
{
  const auto it = std::ranges::find_if(target, [&](const T& existing) -> bool { return equal(existing, value); });
  if (it == target.end()) {
    target.push_back(std::move(value));
  }
}

auto SameScoredSegmentTrace(const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool
{
  return lhs.pattern_id == rhs.pattern_id && lhs.unit_pattern_ids == rhs.unit_pattern_ids;
}

auto TrimScoredSegments(std::vector<ScoredSegment> scored_segments, std::size_t max_size) -> std::vector<ScoredSegment>
{
  if (max_size == 0U || scored_segments.size() <= max_size) {
    std::ranges::sort(scored_segments, PreferScoredSegment);
    return scored_segments;
  }

  std::vector<ScoredSegment> trimmed;
  trimmed.reserve(max_size);
  const std::size_t delay_anchor_limit = std::max<std::size_t>(1U, max_size / 4U);
  const std::size_t slew_anchor_limit = std::max(delay_anchor_limit, max_size / 2U);
  const std::size_t low_cap_anchor_limit = std::max(slew_anchor_limit, (3U * max_size) / 4U);
  std::ranges::sort(scored_segments, PreferScoredSegment);
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= delay_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(scored_segments, [](const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool {
    if (lhs.output_slew_ns != rhs.output_slew_ns) {
      return lhs.output_slew_ns < rhs.output_slew_ns;
    }
    return PreferScoredSegment(lhs, rhs);
  });
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= slew_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(scored_segments, [](const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool {
    if (lhs.source_cap_pf != rhs.source_cap_pf) {
      return lhs.source_cap_pf < rhs.source_cap_pf;
    }
    return PreferScoredSegment(lhs, rhs);
  });
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= low_cap_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(scored_segments, [](const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool {
    if (lhs.source_cap_pf != rhs.source_cap_pf) {
      return lhs.source_cap_pf > rhs.source_cap_pf;
    }
    return PreferScoredSegment(lhs, rhs);
  });
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= max_size) {
      break;
    }
  }

  std::ranges::sort(trimmed, PreferScoredSegment);
  return trimmed;
}

auto SamePartialCandidateTrace(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  return lhs.level_segment_pattern_ids == rhs.level_segment_pattern_ids;
}

auto PartialDominates(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  if (lhs.has_composition_state != rhs.has_composition_state) {
    return false;
  }
  if (lhs.has_composition_state
      && (lhs.composition_state.terminal_semantic != rhs.composition_state.terminal_semantic
          || !(lhs.composition_state.monotonic_boundary_state == rhs.composition_state.monotonic_boundary_state)
          || lhs.composition_state.source_exposed_load_count != rhs.composition_state.source_exposed_load_count)) {
    return false;
  }

  constexpr double epsilon = 1e-12;
  const bool no_worse
      = lhs.conservative_delay_ns <= rhs.conservative_delay_ns + epsilon && lhs.conservative_power_w <= rhs.conservative_power_w + epsilon
        && lhs.upstream_load_cap_pf <= rhs.upstream_load_cap_pf + epsilon && lhs.current_slew_ns <= rhs.current_slew_ns + epsilon;
  const bool strictly_better
      = lhs.conservative_delay_ns < rhs.conservative_delay_ns - epsilon || lhs.conservative_power_w < rhs.conservative_power_w - epsilon
        || lhs.upstream_load_cap_pf < rhs.upstream_load_cap_pf - epsilon || lhs.current_slew_ns < rhs.current_slew_ns - epsilon;
  return no_worse && strictly_better;
}

auto SamePartialStructuralState(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  if (lhs.has_composition_state != rhs.has_composition_state) {
    return false;
  }
  if (!lhs.has_composition_state) {
    return true;
  }
  return lhs.composition_state.terminal_semantic == rhs.composition_state.terminal_semantic
         && lhs.composition_state.monotonic_boundary_state == rhs.composition_state.monotonic_boundary_state
         && lhs.composition_state.source_exposed_load_count == rhs.composition_state.source_exposed_load_count;
}

auto BuildPartialParetoFront(std::vector<PartialAnalyticalCandidate> candidates) -> std::vector<PartialAnalyticalCandidate>
{
  std::ranges::sort(candidates, PreferPartialCandidate);
  std::vector<PartialAnalyticalCandidate> frontier;
  frontier.reserve(candidates.size());
  for (auto& candidate : candidates) {
    bool dominated = false;
    for (const auto& kept : frontier) {
      if (PartialDominates(kept, candidate)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      frontier.push_back(std::move(candidate));
    }
  }
  return frontier;
}

auto PushBestPerPartialStructuralState(std::vector<PartialAnalyticalCandidate>& target,
                                       const std::vector<PartialAnalyticalCandidate>& candidates, std::size_t max_size) -> void
{
  for (const auto& candidate : candidates) {
    const auto same_state_it = std::ranges::find_if(
        target, [&](const PartialAnalyticalCandidate& existing) -> bool { return SamePartialStructuralState(existing, candidate); });
    if (same_state_it == target.end()) {
      PushUnique(target, candidate, SamePartialCandidateTrace);
    }
    if (target.size() >= max_size) {
      return;
    }
  }
}

auto TrimPartialCandidates(std::vector<PartialAnalyticalCandidate> candidates, std::size_t max_size)
    -> std::vector<PartialAnalyticalCandidate>
{
  if (max_size == 0U || candidates.size() <= max_size) {
    std::ranges::sort(candidates, PreferPartialCandidate);
    return candidates;
  }

  auto pareto_front = BuildPartialParetoFront(std::move(candidates));
  if (pareto_front.size() <= max_size) {
    return pareto_front;
  }

  std::vector<PartialAnalyticalCandidate> trimmed;
  trimmed.reserve(max_size);
  auto candidates_for_anchors = pareto_front;
  std::ranges::sort(candidates_for_anchors, PreferPartialCandidate);
  PushBestPerPartialStructuralState(trimmed, candidates_for_anchors, std::max<std::size_t>(1U, max_size / 4U));

  const std::size_t delay_anchor_limit = std::max<std::size_t>(trimmed.size(), max_size / 3U);
  const std::size_t power_anchor_limit = std::max(delay_anchor_limit, max_size / 2U);
  const std::size_t slew_anchor_limit = std::max(power_anchor_limit, (2U * max_size) / 3U);
  const std::size_t low_cap_anchor_limit = std::max(slew_anchor_limit, (5U * max_size) / 6U);
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= delay_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.conservative_power_w != rhs.conservative_power_w) {
      return lhs.conservative_power_w < rhs.conservative_power_w;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= power_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.current_slew_ns != rhs.current_slew_ns) {
      return lhs.current_slew_ns < rhs.current_slew_ns;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= slew_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.upstream_load_cap_pf != rhs.upstream_load_cap_pf) {
      return lhs.upstream_load_cap_pf < rhs.upstream_load_cap_pf;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= low_cap_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.upstream_load_cap_pf != rhs.upstream_load_cap_pf) {
      return lhs.upstream_load_cap_pf > rhs.upstream_load_cap_pf;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= max_size) {
      break;
    }
  }

  std::ranges::sort(trimmed, PreferPartialCandidate);
  return trimmed;
}

auto SegmentHasAnyBuffer(const AnalyticalSolverRequest& request, PatternId pattern_id) -> bool
{
  const auto* pattern = request.segment_pattern_library->find(pattern_id);
  return pattern != nullptr && pattern->isBufferPattern();
}

auto MakeSegmentChoice(std::size_t level_index, const ScoredSegment& selected) -> AnalyticalSegmentChoice
{
  return AnalyticalSegmentChoice{
      .level_index = level_index,
      .length_idx = selected.length_idx,
      .segment_pattern_id = selected.pattern_id,
      .input_slew_ns = selected.input_slew_ns,
      .downstream_load_cap_pf = selected.downstream_load_cap_pf,
      .output_slew_ns = selected.output_slew_ns,
      .source_cap_pf = selected.source_cap_pf,
      .delay_ns = selected.delay_ns,
      .power_w = selected.power_w,
      .source_boundary_power_w = selected.source_boundary_power_w,
      .slew_upper_ns = selected.slew_upper_ns,
      .delay_upper_ns = selected.delay_upper_ns,
      .power_upper_w = selected.power_upper_w,
  };
}

auto AccumulateHTreePower(double accumulated_power_w, std::size_t level_index, const ScoredSegment& selected) -> double
{
  if (level_index == 0U) {
    return accumulated_power_w + selected.power_w;
  }
  const double level_multiplicity = std::ldexp(1.0, static_cast<int>(level_index));
  return accumulated_power_w + level_multiplicity * (selected.power_w - selected.source_boundary_power_w);
}

auto ResolveMergedSourceExposedLoadCount(const PatternCompositionState& upstream_state, const PatternCompositionState& downstream_state)
    -> std::size_t
{
  if (upstream_state.monotonic_boundary_state.source.has_buffer) {
    return 1U;
  }
  if (downstream_state.source_exposed_load_count > std::numeric_limits<std::size_t>::max() / 2U) {
    return std::numeric_limits<std::size_t>::max();
  }
  return downstream_state.source_exposed_load_count * 2U;
}

auto TryPrependCompositionState(const AnalyticalSolverRequest& request, const PartialAnalyticalCandidate& downstream,
                                PatternId upstream_segment_pattern_id) -> std::optional<PatternCompositionState>
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
  if (segment_pattern_library == nullptr) {
    return std::nullopt;
  }
  const auto upstream_state = segment_pattern_library->getCompositionState(upstream_segment_pattern_id);
  if (!downstream.has_composition_state) {
    return upstream_state;
  }
  if (!upstream_state.monotonic_boundary_state.canComposeWith(downstream.composition_state.monotonic_boundary_state)) {
    return std::nullopt;
  }
  if (!IsBinarySourceFanoutLegal(downstream.composition_state.source_exposed_load_count, request.fanout_options.max_fanout)) {
    return std::nullopt;
  }
  return PatternCompositionState{
      .terminal_semantic = downstream.composition_state.terminal_semantic,
      .monotonic_boundary_state
      = MonotonicBoundaryState::compose(upstream_state.monotonic_boundary_state, downstream.composition_state.monotonic_boundary_state),
      .source_exposed_load_count = ResolveMergedSourceExposedLoadCount(upstream_state, downstream.composition_state),
  };
}

auto CanAppendUnitPattern(const BufferPatternLibrary& segment_pattern_library, const std::vector<PatternId>& unit_pattern_ids,
                          PatternId next_pattern_id) -> bool
{
  const auto* next_pattern = segment_pattern_library.find(next_pattern_id);
  if (next_pattern == nullptr) {
    return false;
  }
  if (unit_pattern_ids.empty()) {
    return true;
  }

  const auto* upstream_pattern = segment_pattern_library.find(unit_pattern_ids.back());
  if (upstream_pattern == nullptr) {
    return false;
  }
  return upstream_pattern->get_monotonic_boundary_state().canComposeWith(next_pattern->get_monotonic_boundary_state());
}

auto IsFunctionalSequenceAllowedForLevel(const AnalyticalSolverRequest& request, const HTree::LevelPlan& level,
                                         PatternId segment_pattern_id) -> bool
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
  if (segment_pattern_library == nullptr) {
    return false;
  }
  if (request.boundary_constraints.force_branch_buffer
      && segment_pattern_library->getTerminalSemantic(segment_pattern_id) != TerminalSemantic::kBranchBuffered) {
    return false;
  }
  if (level.is_leaf_level && request.fanout_options.max_fanout > 0U && !SegmentHasAnyBuffer(request, segment_pattern_id)) {
    return false;
  }
  return true;
}

auto ShortlistFrontierFunctionalSegmentsForLevel(const AnalyticalSolverRequest& request, const HTree::LevelPlan& level,
                                                 std::size_t level_index, double input_slew_ns, double downstream_cap_pf,
                                                 FunctionalComposeContext& context, AnalyticalSolverResult& result)
    -> std::vector<ScoredSegment>
{
  const ScoredSegmentCacheKey cache_key{
      .length_idx = level.aligned_length_idx,
      .leaf_level = level.is_leaf_level,
      .force_branch_buffer = request.boundary_constraints.force_branch_buffer,
      .input_slew_ns = input_slew_ns,
      .downstream_cap_pf = downstream_cap_pf,
  };
  if (const auto cache_it = context.scored_segments_by_key.find(cache_key); cache_it != context.scored_segments_by_key.end()) {
    const auto diagnostic_pattern_ids = DiagnosticPatternIds(request, level_index);
    for (const auto& scored : cache_it->second) {
      RecordDiagnosticPatternStage(diagnostic_pattern_ids, scored.pattern_id, DiagnosticPatternStage::kShortlisted, result);
    }
    return cache_it->second;
  }

  if (request.segment_frontier_catalog == nullptr) {
    return {};
  }
  const SegmentFrontierKind frontier_kind
      = request.boundary_constraints.force_branch_buffer ? SegmentFrontierKind::kTerminalBranchBuffered : SegmentFrontierKind::kAll;
  const auto* frontier = request.segment_frontier_catalog->find(level.aligned_length_idx, frontier_kind);
  if (frontier == nullptr || frontier->empty()) {
    return {};
  }

  std::vector<ScoredSegment> scored_segments;
  scored_segments.reserve(frontier->size());
  const auto diagnostic_pattern_ids = DiagnosticPatternIds(request, level_index);
  for (const auto& segment_char : *frontier) {
    ++result.evaluated_segment_count;
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, segment_char.get_pattern_id(), DiagnosticPatternStage::kFrontier, result);
    if (!IsFunctionalSequenceAllowedForLevel(request, level, segment_char.get_pattern_id())) {
      continue;
    }
    auto unit_pattern_ids = DecomposePatternToUnitSequence(segment_char.get_pattern_id(), request, context);
    if (unit_pattern_ids.empty()) {
      ++result.decomposition_rejected_count;
      continue;
    }
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, segment_char.get_pattern_id(), DiagnosticPatternStage::kDecomposed, result);
    auto scored = ScoreFunctionalUnitSequence(request, unit_pattern_ids, segment_char.get_pattern_id(), segment_char.get_length_idx(),
                                              input_slew_ns, downstream_cap_pf, request.options.use_conservative_scoring, result);
    if (scored.has_value()) {
      scored_segments.push_back(std::move(*scored));
      ++result.scored_segment_count;
      RecordDiagnosticPatternStage(diagnostic_pattern_ids, segment_char.get_pattern_id(), DiagnosticPatternStage::kScored, result);
    }
  }
  auto shortlist = TrimScoredSegments(std::move(scored_segments), request.options.per_level_shortlist_size);
  for (const auto& scored : shortlist) {
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, scored.pattern_id, DiagnosticPatternStage::kShortlisted, result);
  }
  return context.scored_segments_by_key.emplace(cache_key, std::move(shortlist)).first->second;
}

auto ShortlistFunctionalSegmentsForLevel(const AnalyticalSolverRequest& request, const HTree::LevelPlan& level, double input_slew_ns,
                                         std::size_t level_index, double downstream_cap_pf, FunctionalComposeContext& context,
                                         AnalyticalSolverResult& result) -> std::vector<ScoredSegment>
{
  if (request.segment_frontier_catalog != nullptr) {
    auto frontier_segments
        = ShortlistFrontierFunctionalSegmentsForLevel(request, level, level_index, input_slew_ns, downstream_cap_pf, context, result);
    if (!frontier_segments.empty()) {
      return frontier_segments;
    }
  }

  auto* mutable_segment_pattern_library = request.mutable_segment_pattern_library;
  if (mutable_segment_pattern_library == nullptr || context.unit_models.empty() || level.aligned_length_idx == 0U
      || request.options.unit_length_idx == 0U || level.aligned_length_idx % request.options.unit_length_idx != 0U) {
    return {};
  }

  const unsigned unit_count = level.aligned_length_idx / request.options.unit_length_idx;
  struct FunctionalSequenceState
  {
    std::vector<PatternId> unit_pattern_ids;
    ScoredSegment scored;
  };

  std::vector<FunctionalSequenceState> beam = {FunctionalSequenceState{}};
  const std::size_t beam_width
      = std::max<std::size_t>(1U, std::min(request.options.unit_compose_beam_size, request.options.per_level_shortlist_size));
  for (unsigned unit_index = 0U; unit_index < unit_count; ++unit_index) {
    std::vector<FunctionalSequenceState> next_beam;
    for (const auto& partial : beam) {
      for (const auto& unit_model : context.unit_models) {
        if (!CanAppendUnitPattern(*mutable_segment_pattern_library, partial.unit_pattern_ids, unit_model.pattern_id)) {
          continue;
        }
        auto unit_pattern_ids = partial.unit_pattern_ids;
        unit_pattern_ids.push_back(unit_model.pattern_id);
        PatternId scoring_pattern_id = unit_pattern_ids.back();
        if (unit_pattern_ids.size() == unit_count) {
          auto materialized_pattern_id = MaterializeFunctionalSegmentPattern(unit_pattern_ids, context, *mutable_segment_pattern_library);
          if (!materialized_pattern_id.has_value() || !IsFunctionalSequenceAllowedForLevel(request, level, *materialized_pattern_id)) {
            continue;
          }
          scoring_pattern_id = *materialized_pattern_id;
        }
        ++result.evaluated_segment_count;
        auto scored = ScoreFunctionalUnitSequence(request, unit_pattern_ids, scoring_pattern_id,
                                                  static_cast<unsigned>(unit_pattern_ids.size() * request.options.unit_length_idx),
                                                  input_slew_ns, downstream_cap_pf, request.options.use_conservative_scoring, result);
        if (!scored.has_value()) {
          continue;
        }
        ++result.scored_segment_count;
        next_beam.push_back(FunctionalSequenceState{.unit_pattern_ids = std::move(unit_pattern_ids), .scored = std::move(*scored)});
      }
    }
    std::ranges::sort(next_beam, [](const FunctionalSequenceState& lhs, const FunctionalSequenceState& rhs) -> bool {
      return PreferScoredSegment(lhs.scored, rhs.scored);
    });
    if (next_beam.size() > beam_width) {
      std::vector<ScoredSegment> scored_for_trim;
      scored_for_trim.reserve(next_beam.size());
      for (const auto& state : next_beam) {
        scored_for_trim.push_back(state.scored);
      }
      auto trimmed_scored = TrimScoredSegments(std::move(scored_for_trim), beam_width);
      std::vector<FunctionalSequenceState> trimmed_next_beam;
      trimmed_next_beam.reserve(trimmed_scored.size());
      for (auto& scored : trimmed_scored) {
        trimmed_next_beam.push_back(FunctionalSequenceState{.unit_pattern_ids = scored.unit_pattern_ids, .scored = std::move(scored)});
      }
      next_beam = std::move(trimmed_next_beam);
    }
    beam = std::move(next_beam);
    if (beam.empty()) {
      return {};
    }
  }

  std::vector<ScoredSegment> scored_segments;
  scored_segments.reserve(beam.size());
  for (auto& state : beam) {
    if (state.scored.length_idx == level.aligned_length_idx) {
      scored_segments.push_back(std::move(state.scored));
    }
  }
  return TrimScoredSegments(std::move(scored_segments), request.options.per_level_shortlist_size);
}

auto ShortlistSegmentsForLevel(const AnalyticalSolverRequest& request, const HTree::LevelPlan& level, double input_slew_ns,
                               std::size_t level_index, double downstream_cap_pf, FunctionalComposeContext* functional_context,
                               AnalyticalSolverResult& result) -> std::vector<ScoredSegment>
{
  if (request.options.use_functional_unit_compose && functional_context != nullptr) {
    return ShortlistFunctionalSegmentsForLevel(request, level, input_slew_ns, level_index, downstream_cap_pf, *functional_context, result);
  }

  const SegmentFrontierKind frontier_kind
      = request.boundary_constraints.force_branch_buffer ? SegmentFrontierKind::kTerminalBranchBuffered : SegmentFrontierKind::kAll;
  const auto* frontier = request.segment_frontier_catalog->find(level.aligned_length_idx, frontier_kind);
  if (frontier == nullptr || frontier->empty()) {
    return {};
  }

  std::vector<ScoredSegment> scored_segments;
  scored_segments.reserve(frontier->size());
  for (const auto& segment_char : *frontier) {
    ++result.evaluated_segment_count;
    if (level.is_leaf_level && request.fanout_options.max_fanout > 0U && !SegmentHasAnyBuffer(request, segment_char.get_pattern_id())) {
      continue;
    }
    const auto* model_set = request.model_catalog->find(AnalyticalModelKey{
        .pattern_id = segment_char.get_pattern_id(),
        .length_idx = segment_char.get_length_idx(),
    });
    if (model_set == nullptr || !model_set->isComplete()) {
      ++result.missing_model_count;
      continue;
    }
    auto scored = ScoreSegment(segment_char, *model_set, input_slew_ns, downstream_cap_pf, request.options.use_conservative_scoring);
    if (scored.has_value()) {
      scored_segments.push_back(*scored);
      ++result.scored_segment_count;
    } else {
      ++result.metric_evaluation_rejected_count;
    }
  }

  auto shortlist = TrimScoredSegments(std::move(scored_segments), request.options.per_level_shortlist_size);
  const auto diagnostic_pattern_ids = DiagnosticPatternIds(request, level_index);
  for (const auto& scored : shortlist) {
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, scored.pattern_id, DiagnosticPatternStage::kShortlisted, result);
  }
  return shortlist;
}

auto EvaluatePartialRootToLeaf(const AnalyticalSolverRequest& request, PartialAnalyticalCandidate& partial, AnalyticalSolverResult& result)
    -> bool
{
  if (partial.level_segment_pattern_ids.size() != request.levels->size() || partial.level_load_caps_pf.size() != request.levels->size()) {
    return false;
  }
  if (request.options.use_functional_unit_compose && partial.level_unit_pattern_ids.size() != request.levels->size()) {
    return false;
  }

  double current_slew_ns = ResolveModelInputRootSlewNs(request);
  double accumulated_delay_ns = 0.0;
  double accumulated_power_w = 0.0;
  double conservative_delay_ns = 0.0;
  double conservative_power_w = 0.0;
  double root_source_cap_pf = 0.0;
  std::vector<AnalyticalSegmentChoice> trace;
  trace.reserve(partial.level_segment_pattern_ids.size());

  for (std::size_t level_index = 0U; level_index < partial.level_segment_pattern_ids.size(); ++level_index) {
    const auto pattern_id = partial.level_segment_pattern_ids.at(level_index);
    const auto length_idx = request.levels->at(level_index).aligned_length_idx;
    const double load_cap_pf = partial.level_load_caps_pf.at(level_index);
    std::optional<ScoredSegment> scored;
    if (request.options.use_functional_unit_compose) {
      scored = ScoreFunctionalUnitSequence(request, partial.level_unit_pattern_ids.at(level_index), pattern_id, length_idx, current_slew_ns,
                                           load_cap_pf, false, result);
    } else {
      const auto* model_set = request.model_catalog->find(AnalyticalModelKey{
          .pattern_id = pattern_id,
          .length_idx = length_idx,
      });
      if (model_set == nullptr || !model_set->isComplete()) {
        ++result.missing_model_count;
        return false;
      }
      scored = ScoreModelSet(pattern_id, length_idx, *model_set, current_slew_ns, load_cap_pf, false);
    }
    if (!scored.has_value()) {
      if (!request.options.use_functional_unit_compose) {
        ++result.metric_evaluation_rejected_count;
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

auto BuildCandidateFromPartial(const AnalyticalSolverRequest& request, PartialAnalyticalCandidate partial, AnalyticalSolverResult& result)
    -> std::optional<AnalyticalCandidate>
{
  ++result.materialization_attempt_count;
  if (!EvaluatePartialRootToLeaf(request, partial, result)) {
    return std::nullopt;
  }

  AnalyticalCandidate candidate;
  candidate.depth = static_cast<unsigned>(request.levels->size());
  candidate.leaf_load_cap_pf = request.options.representative_leaf_load_cap_pf;
  candidate.root_input_slew_ns = request.options.root_input_slew_ns;
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
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
  if (segment_pattern_library == nullptr) {
    candidate.rejection_reason = "missing_segment_pattern_library";
    return std::nullopt;
  }
  auto topology_pattern_library
      = BuildAnalyticalTopologyPattern(candidate.level_segment_pattern_ids, *segment_pattern_library, request.fanout_options.max_fanout);
  if (!topology_pattern_library.has_value()) {
    candidate.rejection_reason = "topology_pattern_composition_illegal";
    ++result.root_fanout_rejected_count;
    return std::nullopt;
  }
  candidate.topology_pattern_library = std::move(*topology_pattern_library);
  const PatternId topology_pattern_id = PatternId::topology(
      candidate.topology_pattern_library.nodes.empty() ? 0U : static_cast<unsigned>(candidate.topology_pattern_library.nodes.size() - 1U));
  const auto composition_state = candidate.topology_pattern_library.getCompositionState(topology_pattern_id);
  candidate.fanout_legal = IsBinarySourceFanoutLegal(composition_state.source_exposed_load_count, request.fanout_options.max_fanout);
  if (!candidate.fanout_legal) {
    candidate.rejection_reason = "root_fanout_illegal";
    ++result.root_fanout_rejected_count;
    return std::nullopt;
  }
  candidate.materialized_char = MaterializeAnalyticalTopologyChar(candidate, request.slew_lattice, request.cap_lattice);
  if (!candidate.materialized_char.has_value()) {
    candidate.rejection_reason = "materialized_char_out_of_lattice";
    ++result.lattice_rejected_count;
    return std::nullopt;
  }
  return candidate;
}

auto BuildDiagnosticDirectCandidate(const AnalyticalSolverRequest& request, FunctionalComposeContext& functional_context,
                                    AnalyticalSolverResult& result) -> std::optional<AnalyticalCandidate>
{
  if (!request.options.use_functional_unit_compose || request.options.diagnostic_segment_pattern_ids.empty() || request.levels == nullptr
      || request.options.diagnostic_segment_pattern_ids.size() != request.levels->size()) {
    return std::nullopt;
  }

  PartialAnalyticalCandidate partial;
  partial.current_slew_ns = ResolveModelInputRootSlewNs(request);
  partial.upstream_load_cap_pf = request.options.representative_leaf_load_cap_pf;
  for (std::size_t reverse_level_index = request.levels->size(); reverse_level_index > 0U; --reverse_level_index) {
    const std::size_t level_index = reverse_level_index - 1U;
    const auto pattern_id = request.options.diagnostic_segment_pattern_ids.at(level_index);
    const auto unit_pattern_ids = DecomposePatternToUnitSequence(pattern_id, request, functional_context);
    if (unit_pattern_ids.empty()) {
      return std::nullopt;
    }
    auto composition_state = TryPrependCompositionState(request, partial, pattern_id);
    if (!composition_state.has_value()) {
      return std::nullopt;
    }
    partial.has_composition_state = true;
    partial.composition_state = *composition_state;
    partial.level_segment_pattern_ids.insert(partial.level_segment_pattern_ids.begin(), pattern_id);
    partial.level_unit_pattern_ids.insert(partial.level_unit_pattern_ids.begin(), unit_pattern_ids);
    partial.level_load_caps_pf.insert(partial.level_load_caps_pf.begin(), partial.upstream_load_cap_pf);
    auto scored = ScoreFunctionalUnitSequence(request, unit_pattern_ids, pattern_id, request.levels->at(level_index).aligned_length_idx,
                                              ResolveModelInputRootSlewNs(request), partial.upstream_load_cap_pf,
                                              request.options.use_conservative_scoring, result);
    if (!scored.has_value()) {
      return std::nullopt;
    }
    partial.upstream_load_cap_pf = scored->source_cap_pf * 2.0;
  }

  auto candidate = BuildCandidateFromPartial(request, std::move(partial), result);
  if (!candidate.has_value() || !candidate->materialized_char.has_value()) {
    return std::nullopt;
  }
  ++result.diagnostic_direct_candidate_count;
  result.diagnostic_direct_delay_ns = candidate->materialized_char->get_delay();
  result.diagnostic_direct_power_w = candidate->materialized_char->get_power();
  result.diagnostic_direct_root_cap_pf = candidate->root_source_cap_pf;
  result.diagnostic_direct_input_slew_idx = candidate->materialized_char->get_input_slew_idx();
  result.diagnostic_direct_output_slew_idx = candidate->materialized_char->get_output_slew_idx();
  result.diagnostic_direct_driven_cap_idx = candidate->materialized_char->get_driven_cap_idx();
  return candidate;
}

auto BuildBeamCandidates(const AnalyticalSolverRequest& request, AnalyticalSolverResult& result) -> std::vector<AnalyticalCandidate>
{
  const double model_input_root_slew_ns = ResolveModelInputRootSlewNs(request);
  FunctionalComposeContext functional_context;
  FunctionalComposeContext* functional_context_ptr = nullptr;
  if (request.options.use_functional_unit_compose) {
    functional_context.unit_models = CollectUnitModelRefs(request);
    if (functional_context.unit_models.empty()) {
      result.first_empty_reason = "empty_unit_model_catalog";
      return {};
    }
    functional_context.unit_pattern_by_cell_master_and_terminal_semantic
        = BuildUnitPatternByCellMaster(request, functional_context.unit_models);
    const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
    functional_context.next_segment_pattern_id
        = segment_pattern_library == nullptr ? 0U : ResolveNextSegmentPatternId(*segment_pattern_library);
    functional_context_ptr = &functional_context;
    (void) BuildDiagnosticDirectCandidate(request, functional_context, result);
  }
  PartialAnalyticalCandidate seed_candidate;
  seed_candidate.current_slew_ns = model_input_root_slew_ns;
  seed_candidate.upstream_load_cap_pf = request.options.representative_leaf_load_cap_pf;
  std::vector<PartialAnalyticalCandidate> beam = {std::move(seed_candidate)};

  const std::size_t beam_width = std::max<std::size_t>(1U, request.options.top_k_per_depth);
  for (std::size_t reverse_level_index = request.levels->size(); reverse_level_index > 0U; --reverse_level_index) {
    const std::size_t level_index = reverse_level_index - 1U;
    const auto& level = request.levels->at(level_index);
    std::vector<PartialAnalyticalCandidate> next_beam;
    for (const auto& partial : beam) {
      const double level_load_cap_pf = partial.upstream_load_cap_pf;
      auto shortlist = ShortlistSegmentsForLevel(request, level, model_input_root_slew_ns, level_index, level_load_cap_pf,
                                                 functional_context_ptr, result);
      if (shortlist.empty()) {
        ++result.empty_shortlist_count;
        if (result.first_empty_reason.empty()) {
          result.first_empty_level_index = static_cast<unsigned>(level_index);
          result.first_empty_length_idx = level.aligned_length_idx;
          result.first_empty_reason = "empty_level_shortlist";
        }
      }
      for (const auto& selected : shortlist) {
        auto composition_state = TryPrependCompositionState(request, partial, selected.pattern_id);
        if (!composition_state.has_value()) {
          ++result.root_fanout_rejected_count;
          continue;
        }
        auto expanded = partial;
        expanded.has_composition_state = true;
        expanded.composition_state = *composition_state;
        expanded.level_segment_pattern_ids.insert(expanded.level_segment_pattern_ids.begin(), selected.pattern_id);
        if (request.options.use_functional_unit_compose) {
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
    auto candidate = BuildCandidateFromPartial(request, std::move(partial), result);
    if (candidate.has_value()) {
      if (candidate->level_segment_pattern_ids == request.options.diagnostic_segment_pattern_ids) {
        ++result.diagnostic_generated_candidate_count;
      }
      candidates.push_back(std::move(*candidate));
      ++result.generated_candidate_count;
    }
  }
  std::ranges::sort(candidates, PreferAnalyticalCandidate);
  if (request.options.top_k_per_depth > 0U && candidates.size() > request.options.top_k_per_depth) {
    candidates.resize(request.options.top_k_per_depth);
  }
  return candidates;
}

}  // namespace

auto SolveAnalyticalHTreeCandidates(const AnalyticalSolverRequest& request) -> AnalyticalSolverResult
{
  const auto validation_failure = ValidateRequest(request);
  if (!validation_failure.empty()) {
    return MakeFailure(validation_failure);
  }

  AnalyticalSolverResult result;
  RecordDiagnosticLibraryHits(request, result);
  result.candidates = BuildBeamCandidates(request, result);
  if (result.candidates.empty()) {
    result.success = false;
    if (result.root_fanout_rejected_count > 0U) {
      result.failure_reason = "root_fanout_illegal";
    } else if (result.lattice_rejected_count > 0U) {
      result.failure_reason = "materialized_char_out_of_lattice";
    } else {
      result.failure_reason = "no_analytical_candidate";
    }
    return result;
  }

  result.success = true;
  return result;
}

}  // namespace icts::htree::analytical_solver
