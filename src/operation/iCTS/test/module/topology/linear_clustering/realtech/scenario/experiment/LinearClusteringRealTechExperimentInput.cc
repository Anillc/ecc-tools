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
 * @file LinearClusteringRealTechExperimentInput.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Input, strategy, and representative-case construction for real-tech linear clustering experiments.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Inst.hh"
#include "Pin.hh"
#include "Point.hh"
#include "database/config/Config.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/LinearOrderGenerator.hh"
#include "module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"

namespace icts_test::linear_clustering::realtech::experiment {

auto ClampToIntRange(double value) -> int
{
  constexpr double min_int_half = static_cast<double>(std::numeric_limits<int>::min()) / 2.0;
  constexpr double max_int_half = static_cast<double>(std::numeric_limits<int>::max()) / 2.0;
  const double clamped = std::clamp(value, min_int_half, max_int_half);
  return static_cast<int>(std::lround(clamped));
}

auto CalcBoundingBoxDiameter(const std::vector<icts::Pin*>& loads) -> int
{
  return loads.empty() ? 0 : CalcClusterDiameter(loads);
}

auto HasExactCapContext(const icts::Pin* pin) -> bool
{
  return pin != nullptr && pin->get_inst() != nullptr && pin->get_net() != nullptr && !pin->get_name().empty()
         && !pin->get_inst()->get_name().empty();
}

auto CalcPercentileValue(std::vector<double> values, double percentile) -> double
{
  if (values.empty()) {
    return 0.0;
  }

  std::ranges::sort(values);
  const auto clamped_percentile = std::clamp(percentile, 0.0, 1.0);
  const auto scaled_index = clamped_percentile * static_cast<double>(values.size() - 1U);
  const auto percentile_index = static_cast<std::size_t>(std::ceil(scaled_index));
  return values.at(std::min(percentile_index, values.size() - 1U));
}

auto SelectRoutingLayerFromConfig() -> int
{
  const auto routing_layers = CONFIG_INST.get_routing_layers();
  if (!routing_layers.empty()) {
    return static_cast<int>(routing_layers.back());
  }
  return 0;
}

auto BuildArm9ExperimentConfig(const FanoutConfigSpec& spec) -> icts::LinearClusteringConfig
{
  icts::LinearClusteringConfig config{};
  config.router_kind = icts::LinearRouterKind::kFlute;
  config.root_policy = icts::LinearRootPolicy::kMedian;
  config.scoring_strategy = icts::LinearScoringStrategy::kMaxDiameter;
  config.max_fanout = spec.fanout_limit;
  config.max_diameter = std::max(1, spec.max_diameter);
  config.max_cap = CONFIG_INST.has_max_cap() ? CONFIG_INST.get_max_cap() : std::numeric_limits<double>::infinity();
  config.enable_exact_cap = spec.enable_exact_cap;
  config.always_build_exact_cap = spec.enable_exact_cap;
  config.order_strategy = spec.order_strategy;
  config.discrete_hilbert_encoding = spec.discrete_hilbert_encoding;
  config.hilbert_transform = spec.hilbert_transform;
  config.order_bits = spec.order_bits;
  config.split_strategy = spec.split_strategy;
  config.sweep_mode = spec.sweep_mode;
  config.strided_sweep_count = spec.strided_sweep_count;
  config.routing_layer = SelectRoutingLayerFromConfig();
  config.wire_width = CONFIG_INST.get_wire_width();
  return config;
}

auto BuildStrategyLabel(const StrategySweepCandidate& candidate) -> std::string
{
  return StrategyLabel(candidate.order_strategy, candidate.discrete_hilbert_encoding, candidate.hilbert_transform, candidate.order_bits,
                       candidate.split_strategy, candidate.sweep_mode, candidate.strided_sweep_count);
}

auto SyntheticDistributionFamilyName(SyntheticDistributionFamily family) -> const char*
{
  switch (family) {
    case SyntheticDistributionFamily::kArm9Subset:
      return "arm9_subset";
    case SyntheticDistributionFamily::kGaussianHotspots:
      return "gaussian_hotspots";
    case SyntheticDistributionFamily::kAxisBands:
      return "axis_bands";
    case SyntheticDistributionFamily::kDiagonalBand:
      return "diagonal_band";
    case SyntheticDistributionFamily::kUniformSpread:
      return "uniform_spread";
  }
  return "unknown";
}

auto SyntheticLoadScaleName(SyntheticLoadScale load_scale) -> const char*
{
  switch (load_scale) {
    case SyntheticLoadScale::kLoads500:
      return "500";
    case SyntheticLoadScale::kLoads1000:
      return "1000";
    case SyntheticLoadScale::kLoads2000:
      return "2000";
    case SyntheticLoadScale::kLoads5000:
      return "5000";
  }
  return "unknown";
}

auto StrategyClassName(StrategyClass strategy_class) -> const char*
{
  switch (strategy_class) {
    case StrategyClass::kContinuous:
      return "continuous";
    case StrategyClass::kDiscrete:
      return "discrete";
  }
  return "unknown";
}

auto IsDiscreteOrderStrategy(icts::LinearOrderStrategy order_strategy) -> bool
{
  return order_strategy == icts::LinearOrderStrategy::kDiscreteHilbert
         || order_strategy == icts::LinearOrderStrategy::kDensityScaledDiscreteHilbert;
}

auto ClampUnit(double value) -> double
{
  return std::clamp(value, 0.0, 1.0);
}

auto ReadEnvSizeValue(std::string_view env_name) -> std::optional<std::size_t>
{
  const auto env_key = std::string(env_name);
  const char* env_value = std::getenv(env_key.c_str());
  if (env_value == nullptr || *env_value == '\0') {
    return std::nullopt;
  }

  char* end_ptr = nullptr;
  const auto parsed = std::strtoull(env_value, &end_ptr, 10);
  if (end_ptr == env_value || (end_ptr != nullptr && *end_ptr != '\0')) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(parsed);
}

auto ReadEnvFlag(std::string_view env_name) -> bool
{
  return ReadEnvSizeValue(env_name).value_or(0U) != 0U;
}

auto BuildRetainedBenchmarkTemplates() -> std::vector<BenchmarkStrategyTemplate>
{
  return {
      {
          .strategy_class = StrategyClass::kDiscrete,
          .order_strategy = icts::LinearOrderStrategy::kDiscreteHilbert,
          .discrete_hilbert_encoding = icts::DiscreteHilbertEncoding::kClassicIndex,
          .hilbert_transform = icts::HilbertTransform::kSwapXY,
          .order_bits = 10,
          .split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy,
          .sweep_mode = icts::LinearSweepMode::kPrefixAndStridedSweep,
          .strided_sweep_count = kDefaultStridedSweepCount,
          .rationale = "best real-arm9 discrete strategy from the final retained set",
      },
      {
          .strategy_class = StrategyClass::kDiscrete,
          .order_strategy = icts::LinearOrderStrategy::kDiscreteHilbert,
          .discrete_hilbert_encoding = icts::DiscreteHilbertEncoding::kClassicIndexTangent,
          .hilbert_transform = icts::HilbertTransform::kSwapXY,
          .order_bits = 10,
          .split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy,
          .sweep_mode = icts::LinearSweepMode::kPrefixAndStridedSweep,
          .strided_sweep_count = kDefaultStridedSweepCount,
          .rationale = "second-best real-arm9 discrete strategy from the final retained set",
      },
      {
          .strategy_class = StrategyClass::kContinuous,
          .order_strategy = icts::LinearOrderStrategy::kContinuousHilbert,
          .split_strategy = icts::LinearSplitStrategy::kReverseGreedy,
          .sweep_mode = icts::LinearSweepMode::kPrefixAndStridedSweep,
          .strided_sweep_count = kDefaultStridedSweepCount,
          .rationale = "best continuous baseline in the representative benchmark; retained as real-arm9 competitive",
      },
      {
          .strategy_class = StrategyClass::kContinuous,
          .order_strategy = icts::LinearOrderStrategy::kContinuousHilbert,
          .split_strategy = icts::LinearSplitStrategy::kForwardGreedy,
          .sweep_mode = icts::LinearSweepMode::kPrefixAndStridedSweep,
          .strided_sweep_count = kDefaultStridedSweepCount,
          .rationale = "second-best continuous baseline in the representative benchmark; retained as real-arm9 competitive",
      },
  };
}

auto BuildStrategyDescriptors(const std::vector<BenchmarkStrategyTemplate>& templates)
    -> std::unordered_map<std::string, StrategyDescriptor>
{
  std::unordered_map<std::string, StrategyDescriptor> descriptors;
  descriptors.reserve(templates.size());
  for (const auto& strategy_template : templates) {
    const auto label = StrategyLabel(strategy_template.order_strategy, strategy_template.discrete_hilbert_encoding,
                                     strategy_template.hilbert_transform, strategy_template.order_bits, strategy_template.split_strategy,
                                     strategy_template.sweep_mode, strategy_template.strided_sweep_count);
    descriptors.emplace(label, StrategyDescriptor{
                                   .label = label,
                                   .strategy_class = strategy_template.strategy_class,
                                   .order_strategy = strategy_template.order_strategy,
                                   .discrete_hilbert_encoding = strategy_template.discrete_hilbert_encoding,
                                   .hilbert_transform = strategy_template.hilbert_transform,
                                   .order_bits = strategy_template.order_bits,
                                   .split_strategy = strategy_template.split_strategy,
                                   .sweep_mode = strategy_template.sweep_mode,
                                   .strided_sweep_count = strategy_template.strided_sweep_count,
                               });
  }
  return descriptors;
}

auto BuildBenchmarkSpecs(std::size_t max_fanout, int max_diameter, const std::vector<BenchmarkStrategyTemplate>& templates)
    -> std::vector<FanoutConfigSpec>
{
  std::vector<FanoutConfigSpec> specs;
  specs.reserve(templates.size());
  for (const auto& strategy_template : templates) {
    specs.push_back(FanoutConfigSpec{
        .fanout_limit = max_fanout,
        .max_diameter = max_diameter,
        .enable_exact_cap = true,
        .order_strategy = strategy_template.order_strategy,
        .discrete_hilbert_encoding = strategy_template.discrete_hilbert_encoding,
        .hilbert_transform = strategy_template.hilbert_transform,
        .order_bits = strategy_template.order_bits,
        .split_strategy = strategy_template.split_strategy,
        .sweep_mode = strategy_template.sweep_mode,
        .strided_sweep_count = strategy_template.strided_sweep_count,
    });
  }
  return specs;
}

auto CompareStrategyCandidates(const StrategySweepCandidate& lhs, const StrategySweepCandidate& rhs) -> bool
{
  const bool lhs_usable = lhs.legal && !lhs.empty_result;
  const bool rhs_usable = rhs.legal && !rhs.empty_result;
  if (lhs_usable != rhs_usable) {
    return lhs_usable;
  }
  if (!lhs_usable) {
    return BuildStrategyLabel(lhs) < BuildStrategyLabel(rhs);
  }
  if (lhs.selection_score != rhs.selection_score) {
    return lhs.selection_score < rhs.selection_score;
  }
  return BuildStrategyLabel(lhs) < BuildStrategyLabel(rhs);
}

auto CollectRankedCandidateIndices(const std::vector<StrategySweepCandidate>& candidates) -> std::vector<std::size_t>
{
  std::vector<std::size_t> ranked_indices(candidates.size());
  std::iota(ranked_indices.begin(), ranked_indices.end(), 0U);
  std::ranges::sort(ranked_indices, [&candidates](std::size_t lhs, std::size_t rhs) -> bool {
    return CompareStrategyCandidates(candidates.at(lhs), candidates.at(rhs));
  });
  return ranked_indices;
}

auto ResolveSelectedIndex(const std::vector<StrategySweepCandidate>& candidates) -> std::optional<std::size_t>
{
  for (const auto index : CollectRankedCandidateIndices(candidates)) {
    const auto& candidate = candidates.at(index);
    if (candidate.legal && !candidate.empty_result) {
      return index;
    }
  }
  return std::nullopt;
}

auto EvaluateExperimentCase(const std::vector<icts::Pin*>& loads, const std::vector<FanoutConfigSpec>& candidate_specs)
    -> ExperimentCaseRecord
{
  ExperimentCaseRecord record;
  record.load_count = loads.size();
  record.bounding_box_diameter = candidate_specs.empty() ? 0 : candidate_specs.front().max_diameter;
  record.candidates.reserve(candidate_specs.size());

  for (const auto& spec : candidate_specs) {
    StrategySweepCandidate candidate;
    candidate.order_strategy = spec.order_strategy;
    candidate.discrete_hilbert_encoding = spec.discrete_hilbert_encoding;
    candidate.hilbert_transform = spec.hilbert_transform;
    candidate.order_bits = spec.order_bits;
    candidate.split_strategy = spec.split_strategy;
    candidate.sweep_mode = spec.sweep_mode;
    candidate.strided_sweep_count = spec.strided_sweep_count;

    const auto config = BuildArm9ExperimentConfig(spec);
    candidate.note = BuildSweepNote(loads.size(), config);
    (void) EvaluateStrategySweepCandidate(loads, config, candidate);
    record.candidates.push_back(std::move(candidate));
  }

  record.selected_index = ResolveSelectedIndex(record.candidates);
  return record;
}

auto BuildOrderDiagnostic(const std::vector<icts::Pin*>& loads, std::size_t max_fanout, int max_diameter,
                          icts::LinearOrderStrategy order_strategy, const ExperimentCaseRecord& real_arm9_case) -> OrderDiagnostic
{
  OrderDiagnostic diagnostic{.order_strategy = order_strategy};

  FanoutConfigSpec diagnostic_spec{
      .fanout_limit = max_fanout,
      .max_diameter = max_diameter,
      .enable_exact_cap = true,
      .order_strategy = order_strategy,
      .split_strategy = icts::LinearSplitStrategy::kForwardGreedy,
      .sweep_mode = icts::LinearSweepMode::kPrefixSweep,
      .strided_sweep_count = 0U,
  };
  for (const auto candidate_index : CollectRankedCandidateIndices(real_arm9_case.candidates)) {
    const auto& candidate = real_arm9_case.candidates.at(candidate_index);
    if (candidate.order_strategy != order_strategy || !candidate.legal || candidate.empty_result) {
      continue;
    }
    diagnostic_spec.discrete_hilbert_encoding = candidate.discrete_hilbert_encoding;
    diagnostic_spec.hilbert_transform = candidate.hilbert_transform;
    diagnostic_spec.order_bits = candidate.order_bits;
    break;
  }

  const auto config = BuildArm9ExperimentConfig(diagnostic_spec);
  const auto ordered_loads = icts::LinearOrderGenerator::generateOrder(loads, config);
  if (ordered_loads.empty()) {
    return diagnostic;
  }

  std::vector<double> ring_steps;
  ring_steps.reserve(ordered_loads.size());
  for (std::size_t index = 0; index < ordered_loads.size(); ++index) {
    const auto& current = ordered_loads.at(index).location;
    const auto& next = ordered_loads.at((index + 1U) % ordered_loads.size()).location;
    const auto step = std::abs(current.get_x() - next.get_x()) + std::abs(current.get_y() - next.get_y());
    ring_steps.push_back(static_cast<double>(step));
  }

  diagnostic.avg_ring_step
      = ring_steps.empty() ? 0.0 : std::accumulate(ring_steps.begin(), ring_steps.end(), 0.0) / static_cast<double>(ring_steps.size());
  diagnostic.p95_ring_step = CalcPercentileValue(ring_steps, 0.95);
  diagnostic.max_ring_step = ring_steps.empty() ? 0.0 : *std::ranges::max_element(ring_steps);

  const auto window_size = std::max<std::size_t>(1U, std::min(max_fanout, ordered_loads.size()));
  std::vector<double> window_diameters;
  window_diameters.reserve(ordered_loads.size());
  std::vector<icts::Pin*> window_pins;
  window_pins.reserve(window_size);
  for (std::size_t start = 0; start < ordered_loads.size(); ++start) {
    window_pins.clear();
    for (std::size_t offset = 0; offset < window_size; ++offset) {
      auto* pin = ordered_loads.at((start + offset) % ordered_loads.size()).pin;
      if (pin != nullptr) {
        window_pins.push_back(pin);
      }
    }
    window_diameters.push_back(static_cast<double>(CalcClusterDiameter(window_pins)));
  }

  diagnostic.avg_window_diameter = window_diameters.empty() ? 0.0
                                                            : std::accumulate(window_diameters.begin(), window_diameters.end(), 0.0)
                                                                  / static_cast<double>(window_diameters.size());
  diagnostic.p95_window_diameter = CalcPercentileValue(window_diameters, 0.95);
  diagnostic.max_window_diameter = window_diameters.empty() ? 0.0 : *std::ranges::max_element(window_diameters);

  const auto ranked_indices = CollectRankedCandidateIndices(real_arm9_case.candidates);
  for (std::size_t rank = 0; rank < ranked_indices.size(); ++rank) {
    const auto candidate_index = ranked_indices.at(rank);
    const auto& candidate = real_arm9_case.candidates.at(candidate_index);
    if (candidate.order_strategy != order_strategy || !candidate.legal || candidate.empty_result) {
      continue;
    }
    diagnostic.best_real_arm9_rank = rank + 1U;
    diagnostic.best_real_arm9_score = candidate.selection_score;
    break;
  }
  return diagnostic;
}

}  // namespace icts_test::linear_clustering::realtech::experiment
