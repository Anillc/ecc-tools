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
 * @file LinearClusteringRealTechExperimentScenario.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-18
 * @brief Arm9-focused strategy ranking experiment for real-tech linear clustering.
 */

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Inst.hh"
#include "Log.hh"
#include "Pin.hh"
#include "Point.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "common/types/TestDataTypes.hh"
#include "database/config/Config.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/LinearOrderGenerator.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechShared.hh"

namespace icts_test::linear_clustering::realtech {

namespace {
using namespace detail;
using common::io::EmitInfoReport;
using common::io::WriteRawTextLog;
using common::logging::ScopedLogFile;
using common::realtech::EnsureRealTechSetup;
using common::realtech::RealTechMode;

constexpr std::string_view kArm9ExperimentTestName = "LinearClusteringRealTechExperimentTest.Arm9StrategyRankingExperiment";
constexpr std::string_view kArm9ExperimentTitle = "arm9_strategy_ranking_experiment";
constexpr unsigned kRepresentativeBenchmarkSeedBase = 20260418U;
constexpr std::size_t kRepresentativeCasesPerFamilyScale = 5U;
constexpr std::string_view kBenchmarkBatchCountEnv = "ICTS_LINEAR_CLUSTERING_BATCH_COUNT";
constexpr std::string_view kBenchmarkBatchIndexEnv = "ICTS_LINEAR_CLUSTERING_BATCH_INDEX";
constexpr std::string_view kSkipRealArm9Env = "ICTS_LINEAR_CLUSTERING_SKIP_REAL_ARM9";

struct StrategyAggregate
{
  std::string label;
  std::size_t legal_cases = 0;
  std::size_t wins = 0;
  std::size_t top3 = 0;
  double rank_sum = 0.0;
  double selection_score_sum = 0.0;
  double cluster_count_sum = 0.0;
  double singleton_cluster_sum = 0.0;
  double max_cluster_diameter_sum = 0.0;
  std::optional<std::size_t> real_arm9_rank = std::nullopt;
  std::optional<double> real_arm9_score = std::nullopt;
};

enum class SyntheticDistributionFamily
{
  kArm9Subset,
  kGaussianHotspots,
  kAxisBands,
  kDiagonalBand,
  kUniformSpread,
};

enum class SyntheticLoadScale
{
  kLoads500,
  kLoads1000,
  kLoads2000,
  kLoads5000,
};

enum class StrategyClass
{
  kContinuous,
  kDiscrete,
};

constexpr std::array<SyntheticDistributionFamily, 5> kSyntheticDistributionFamilies = {
    SyntheticDistributionFamily::kArm9Subset,   SyntheticDistributionFamily::kGaussianHotspots, SyntheticDistributionFamily::kAxisBands,
    SyntheticDistributionFamily::kDiagonalBand, SyntheticDistributionFamily::kUniformSpread,
};

constexpr std::array<SyntheticLoadScale, 4> kSyntheticLoadScales = {
    SyntheticLoadScale::kLoads500,
    SyntheticLoadScale::kLoads1000,
    SyntheticLoadScale::kLoads2000,
    SyntheticLoadScale::kLoads5000,
};

struct ExperimentCaseRecord
{
  std::string case_name;
  std::string case_kind;
  std::string distribution_family = "-";
  std::string load_scale = "-";
  std::size_t case_index = 0;
  std::size_t load_count = 0;
  int bounding_box_diameter = 0;
  double die_scale_x = 1.0;
  double die_scale_y = 1.0;
  std::vector<StrategySweepCandidate> candidates;
  std::optional<std::size_t> selected_index = std::nullopt;
};

struct SyntheticCaseSpec
{
  SyntheticDistributionFamily family = SyntheticDistributionFamily::kArm9Subset;
  SyntheticLoadScale load_scale = SyntheticLoadScale::kLoads1000;
  std::size_t family_instance_index = 0;
  std::size_t load_count = 0;
  double die_scale_x = 1.0;
  double die_scale_y = 1.0;
  double local_jitter_ratio = 0.0;
};

struct StrategyDescriptor
{
  std::string label;
  StrategyClass strategy_class = StrategyClass::kContinuous;
  icts::LinearOrderStrategy order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  icts::DiscreteHilbertEncoding discrete_hilbert_encoding = icts::DiscreteHilbertEncoding::kSinkThetaCell;
  icts::HilbertTransform hilbert_transform = icts::HilbertTransform::kIdentity;
  int order_bits = 16;
  icts::LinearSplitStrategy split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy;
  icts::LinearSweepMode sweep_mode = icts::LinearSweepMode::kPrefixSweep;
  std::size_t strided_sweep_count = 0;
};

struct BenchmarkStrategyTemplate
{
  StrategyClass strategy_class = StrategyClass::kContinuous;
  icts::LinearOrderStrategy order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  icts::DiscreteHilbertEncoding discrete_hilbert_encoding = icts::DiscreteHilbertEncoding::kSinkThetaCell;
  icts::HilbertTransform hilbert_transform = icts::HilbertTransform::kIdentity;
  int order_bits = 16;
  icts::LinearSplitStrategy split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy;
  icts::LinearSweepMode sweep_mode = icts::LinearSweepMode::kPrefixAndStridedSweep;
  std::size_t strided_sweep_count = kDefaultStridedSweepCount;
  std::string_view rationale = "";
};

struct HeadToHeadAggregate
{
  std::size_t cases = 0;
  std::size_t continuous_wins = 0;
  std::size_t discrete_wins = 0;
  std::size_t ties = 0;
  double score_gap_sum = 0.0;
  double relative_gap_sum = 0.0;
};

struct SourcePointTemplate
{
  const icts::Pin* pin = nullptr;
  double normalized_x = 0.5;
  double normalized_y = 0.5;
};

struct SourcePointCloud
{
  std::vector<SourcePointTemplate> points;
  int min_x = 0;
  int min_y = 0;
  int max_x = 0;
  int max_y = 0;
  double source_width = 1.0;
  double source_height = 1.0;
};

struct OrderDiagnostic
{
  icts::LinearOrderStrategy order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  std::optional<std::size_t> best_real_arm9_rank = std::nullopt;
  std::optional<double> best_real_arm9_score = std::nullopt;
  double avg_ring_step = 0.0;
  double p95_ring_step = 0.0;
  double max_ring_step = 0.0;
  double avg_window_diameter = 0.0;
  double p95_window_diameter = 0.0;
  double max_window_diameter = 0.0;
};

using AggregateMap = std::unordered_map<std::string, StrategyAggregate>;

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

auto CollectRankedCandidateIndices(const std::vector<StrategySweepCandidate>& candidates) -> std::vector<std::size_t>;

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

auto BuildSourcePointCloud(const RealClockLoads& real_clock_loads) -> SourcePointCloud
{
  SourcePointCloud source_cloud;
  if (!real_clock_loads.available || real_clock_loads.loads.empty()) {
    return source_cloud;
  }

  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
  for (const auto* pin : real_clock_loads.loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    min_x = std::min(min_x, location.get_x());
    min_y = std::min(min_y, location.get_y());
    max_x = std::max(max_x, location.get_x());
    max_y = std::max(max_y, location.get_y());
  }
  if (min_x > max_x || min_y > max_y) {
    return source_cloud;
  }

  source_cloud.min_x = min_x;
  source_cloud.min_y = min_y;
  source_cloud.max_x = max_x;
  source_cloud.max_y = max_y;
  source_cloud.source_width = std::max(1, max_x - min_x);
  source_cloud.source_height = std::max(1, max_y - min_y);
  source_cloud.points.reserve(real_clock_loads.loads.size());
  for (const auto* pin : real_clock_loads.loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    source_cloud.points.push_back(SourcePointTemplate{
        .pin = pin,
        .normalized_x = source_cloud.source_width <= 1.0 ? 0.5 : static_cast<double>(location.get_x() - min_x) / source_cloud.source_width,
        .normalized_y
        = source_cloud.source_height <= 1.0 ? 0.5 : static_cast<double>(location.get_y() - min_y) / source_cloud.source_height,
    });
  }
  return source_cloud;
}

auto ResolveSyntheticLoadCount(SyntheticLoadScale load_scale) -> std::size_t
{
  switch (load_scale) {
    case SyntheticLoadScale::kLoads500:
      return 500U;
    case SyntheticLoadScale::kLoads1000:
      return 1000U;
    case SyntheticLoadScale::kLoads2000:
      return 2000U;
    case SyntheticLoadScale::kLoads5000:
      return 5000U;
  }
  return 1000U;
}

auto SampleSyntheticDieScale(std::mt19937& generator, std::size_t real_load_count, SyntheticDistributionFamily family,
                             std::size_t synthetic_load_count) -> std::pair<double, double>
{
  const double load_ratio = static_cast<double>(std::max<std::size_t>(1U, synthetic_load_count))
                            / static_cast<double>(std::max<std::size_t>(1U, real_load_count));
  const double base_linear_scale = std::sqrt(std::max(load_ratio, 1.0e-6));

  std::pair<double, double> density_range = {0.94, 1.06};
  std::pair<double, double> aspect_range = {0.88, 1.14};
  switch (family) {
    case SyntheticDistributionFamily::kArm9Subset:
      density_range = {0.96, 1.04};
      aspect_range = {0.92, 1.10};
      break;
    case SyntheticDistributionFamily::kGaussianHotspots:
      density_range = {0.88, 1.00};
      aspect_range = {0.84, 1.18};
      break;
    case SyntheticDistributionFamily::kAxisBands:
      density_range = {0.92, 1.06};
      aspect_range = {0.68, 1.48};
      break;
    case SyntheticDistributionFamily::kDiagonalBand:
      density_range = {0.93, 1.05};
      aspect_range = {0.74, 1.34};
      break;
    case SyntheticDistributionFamily::kUniformSpread:
      density_range = {1.00, 1.14};
      aspect_range = {0.90, 1.12};
      break;
  }

  std::uniform_real_distribution<double> density_distribution(density_range.first, density_range.second);
  std::uniform_real_distribution<double> aspect_distribution(aspect_range.first, aspect_range.second);
  const double density_scale = density_distribution(generator);
  const double aspect_scale = aspect_distribution(generator);
  const double final_linear_scale = base_linear_scale * density_scale;
  return {
      std::clamp(final_linear_scale * aspect_scale, 0.55, 2.80),
      std::clamp(final_linear_scale / aspect_scale, 0.55, 2.80),
  };
}

auto SampleJitterRatio(std::mt19937& generator, SyntheticDistributionFamily family, std::size_t synthetic_load_count) -> double
{
  std::pair<double, double> jitter_range = {0.002, 0.010};
  switch (family) {
    case SyntheticDistributionFamily::kArm9Subset:
      jitter_range = {0.002, 0.010};
      break;
    case SyntheticDistributionFamily::kGaussianHotspots:
      jitter_range = {0.001, 0.006};
      break;
    case SyntheticDistributionFamily::kAxisBands:
    case SyntheticDistributionFamily::kDiagonalBand:
      jitter_range = {0.001, 0.007};
      break;
    case SyntheticDistributionFamily::kUniformSpread:
      jitter_range = {0.001, 0.004};
      break;
  }

  double scale_bonus = 0.0;
  if (synthetic_load_count >= 5000U) {
    scale_bonus = 0.0020;
  } else if (synthetic_load_count >= 2000U) {
    scale_bonus = 0.0010;
  }
  std::uniform_real_distribution<double> jitter_distribution(jitter_range.first, jitter_range.second + scale_bonus);
  return jitter_distribution(generator);
}

auto BuildRepresentativeCaseSpecs(std::size_t real_load_count) -> std::vector<SyntheticCaseSpec>
{
  std::vector<SyntheticCaseSpec> specs;
  specs.reserve(kSyntheticDistributionFamilies.size() * kSyntheticLoadScales.size() * kRepresentativeCasesPerFamilyScale);
  // NOLINTNEXTLINE(bugprone-random-generator-seed)
  std::mt19937 generator(kRepresentativeBenchmarkSeedBase);

  for (const auto family : kSyntheticDistributionFamilies) {
    for (const auto load_scale : kSyntheticLoadScales) {
      const auto load_count = ResolveSyntheticLoadCount(load_scale);
      for (std::size_t family_instance_index = 0; family_instance_index < kRepresentativeCasesPerFamilyScale; ++family_instance_index) {
        const auto [die_scale_x, die_scale_y] = SampleSyntheticDieScale(generator, real_load_count, family, load_count);
        specs.push_back(SyntheticCaseSpec{
            .family = family,
            .load_scale = load_scale,
            .family_instance_index = family_instance_index,
            .load_count = load_count,
            .die_scale_x = die_scale_x,
            .die_scale_y = die_scale_y,
            .local_jitter_ratio = SampleJitterRatio(generator, family, load_count),
        });
      }
    }
  }

  return specs;
}

auto GenerateArm9SubsetPoints(const SourcePointCloud& source_cloud, std::size_t count, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (source_cloud.points.empty() || count == 0U) {
    return points;
  }

  std::vector<std::size_t> indices(source_cloud.points.size());
  std::iota(indices.begin(), indices.end(), 0U);
  std::shuffle(indices.begin(), indices.end(), generator);

  points.reserve(count);
  const auto direct_count = std::min(count, indices.size());
  for (std::size_t index = 0; index < direct_count; ++index) {
    const auto& source_point = source_cloud.points.at(indices.at(index));
    points.emplace_back(source_point.normalized_x, source_point.normalized_y);
  }

  if (count > direct_count) {
    std::uniform_int_distribution<std::size_t> source_index_distribution(0U, source_cloud.points.size() - 1U);
    while (points.size() < count) {
      const auto& source_point = source_cloud.points.at(source_index_distribution(generator));
      points.emplace_back(source_point.normalized_x, source_point.normalized_y);
    }
  }
  return points;
}

auto GenerateGaussianHotspotPoints(const SourcePointCloud& source_cloud, std::size_t count, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (source_cloud.points.empty() || count == 0U) {
    return points;
  }

  std::uniform_int_distribution<int> hotspot_count_distribution(2, 4);
  const auto hotspot_count = static_cast<std::size_t>(hotspot_count_distribution(generator));
  std::vector<std::size_t> indices(source_cloud.points.size());
  std::iota(indices.begin(), indices.end(), 0U);
  std::shuffle(indices.begin(), indices.end(), generator);

  std::vector<std::pair<double, double>> centers;
  std::vector<double> weights;
  centers.reserve(hotspot_count);
  weights.reserve(hotspot_count);
  std::uniform_real_distribution<double> weight_distribution(0.2, 1.0);
  for (std::size_t hotspot_index = 0; hotspot_index < hotspot_count; ++hotspot_index) {
    const auto& source_point = source_cloud.points.at(indices.at(hotspot_index % indices.size()));
    centers.emplace_back(source_point.normalized_x, source_point.normalized_y);
    weights.push_back(weight_distribution(generator));
  }

  std::discrete_distribution<std::size_t> hotspot_distribution(weights.begin(), weights.end());
  std::uniform_real_distribution<double> sigma_distribution(0.035, 0.10);
  const double sigma_x = sigma_distribution(generator);
  const double sigma_y = sigma_distribution(generator);

  points.reserve(count);
  for (std::size_t point_index = 0; point_index < count; ++point_index) {
    const auto& center = centers.at(hotspot_distribution(generator));
    std::normal_distribution<double> x_distribution(center.first, sigma_x);
    std::normal_distribution<double> y_distribution(center.second, sigma_y);
    points.emplace_back(ClampUnit(x_distribution(generator)), ClampUnit(y_distribution(generator)));
  }
  return points;
}

auto GenerateAxisBandPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (count == 0U) {
    return points;
  }

  std::bernoulli_distribution vertical_distribution(0.5);
  const bool vertical_bands = vertical_distribution(generator);
  std::uniform_int_distribution<int> band_count_distribution(2, 4);
  const auto band_count = static_cast<std::size_t>(band_count_distribution(generator));
  std::uniform_real_distribution<double> center_distribution(0.12, 0.88);
  std::uniform_real_distribution<double> width_distribution(0.025, 0.08);

  std::vector<double> band_centers;
  band_centers.reserve(band_count);
  for (std::size_t band_index = 0; band_index < band_count; ++band_index) {
    band_centers.push_back(center_distribution(generator));
  }

  std::vector<double> band_weights(band_count, 1.0);
  std::discrete_distribution<std::size_t> band_distribution(band_weights.begin(), band_weights.end());
  const double band_width = width_distribution(generator);
  std::uniform_real_distribution<double> secondary_distribution(0.0, 1.0);

  points.reserve(count);
  for (std::size_t point_index = 0; point_index < count; ++point_index) {
    const auto band_center = band_centers.at(band_distribution(generator));
    std::normal_distribution<double> primary_distribution(band_center, band_width);
    const auto primary = ClampUnit(primary_distribution(generator));
    const auto secondary = ClampUnit(secondary_distribution(generator));
    points.emplace_back(vertical_bands ? primary : secondary, vertical_bands ? secondary : primary);
  }
  return points;
}

auto GenerateDiagonalBandPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (count == 0U) {
    return points;
  }

  std::bernoulli_distribution anti_diagonal_distribution(0.5);
  const bool anti_diagonal = anti_diagonal_distribution(generator);
  std::uniform_real_distribution<double> position_distribution(0.0, 1.0);
  std::normal_distribution<double> parallel_distribution(0.0, 0.02);
  std::uniform_real_distribution<double> width_distribution(0.035, 0.10);
  std::normal_distribution<double> cross_distribution(0.0, width_distribution(generator));

  points.reserve(count);
  for (std::size_t point_index = 0; point_index < count; ++point_index) {
    const double u = position_distribution(generator) + parallel_distribution(generator);
    const double v = cross_distribution(generator);
    if (anti_diagonal) {
      points.emplace_back(ClampUnit(u + v), ClampUnit(1.0 - u + v));
    } else {
      points.emplace_back(ClampUnit(u + v), ClampUnit(u - v));
    }
  }
  return points;
}

auto GenerateUniformSpreadPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (count == 0U) {
    return points;
  }

  const auto grid_side = std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
  std::vector<std::size_t> cell_indices(grid_side * grid_side);
  std::iota(cell_indices.begin(), cell_indices.end(), 0U);
  std::shuffle(cell_indices.begin(), cell_indices.end(), generator);
  cell_indices.resize(count);

  std::uniform_real_distribution<double> unit_distribution(0.0, 1.0);
  points.reserve(count);
  for (const auto cell_index : cell_indices) {
    const auto row = cell_index / grid_side;
    const auto column = cell_index % grid_side;
    const double x = (static_cast<double>(column) + unit_distribution(generator)) / static_cast<double>(grid_side);
    const double y = (static_cast<double>(row) + unit_distribution(generator)) / static_cast<double>(grid_side);
    points.emplace_back(ClampUnit(x), ClampUnit(y));
  }
  return points;
}

auto GenerateRepresentativeNormalizedPoints(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>
{
  switch (spec.family) {
    case SyntheticDistributionFamily::kArm9Subset:
      return GenerateArm9SubsetPoints(source_cloud, spec.load_count, generator);
    case SyntheticDistributionFamily::kGaussianHotspots:
      return GenerateGaussianHotspotPoints(source_cloud, spec.load_count, generator);
    case SyntheticDistributionFamily::kAxisBands:
      return GenerateAxisBandPoints(spec.load_count, generator);
    case SyntheticDistributionFamily::kDiagonalBand:
      return GenerateDiagonalBandPoints(spec.load_count, generator);
    case SyntheticDistributionFamily::kUniformSpread:
      return GenerateUniformSpreadPoints(spec.load_count, generator);
  }
  return {};
}

auto BuildPinsFromNormalizedPoints(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec,
                                   const std::vector<std::pair<double, double>>& normalized_points, unsigned seed) -> GeneratedPins
{
  GeneratedPins generated;
  if (source_cloud.points.empty() || normalized_points.empty()) {
    return generated;
  }

  const double target_width = std::max(1.0, source_cloud.source_width * spec.die_scale_x);
  const double target_height = std::max(1.0, source_cloud.source_height * spec.die_scale_y);
  const double margin_x = std::max(8.0, source_cloud.source_width * 0.01);
  const double margin_y = std::max(8.0, source_cloud.source_height * 0.01);
  const double usable_width = std::max(1.0, target_width - (2.0 * margin_x));
  const double usable_height = std::max(1.0, target_height - (2.0 * margin_y));

  std::mt19937 generator(seed ^ 0x9e3779b9U);
  std::uniform_real_distribution<double> jitter_x_distribution(-(source_cloud.source_width * spec.local_jitter_ratio),
                                                               source_cloud.source_width * spec.local_jitter_ratio);
  std::uniform_real_distribution<double> jitter_y_distribution(-(source_cloud.source_height * spec.local_jitter_ratio),
                                                               source_cloud.source_height * spec.local_jitter_ratio);

  std::vector<std::size_t> template_indices(source_cloud.points.size());
  std::iota(template_indices.begin(), template_indices.end(), 0U);
  std::shuffle(template_indices.begin(), template_indices.end(), generator);

  generated.storage.reserve(normalized_points.size());
  generated.loads.reserve(normalized_points.size());
  for (std::size_t point_index = 0; point_index < normalized_points.size(); ++point_index) {
    const auto& normalized_point = normalized_points.at(point_index);
    const auto& source_point = source_cloud.points.at(template_indices.at(point_index % template_indices.size()));
    const auto* source_pin = source_point.pin;
    if (source_pin == nullptr) {
      continue;
    }

    const double jitter_x = jitter_x_distribution(generator);
    const double jitter_y = jitter_y_distribution(generator);
    const int mapped_x
        = ClampToIntRange(std::clamp(margin_x + (ClampUnit(normalized_point.first) * usable_width) + jitter_x, 0.0, target_width));
    const int mapped_y
        = ClampToIntRange(std::clamp(margin_y + (ClampUnit(normalized_point.second) * usable_height) + jitter_y, 0.0, target_height));

    const bool keep_exact_cap_context = HasExactCapContext(source_pin);
    const auto pin_name = keep_exact_cap_context ? std::string(source_pin->get_name())
                                                 : std::string(source_pin->get_name()) + "__" + SyntheticDistributionFamilyName(spec.family)
                                                       + "_" + std::to_string(point_index);
    auto synthetic_pin = std::make_unique<icts::Pin>(pin_name, source_pin->get_type(), icts::Point<int>(mapped_x, mapped_y),
                                                     keep_exact_cap_context ? source_pin->get_inst() : nullptr,
                                                     keep_exact_cap_context ? source_pin->get_net() : nullptr, source_pin->is_io());
    generated.loads.push_back(synthetic_pin.get());
    generated.storage.push_back(std::move(synthetic_pin));
  }

  generated.width = ClampToIntRange(target_width);
  generated.height = ClampToIntRange(target_height);
  return generated;
}

auto BuildRepresentativePins(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec, unsigned seed) -> GeneratedPins
{
  std::mt19937 generator(seed);
  const auto normalized_points = GenerateRepresentativeNormalizedPoints(source_cloud, spec, generator);
  return BuildPinsFromNormalizedPoints(source_cloud, spec, normalized_points, seed);
}

auto BuildCandidateTableLines(const std::vector<StrategySweepCandidate>& candidates, std::size_t limit) -> std::vector<std::string>
{
  std::vector<std::string> lines;
  lines.emplace_back(
      "rank  strategy                                                                                                     legal  score     "
      "   clusters  singleton  max_diameter  note");

  const auto ranked_indices = CollectRankedCandidateIndices(candidates);
  const auto row_count = std::min(limit, ranked_indices.size());
  for (std::size_t rank = 0; rank < row_count; ++rank) {
    const auto& candidate = candidates.at(ranked_indices.at(rank));
    std::ostringstream line;
    line.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    line << std::setprecision(2);
    line << std::setw(4) << (rank + 1U) << "  " << std::left << std::setw(110) << BuildStrategyLabel(candidate) << std::right << "  "
         << std::setw(5) << ((candidate.legal && !candidate.empty_result) ? "yes" : "no") << "  " << std::setw(11)
         << candidate.selection_score << "  " << std::setw(8) << candidate.metrics.cluster_count << "  " << std::setw(9)
         << candidate.metrics.singleton_cluster_count << "  " << std::setw(12) << candidate.metrics.max_cluster_diameter << "  "
         << (candidate.note.empty() ? "-" : candidate.note);
    lines.push_back(line.str());
  }
  return lines;
}

auto BuildAggregateTableLines(const std::vector<StrategyAggregate>& aggregates, std::size_t total_synthetic_cases)
    -> std::vector<std::string>
{
  std::vector<std::size_t> ranked_indices(aggregates.size());
  std::iota(ranked_indices.begin(), ranked_indices.end(), 0U);
  std::ranges::sort(ranked_indices, [&aggregates](std::size_t lhs, std::size_t rhs) -> bool {
    const auto& lhs_value = aggregates.at(lhs);
    const auto& rhs_value = aggregates.at(rhs);
    const double lhs_avg_rank = lhs_value.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                            : lhs_value.rank_sum / static_cast<double>(lhs_value.legal_cases);
    const double rhs_avg_rank = rhs_value.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                            : rhs_value.rank_sum / static_cast<double>(rhs_value.legal_cases);
    if (lhs_avg_rank != rhs_avg_rank) {
      return lhs_avg_rank < rhs_avg_rank;
    }
    if (lhs_value.wins != rhs_value.wins) {
      return lhs_value.wins > rhs_value.wins;
    }
    if (lhs_value.legal_cases != rhs_value.legal_cases) {
      return lhs_value.legal_cases > rhs_value.legal_cases;
    }
    return lhs_value.label < rhs_value.label;
  });

  std::vector<std::string> lines;
  lines.emplace_back(
      "rank  strategy                                                                                                     legal_cases  "
      "wins  top3  avg_rank  avg_score   avg_clusters  avg_singleton  avg_max_diameter  real_rank");
  for (std::size_t display_rank = 0; display_rank < ranked_indices.size(); ++display_rank) {
    const auto& aggregate = aggregates.at(ranked_indices.at(display_rank));
    const double avg_rank = aggregate.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                        : aggregate.rank_sum / static_cast<double>(aggregate.legal_cases);
    const double avg_score = aggregate.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                         : aggregate.selection_score_sum / static_cast<double>(aggregate.legal_cases);
    const double avg_clusters
        = aggregate.legal_cases == 0U ? 0.0 : aggregate.cluster_count_sum / static_cast<double>(aggregate.legal_cases);
    const double avg_singletons
        = aggregate.legal_cases == 0U ? 0.0 : aggregate.singleton_cluster_sum / static_cast<double>(aggregate.legal_cases);
    const double avg_max_diameter
        = aggregate.legal_cases == 0U ? 0.0 : aggregate.max_cluster_diameter_sum / static_cast<double>(aggregate.legal_cases);

    std::ostringstream line;
    line.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    line << std::setprecision(2);
    line << std::setw(4) << (display_rank + 1U) << "  " << std::left << std::setw(110) << aggregate.label << std::right << "  "
         << std::setw(5) << aggregate.legal_cases << "/" << std::setw(3) << total_synthetic_cases << "  " << std::setw(4) << aggregate.wins
         << "  " << std::setw(4) << aggregate.top3 << "  " << std::setw(8) << avg_rank << "  " << std::setw(10) << avg_score << "  "
         << std::setw(12) << avg_clusters << "  " << std::setw(13) << avg_singletons << "  " << std::setw(16) << avg_max_diameter << "  "
         << (aggregate.real_arm9_rank.has_value() ? std::to_string(aggregate.real_arm9_rank.value()) : std::string("-"));
    lines.push_back(line.str());
  }
  return lines;
}

auto BuildCaseCsvHeader() -> std::string
{
  return "case_kind,distribution_family,load_scale,case_index,case_name,load_count,bounding_box_diameter,die_scale_x,die_scale_y,strategy_"
         "label,order_strategy,"
         "discrete_hilbert_encoding,hilbert_transform,order_bits,rank,is_selected,legal,empty,selection_score,cluster_count,"
         "singleton_cluster_count,max_cluster_diameter,note\n";
}

auto AppendCaseCsv(std::ostringstream& output_stream, const ExperimentCaseRecord& record) -> void
{
  const auto ranked_indices = CollectRankedCandidateIndices(record.candidates);
  for (std::size_t rank = 0; rank < ranked_indices.size(); ++rank) {
    const auto candidate_index = ranked_indices.at(rank);
    const auto& candidate = record.candidates.at(candidate_index);
    output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    output_stream << std::setprecision(4);
    output_stream << record.case_kind << "," << record.distribution_family << "," << record.load_scale << "," << record.case_index << ","
                  << record.case_name << "," << record.load_count << "," << record.bounding_box_diameter << "," << record.die_scale_x << ","
                  << record.die_scale_y << "," << BuildStrategyLabel(candidate) << "," << OrderStrategyName(candidate.order_strategy) << ","
                  << (IsDiscreteOrderStrategy(candidate.order_strategy) ? DiscreteHilbertEncodingName(candidate.discrete_hilbert_encoding)
                                                                        : "-")
                  << "," << (IsDiscreteOrderStrategy(candidate.order_strategy) ? HilbertTransformName(candidate.hilbert_transform) : "-")
                  << "," << (IsDiscreteOrderStrategy(candidate.order_strategy) ? candidate.order_bits : 0) << "," << (rank + 1U) << ","
                  << ((record.selected_index.has_value() && record.selected_index.value() == candidate_index) ? "true" : "false") << ","
                  << (candidate.legal ? "true" : "false") << "," << (candidate.empty_result ? "true" : "false") << ","
                  << candidate.selection_score << "," << candidate.metrics.cluster_count << "," << candidate.metrics.singleton_cluster_count
                  << "," << candidate.metrics.max_cluster_diameter << ",";
    if (!candidate.note.empty()) {
      output_stream << '"' << candidate.note << '"';
    }
    output_stream << "\n";
  }
}

auto BuildAggregateCsvHeader() -> std::string
{
  return "strategy_label,legal_cases,wins,top3,avg_rank,avg_score,avg_cluster_count,avg_singleton_cluster_count,avg_max_cluster_diameter,"
         "real_arm9_rank,real_arm9_score\n";
}

auto BuildOrderDiagnosticCsvHeader() -> std::string
{
  return "order_strategy,best_real_arm9_rank,best_real_arm9_score,avg_ring_step,p95_ring_step,max_ring_step,avg_window_diameter,"
         "p95_window_diameter,max_window_diameter\n";
}

auto AppendAggregateCsv(std::ostringstream& output_stream, const StrategyAggregate& aggregate) -> void
{
  const double avg_rank = aggregate.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                      : aggregate.rank_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_score = aggregate.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                       : aggregate.selection_score_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_clusters = aggregate.legal_cases == 0U ? 0.0 : aggregate.cluster_count_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_singletons
      = aggregate.legal_cases == 0U ? 0.0 : aggregate.singleton_cluster_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_max_diameter
      = aggregate.legal_cases == 0U ? 0.0 : aggregate.max_cluster_diameter_sum / static_cast<double>(aggregate.legal_cases);
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(4);
  output_stream << aggregate.label << "," << aggregate.legal_cases << "," << aggregate.wins << "," << aggregate.top3 << "," << avg_rank
                << "," << avg_score << "," << avg_clusters << "," << avg_singletons << "," << avg_max_diameter << ","
                << (aggregate.real_arm9_rank.has_value() ? std::to_string(aggregate.real_arm9_rank.value()) : std::string()) << ","
                << (aggregate.real_arm9_score.has_value() ? std::to_string(*aggregate.real_arm9_score) : std::string()) << "\n";
}

auto AppendOrderDiagnosticCsv(std::ostringstream& output_stream, const OrderDiagnostic& diagnostic) -> void
{
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(4);
  output_stream << OrderStrategyName(diagnostic.order_strategy) << ","
                << (diagnostic.best_real_arm9_rank.has_value() ? std::to_string(*diagnostic.best_real_arm9_rank) : std::string()) << ","
                << (diagnostic.best_real_arm9_score.has_value() ? std::to_string(*diagnostic.best_real_arm9_score) : std::string()) << ","
                << diagnostic.avg_ring_step << "," << diagnostic.p95_ring_step << "," << diagnostic.max_ring_step << ","
                << diagnostic.avg_window_diameter << "," << diagnostic.p95_window_diameter << "," << diagnostic.max_window_diameter << "\n";
}

auto MaterializeAggregateVector(const AggregateMap& aggregate_map) -> std::vector<StrategyAggregate>
{
  std::vector<StrategyAggregate> aggregates;
  aggregates.reserve(aggregate_map.size());
  for (const auto& [label, aggregate] : aggregate_map) {
    (void) label;
    aggregates.push_back(aggregate);
  }
  std::ranges::sort(aggregates, [](const StrategyAggregate& lhs, const StrategyAggregate& rhs) -> bool { return lhs.label < rhs.label; });
  return aggregates;
}

auto BuildCaseMixTableLines(const std::vector<SyntheticCaseSpec>& specs) -> std::vector<std::string>
{
  struct CaseMixRow
  {
    std::size_t total = 0;
    std::size_t load_500 = 0;
    std::size_t load_1000 = 0;
    std::size_t load_2000 = 0;
    std::size_t load_5000 = 0;
    std::size_t min_load = std::numeric_limits<std::size_t>::max();
    std::size_t max_load = 0;
    double load_sum = 0.0;
  };

  std::unordered_map<std::string, CaseMixRow> rows;
  for (const auto& spec : specs) {
    auto& row = rows[std::string(SyntheticDistributionFamilyName(spec.family))];
    ++row.total;
    switch (spec.load_scale) {
      case SyntheticLoadScale::kLoads500:
        ++row.load_500;
        break;
      case SyntheticLoadScale::kLoads1000:
        ++row.load_1000;
        break;
      case SyntheticLoadScale::kLoads2000:
        ++row.load_2000;
        break;
      case SyntheticLoadScale::kLoads5000:
        ++row.load_5000;
        break;
    }
    row.min_load = std::min(row.min_load, spec.load_count);
    row.max_load = std::max(row.max_load, spec.load_count);
    row.load_sum += static_cast<double>(spec.load_count);
  }

  std::vector<std::string> family_names;
  family_names.reserve(rows.size());
  for (const auto& [family_name, row] : rows) {
    (void) row;
    family_names.push_back(family_name);
  }
  std::ranges::sort(family_names);

  std::vector<std::string> lines;
  lines.emplace_back("family                total   500  1000  2000  5000  min_load  max_load  avg_load");
  for (const auto& family_name : family_names) {
    const auto& row = rows.at(family_name);
    std::ostringstream line;
    line.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    line << std::setprecision(2);
    line << std::left << std::setw(20) << family_name << std::right << "  " << std::setw(5) << row.total << "  " << std::setw(4)
         << row.load_500 << "  " << std::setw(5) << row.load_1000 << "  " << std::setw(5) << row.load_2000 << "  " << std::setw(5)
         << row.load_5000 << "  " << std::setw(8) << (row.total == 0U ? 0U : row.min_load) << "  " << std::setw(8) << row.max_load << "  "
         << std::setw(8) << (row.total == 0U ? 0.0 : row.load_sum / static_cast<double>(row.total));
    lines.push_back(line.str());
  }
  return lines;
}

auto BuildNamedRankingSnapshotLines(const std::unordered_map<std::string, AggregateMap>& grouped_aggregates,
                                    const std::unordered_map<std::string, std::size_t>& group_case_counts, std::size_t top_n)
    -> std::vector<std::string>
{
  std::vector<std::string> group_names;
  group_names.reserve(grouped_aggregates.size());
  for (const auto& [group_name, aggregates] : grouped_aggregates) {
    (void) aggregates;
    group_names.push_back(group_name);
  }
  std::ranges::sort(group_names);

  std::vector<std::string> lines;
  for (const auto& group_name : group_names) {
    const auto aggregate_it = grouped_aggregates.find(group_name);
    const auto case_count_it = group_case_counts.find(group_name);
    if (aggregate_it == grouped_aggregates.end() || case_count_it == group_case_counts.end()) {
      continue;
    }

    lines.push_back(group_name + " (" + std::to_string(case_count_it->second) + " cases)");
    const auto ranking_lines = BuildAggregateTableLines(MaterializeAggregateVector(aggregate_it->second), case_count_it->second);
    const auto line_limit = std::min<std::size_t>(ranking_lines.size(), top_n + 1U);
    for (std::size_t line_index = 0; line_index < line_limit; ++line_index) {
      lines.push_back(ranking_lines.at(line_index));
    }
    lines.emplace_back("");
  }
  return lines;
}

auto BuildNamedAggregateCsvHeader(std::string_view group_name_label) -> std::string
{
  return std::string(group_name_label)
         + ",strategy_label,legal_cases,wins,top3,avg_rank,avg_score,avg_cluster_count,avg_singleton_cluster_count,avg_max_cluster_diameter,"
           "real_arm9_rank,real_arm9_score\n";
}

auto AppendNamedAggregateCsv(std::ostringstream& output_stream, std::string_view group_name, const StrategyAggregate& aggregate) -> void
{
  const double avg_rank = aggregate.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                      : aggregate.rank_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_score = aggregate.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                       : aggregate.selection_score_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_clusters = aggregate.legal_cases == 0U ? 0.0 : aggregate.cluster_count_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_singletons
      = aggregate.legal_cases == 0U ? 0.0 : aggregate.singleton_cluster_sum / static_cast<double>(aggregate.legal_cases);
  const double avg_max_diameter
      = aggregate.legal_cases == 0U ? 0.0 : aggregate.max_cluster_diameter_sum / static_cast<double>(aggregate.legal_cases);
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(4);
  output_stream << group_name << "," << aggregate.label << "," << aggregate.legal_cases << "," << aggregate.wins << "," << aggregate.top3
                << "," << avg_rank << "," << avg_score << "," << avg_clusters << "," << avg_singletons << "," << avg_max_diameter << ","
                << (aggregate.real_arm9_rank.has_value() ? std::to_string(aggregate.real_arm9_rank.value()) : std::string()) << ","
                << (aggregate.real_arm9_score.has_value() ? std::to_string(*aggregate.real_arm9_score) : std::string()) << "\n";
}

auto BuildBenchmarkScopeLines(const std::vector<BenchmarkStrategyTemplate>& strategy_templates) -> std::vector<std::string>
{
  std::vector<std::string> lines;
  lines.emplace_back("final retained benchmark strategies (top4)");
  lines.emplace_back(
      "strategy                                                                                                     class       rationale");
  for (const auto& strategy_template : strategy_templates) {
    std::ostringstream line;
    const auto label = StrategyLabel(strategy_template.order_strategy, strategy_template.discrete_hilbert_encoding,
                                     strategy_template.hilbert_transform, strategy_template.order_bits, strategy_template.split_strategy,
                                     strategy_template.sweep_mode, strategy_template.strided_sweep_count);
    line << std::left << std::setw(110) << label << "  " << std::setw(10) << StrategyClassName(strategy_template.strategy_class) << "  "
         << strategy_template.rationale;
    lines.push_back(line.str());
  }
  lines.emplace_back("");
  lines.emplace_back("pruned scope");
  lines.emplace_back("item                                  reason");
  lines.emplace_back("interim extra strategies              removed: keep only final discrete top2 and continuous top2");
  lines.emplace_back("density-scaled order strategies       removed: no strategy from that class survived final retention");
  lines.emplace_back("sink-theta discrete encodings         removed: final discrete winners both use classic_index encodings");
  lines.emplace_back("prefix_sweep / strided_sweep          removed: final retained set uses prefix_and_strided_sweep only");
  lines.emplace_back("real-arm9 exhaustive pre-sweep        removed: benchmark evaluates retained top4 directly");
  return lines;
}

auto BuildRetainedStrategyAuditLines(const std::vector<StrategyAggregate>& overall_aggregates,
                                     const std::unordered_map<std::string, StrategyDescriptor>& descriptors) -> std::vector<std::string>
{
  std::vector<std::size_t> ranked_indices(overall_aggregates.size());
  std::iota(ranked_indices.begin(), ranked_indices.end(), 0U);
  std::ranges::sort(ranked_indices, [&overall_aggregates](std::size_t lhs, std::size_t rhs) -> bool {
    const auto& lhs_value = overall_aggregates.at(lhs);
    const auto& rhs_value = overall_aggregates.at(rhs);
    const double lhs_avg_rank = lhs_value.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                            : lhs_value.rank_sum / static_cast<double>(lhs_value.legal_cases);
    const double rhs_avg_rank = rhs_value.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                            : rhs_value.rank_sum / static_cast<double>(rhs_value.legal_cases);
    if (lhs_avg_rank != rhs_avg_rank) {
      return lhs_avg_rank < rhs_avg_rank;
    }
    return lhs_value.label < rhs_value.label;
  });

  std::vector<std::string> lines;
  lines.emplace_back(
      "rank  strategy                                                                                                     class       wins "
      " top3  avg_rank  verdict");
  for (std::size_t display_rank = 0; display_rank < ranked_indices.size(); ++display_rank) {
    const auto& aggregate = overall_aggregates.at(ranked_indices.at(display_rank));
    const auto descriptor_it = descriptors.find(aggregate.label);
    const auto strategy_class = descriptor_it == descriptors.end() ? StrategyClass::kContinuous : descriptor_it->second.strategy_class;
    const double avg_rank = aggregate.legal_cases == 0U ? std::numeric_limits<double>::infinity()
                                                        : aggregate.rank_sum / static_cast<double>(aggregate.legal_cases);
    std::string verdict = "competitive";
    if (aggregate.top3 == 0U) {
      verdict = "dormant";
    } else if (aggregate.wins == 0U) {
      verdict = "podium_only";
    }

    std::ostringstream line;
    line.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    line << std::setprecision(2);
    line << std::setw(4) << (display_rank + 1U) << "  " << std::left << std::setw(110) << aggregate.label << std::right << "  "
         << std::setw(10) << StrategyClassName(strategy_class) << "  " << std::setw(4) << aggregate.wins << "  " << std::setw(4)
         << aggregate.top3 << "  " << std::setw(8) << avg_rank << "  " << verdict;
    lines.push_back(line.str());
  }
  return lines;
}

auto MakeCaseOutputPath(const std::filesystem::path& output_dir, std::string_view file_name) -> std::filesystem::path
{
  return output_dir / std::string(file_name);
}

auto WriteExperimentArtifact(const std::filesystem::path& path, const std::string& content) -> void
{
  EXPECT_TRUE(WriteRawTextLog(path, content)) << "Failed to write artifact: " << path.string();
}

auto RecordAggregate(StrategyAggregate& aggregate, const StrategySweepCandidate& candidate, std::size_t rank) -> void
{
  if (!candidate.legal || candidate.empty_result) {
    return;
  }
  ++aggregate.legal_cases;
  aggregate.rank_sum += static_cast<double>(rank);
  aggregate.selection_score_sum += candidate.selection_score;
  aggregate.cluster_count_sum += static_cast<double>(candidate.metrics.cluster_count);
  aggregate.singleton_cluster_sum += static_cast<double>(candidate.metrics.singleton_cluster_count);
  aggregate.max_cluster_diameter_sum += static_cast<double>(candidate.metrics.max_cluster_diameter);
  if (rank == 1U) {
    ++aggregate.wins;
  }
  if (rank <= 3U) {
    ++aggregate.top3;
  }
}

auto RecordHeadToHead(HeadToHeadAggregate& aggregate, const ExperimentCaseRecord& record) -> void
{
  std::optional<double> best_continuous_score = std::nullopt;
  std::optional<double> best_discrete_score = std::nullopt;

  for (const auto& candidate : record.candidates) {
    if (!candidate.legal || candidate.empty_result) {
      continue;
    }

    auto& best_score = IsDiscreteOrderStrategy(candidate.order_strategy) ? best_discrete_score : best_continuous_score;
    if (!best_score.has_value() || candidate.selection_score < *best_score) {
      best_score = candidate.selection_score;
    }
  }

  if (!best_continuous_score.has_value() || !best_discrete_score.has_value()) {
    return;
  }

  ++aggregate.cases;
  const double score_gap = *best_discrete_score - *best_continuous_score;
  const double reference_score = std::max(1.0, std::min(*best_continuous_score, *best_discrete_score));
  aggregate.score_gap_sum += score_gap;
  aggregate.relative_gap_sum += score_gap / reference_score;
  if (score_gap > 0.0) {
    ++aggregate.continuous_wins;
  } else if (score_gap < 0.0) {
    ++aggregate.discrete_wins;
  } else {
    ++aggregate.ties;
  }
}

auto BuildHeadToHeadRow(std::string_view group_name, const HeadToHeadAggregate& aggregate) -> std::string
{
  std::ostringstream line;
  line.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  line << std::setprecision(2);
  const double avg_gap = aggregate.cases == 0U ? 0.0 : aggregate.score_gap_sum / static_cast<double>(aggregate.cases);
  const double avg_gap_percent = aggregate.cases == 0U ? 0.0 : (aggregate.relative_gap_sum / static_cast<double>(aggregate.cases)) * 100.0;
  line << std::left << std::setw(22) << group_name << std::right << "  " << std::setw(5) << aggregate.cases << "  " << std::setw(9)
       << aggregate.continuous_wins << "  " << std::setw(9) << aggregate.discrete_wins << "  " << std::setw(4) << aggregate.ties << "  "
       << std::setw(12) << avg_gap << "  " << std::setw(11) << avg_gap_percent;
  return line.str();
}

auto BuildNamedHeadToHeadLines(const std::unordered_map<std::string, HeadToHeadAggregate>& grouped_aggregates) -> std::vector<std::string>
{
  std::vector<std::string> group_names;
  group_names.reserve(grouped_aggregates.size());
  for (const auto& [group_name, aggregate] : grouped_aggregates) {
    (void) aggregate;
    group_names.push_back(group_name);
  }
  std::ranges::sort(group_names);

  std::vector<std::string> lines;
  lines.emplace_back("group                   cases  cont_wins  disc_wins  ties   avg_gap     avg_gap_pct");
  for (const auto& group_name : group_names) {
    const auto aggregate_it = grouped_aggregates.find(group_name);
    if (aggregate_it == grouped_aggregates.end()) {
      continue;
    }
    lines.push_back(BuildHeadToHeadRow(group_name, aggregate_it->second));
  }
  return lines;
}

auto BuildOrderDiagnosticTableLines(std::vector<OrderDiagnostic> diagnostics) -> std::vector<std::string>
{
  std::ranges::sort(diagnostics, [](const OrderDiagnostic& lhs, const OrderDiagnostic& rhs) -> bool {
    const auto lhs_rank = lhs.best_real_arm9_rank.value_or(std::numeric_limits<std::size_t>::max());
    const auto rhs_rank = rhs.best_real_arm9_rank.value_or(std::numeric_limits<std::size_t>::max());
    if (lhs_rank != rhs_rank) {
      return lhs_rank < rhs_rank;
    }
    return std::string(OrderStrategyName(lhs.order_strategy)) < std::string(OrderStrategyName(rhs.order_strategy));
  });

  std::vector<std::string> lines;
  lines.emplace_back(
      "order_strategy                        best_rank  best_score   avg_step   p95_step   max_step   avg_w32    p95_w32    max_w32");
  for (const auto& diagnostic : diagnostics) {
    std::ostringstream line;
    line.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    line << std::setprecision(2);
    line << std::left << std::setw(34) << OrderStrategyName(diagnostic.order_strategy) << std::right << "  " << std::setw(9)
         << (diagnostic.best_real_arm9_rank.has_value() ? std::to_string(*diagnostic.best_real_arm9_rank) : std::string("-")) << "  "
         << std::setw(11)
         << (diagnostic.best_real_arm9_score.has_value() ? *diagnostic.best_real_arm9_score : std::numeric_limits<double>::quiet_NaN())
         << "  " << std::setw(9) << diagnostic.avg_ring_step << "  " << std::setw(9) << diagnostic.p95_ring_step << "  " << std::setw(9)
         << diagnostic.max_ring_step << "  " << std::setw(9) << diagnostic.avg_window_diameter << "  " << std::setw(9)
         << diagnostic.p95_window_diameter << "  " << std::setw(9) << diagnostic.max_window_diameter;
    lines.push_back(line.str());
  }
  return lines;
}

auto BuildExperimentSummaryReport(
    const RealClockLoads& real_clock_loads, std::string_view batch_label, std::size_t synthetic_case_count,
    std::size_t retained_candidate_count, std::size_t max_fanout, double max_cap_pf, int routing_layer,
    const std::vector<std::string>& benchmark_scope_lines, const std::vector<std::string>& case_mix_lines,
    const std::vector<std::string>& overall_ranking_lines, const std::vector<std::string>& family_ranking_lines,
    const std::vector<std::string>& load_scale_ranking_lines, const std::vector<std::string>& retained_strategy_audit_lines,
    const std::vector<std::string>& synthetic_head_to_head_lines, const std::vector<std::string>& family_head_to_head_lines,
    const std::vector<std::string>& load_scale_head_to_head_lines, const std::vector<std::string>& real_arm9_lines,
    const std::vector<std::string>& real_arm9_head_to_head_lines, const std::vector<std::string>& order_diagnostic_lines,
    const std::vector<std::string>& artifact_names) -> std::string
{
  std::ostringstream report;
  report.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report << std::setprecision(4);
  report << "Test: " << kArm9ExperimentTestName << "\n";
  report << "Mode: real-tech arm9 strategy ranking experiment\n";
  report << "Setup: " << EnsureRealTechSetup().summary << "\n";
  report << "Clock: " << real_clock_loads.clock_name << "\n";
  report << "Benchmark batch: " << batch_label << "\n";
  report << "Real arm9 load_count: " << real_clock_loads.loads.size() << "\n";
  report << "Representative synthetic benchmark case_count: " << synthetic_case_count << "\n";
  report << "Experiment max_fanout: " << max_fanout << "\n";
  report << "Experiment max_cap_pf: " << max_cap_pf << "\n";
  report << "Experiment routing_layer: " << routing_layer << "\n";
  report << "Arm9 bounding_box_diameter: " << real_clock_loads.bounding_box_diameter << "\n";
  report << "Retained benchmark candidate_count: " << retained_candidate_count << "\n";
  report << "Benchmark scope\n";
  for (const auto& line : benchmark_scope_lines) {
    report << line << "\n";
  }
  report << "Representative benchmark case mix\n";
  for (const auto& line : case_mix_lines) {
    report << line << "\n";
  }
  report << "Overall ranking\n";
  for (const auto& line : overall_ranking_lines) {
    report << line << "\n";
  }
  report << "Distribution-family ranking snapshots\n";
  for (const auto& line : family_ranking_lines) {
    report << line << "\n";
  }
  report << "Load-scale ranking snapshots\n";
  for (const auto& line : load_scale_ranking_lines) {
    report << line << "\n";
  }
  report << "Retained strategy audit\n";
  for (const auto& line : retained_strategy_audit_lines) {
    report << line << "\n";
  }
  report << "Continuous-vs-discrete synthetic head-to-head\n";
  for (const auto& line : synthetic_head_to_head_lines) {
    report << line << "\n";
  }
  report << "Continuous-vs-discrete by distribution family\n";
  for (const auto& line : family_head_to_head_lines) {
    report << line << "\n";
  }
  report << "Continuous-vs-discrete by load scale\n";
  for (const auto& line : load_scale_head_to_head_lines) {
    report << line << "\n";
  }
  report << "Real arm9 ranking\n";
  for (const auto& line : real_arm9_lines) {
    report << line << "\n";
  }
  report << "Real arm9 continuous-vs-discrete head-to-head\n";
  for (const auto& line : real_arm9_head_to_head_lines) {
    report << line << "\n";
  }
  report << "Real arm9 order diagnostics\n";
  report << "Metric notes: step uses cyclic Manhattan distance between adjacent order positions; w32 uses cyclic window diameter for "
            "size=min(load_count,max_fanout).\n";
  for (const auto& line : order_diagnostic_lines) {
    report << line << "\n";
  }
  report << "Artifacts\n";
  for (const auto& artifact_name : artifact_names) {
    report << "- " << artifact_name << "\n";
  }
  return report.str();
}

}  // namespace

auto RunRealTechArm9StrategyRankingExperiment() -> void
{
  const auto batch_count = ReadEnvSizeValue(kBenchmarkBatchCountEnv).value_or(1U);
  const auto batch_index = ReadEnvSizeValue(kBenchmarkBatchIndexEnv).value_or(0U);
  const bool skip_real_arm9 = ReadEnvFlag(kSkipRealArm9Env);
  ASSERT_GT(batch_count, 0U) << "Benchmark batch count must be positive.";
  ASSERT_LT(batch_index, batch_count) << "Benchmark batch index must be smaller than batch count.";

  const std::string batch_label
      = batch_count > 1U ? (std::to_string(batch_index + 1U) + "/" + std::to_string(batch_count)) : std::string("full");
  std::string output_tag = std::string(kArm9ExperimentTitle) + "__retained_top4";
  if (batch_count > 1U) {
    output_tag += "__batch_" + std::to_string(batch_index + 1U) + "_of_" + std::to_string(batch_count);
  }
  if (skip_real_arm9) {
    output_tag += "__synthetic_only";
  }
  const auto& setup_state = EnsureRealTechSetup();
  const auto output_dir = PrepareOutputDir(output_tag);
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare arm9 experiment output dir.";

  const auto report_path = MakeCaseOutputPath(output_dir, "report.log");
  const auto cts_log_path = MakeCaseOutputPath(output_dir, "cts.log");
  if (setup_state.mode != RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    const std::string skipped_report = std::string("Test: ") + std::string(kArm9ExperimentTestName)
                                       + "\nMode: skipped\nReason: real-tech assets are unavailable.\n" + "Setup: " + setup_state.summary
                                       + "\n";
    ASSERT_TRUE(WriteRawTextLog(report_path, skipped_report));
    ScopedLogFile log_guard(cts_log_path, "Linear Clustering Arm9 Experiment");
    EmitInfoReport(InfoReport{.title = std::string(kArm9ExperimentTitle), .content = skipped_report});
    GTEST_SKIP() << "Real-tech assets are unavailable: " << setup_state.summary;
  }

  const auto& real_clock_loads = EnsureLargestRealClockLoads();
  ASSERT_TRUE(real_clock_loads.available) << "Real-tech setup succeeded but no CTS clock loads are available.";
  ASSERT_FALSE(real_clock_loads.loads.empty());
  const auto source_cloud = BuildSourcePointCloud(real_clock_loads);
  ASSERT_FALSE(source_cloud.points.empty()) << "Failed to derive normalized source point cloud from real arm9 loads.";
  const auto emit_progress = [](const std::string& message) -> void { LOG_INFO << message; };

  const std::size_t experiment_max_fanout = std::max<std::size_t>(1U, CONFIG_INST.get_max_fanout());
  const double experiment_max_cap = CONFIG_INST.has_max_cap() ? CONFIG_INST.get_max_cap() : std::numeric_limits<double>::infinity();
  const int experiment_routing_layer = SelectRoutingLayerFromConfig();
  const auto strategy_templates = BuildRetainedBenchmarkTemplates();
  emit_progress("[linear_clustering] arm9 experiment start: batch=" + batch_label + ", retained_candidates="
                + std::to_string(strategy_templates.size()) + ", skip_real_arm9=" + std::string(skip_real_arm9 ? "true" : "false"));

  const auto benchmark_template_specs
      = BuildBenchmarkSpecs(experiment_max_fanout, real_clock_loads.bounding_box_diameter, strategy_templates);
  ExperimentCaseRecord real_arm9_case;
  if (!skip_real_arm9) {
    emit_progress("[linear_clustering] evaluating real arm9 ranking");
    real_arm9_case = EvaluateExperimentCase(real_clock_loads.loads, benchmark_template_specs);
    real_arm9_case.case_name = "real_arm9";
    real_arm9_case.case_kind = "real_arm9";
    real_arm9_case.case_index = 0U;
    real_arm9_case.die_scale_x = 1.0;
    real_arm9_case.die_scale_y = 1.0;
    real_arm9_case.load_scale = "real_arm9";
    ASSERT_TRUE(real_arm9_case.selected_index.has_value()) << "Arm9 real-tech experiment selected no legal strategy.";
  }

  const auto strategy_descriptors = BuildStrategyDescriptors(strategy_templates);
  AggregateMap aggregate_map;
  for (const auto& [label, descriptor] : strategy_descriptors) {
    (void) descriptor;
    aggregate_map.emplace(label, StrategyAggregate{.label = label});
  }
  const auto aggregate_template = aggregate_map;

  if (!skip_real_arm9) {
    const auto real_arm9_ranked_indices = CollectRankedCandidateIndices(real_arm9_case.candidates);
    for (std::size_t rank = 0; rank < real_arm9_ranked_indices.size(); ++rank) {
      const auto candidate_index = real_arm9_ranked_indices.at(rank);
      const auto& candidate = real_arm9_case.candidates.at(candidate_index);
      auto& aggregate = aggregate_map.at(BuildStrategyLabel(candidate));
      if (candidate.legal && !candidate.empty_result) {
        aggregate.real_arm9_rank = rank + 1U;
        aggregate.real_arm9_score = candidate.selection_score;
      }
    }
  }

  std::ostringstream case_csv;
  case_csv << BuildCaseCsvHeader();
  if (!skip_real_arm9) {
    AppendCaseCsv(case_csv, real_arm9_case);
  }

  const auto all_synthetic_specs = BuildRepresentativeCaseSpecs(real_clock_loads.loads.size());
  std::vector<SyntheticCaseSpec> synthetic_specs;
  synthetic_specs.reserve((all_synthetic_specs.size() + batch_count - 1U) / batch_count);
  for (std::size_t spec_index = 0; spec_index < all_synthetic_specs.size(); ++spec_index) {
    if ((spec_index % batch_count) == batch_index) {
      synthetic_specs.push_back(all_synthetic_specs.at(spec_index));
    }
  }
  ASSERT_FALSE(synthetic_specs.empty()) << "No representative synthetic cases selected for benchmark batch " << batch_label;
  std::unordered_map<std::string, AggregateMap> family_aggregate_maps;
  std::unordered_map<std::string, AggregateMap> load_scale_aggregate_maps;
  std::unordered_map<std::string, std::size_t> family_case_counts;
  std::unordered_map<std::string, std::size_t> load_scale_case_counts;
  HeadToHeadAggregate synthetic_head_to_head;
  std::unordered_map<std::string, HeadToHeadAggregate> family_head_to_head_maps;
  std::unordered_map<std::string, HeadToHeadAggregate> load_scale_head_to_head_maps;
  for (std::size_t case_index = 0; case_index < synthetic_specs.size(); ++case_index) {
    const auto& spec = synthetic_specs.at(case_index);
    emit_progress("[linear_clustering] representative synthetic case " + std::to_string(case_index + 1U) + "/"
                  + std::to_string(synthetic_specs.size()) + " family=" + SyntheticDistributionFamilyName(spec.family)
                  + " scale=" + SyntheticLoadScaleName(spec.load_scale) + " loads=" + std::to_string(spec.load_count));
    auto synthetic_case
        = BuildRepresentativePins(source_cloud, spec, kRepresentativeBenchmarkSeedBase + static_cast<unsigned>(case_index * 17U));
    ASSERT_FALSE(synthetic_case.loads.empty()) << "Failed to build representative synthetic case " << case_index;

    const auto benchmark_specs
        = BuildBenchmarkSpecs(experiment_max_fanout, CalcBoundingBoxDiameter(synthetic_case.loads), strategy_templates);
    ExperimentCaseRecord record = EvaluateExperimentCase(synthetic_case.loads, benchmark_specs);
    record.case_name = std::string(SyntheticDistributionFamilyName(spec.family)) + "_n" + SyntheticLoadScaleName(spec.load_scale) + "_"
                       + std::to_string(spec.family_instance_index + 1U);
    record.case_kind = "representative_synthetic";
    record.distribution_family = SyntheticDistributionFamilyName(spec.family);
    record.load_scale = SyntheticLoadScaleName(spec.load_scale);
    record.case_index = case_index + 1U;
    record.die_scale_x = spec.die_scale_x;
    record.die_scale_y = spec.die_scale_y;
    ASSERT_TRUE(record.selected_index.has_value()) << "Representative synthetic case selected no legal strategy: " << record.case_name;

    auto [family_it, family_inserted] = family_aggregate_maps.try_emplace(record.distribution_family, aggregate_template);
    auto [load_scale_it, load_scale_inserted] = load_scale_aggregate_maps.try_emplace(record.load_scale, aggregate_template);
    (void) family_inserted;
    (void) load_scale_inserted;
    ++family_case_counts[record.distribution_family];
    ++load_scale_case_counts[record.load_scale];

    const auto ranked_indices = CollectRankedCandidateIndices(record.candidates);
    for (std::size_t rank = 0; rank < ranked_indices.size(); ++rank) {
      const auto& candidate = record.candidates.at(ranked_indices.at(rank));
      RecordAggregate(aggregate_map.at(BuildStrategyLabel(candidate)), candidate, rank + 1U);
      RecordAggregate(family_it->second.at(BuildStrategyLabel(candidate)), candidate, rank + 1U);
      RecordAggregate(load_scale_it->second.at(BuildStrategyLabel(candidate)), candidate, rank + 1U);
    }
    RecordHeadToHead(synthetic_head_to_head, record);
    RecordHeadToHead(family_head_to_head_maps[record.distribution_family], record);
    RecordHeadToHead(load_scale_head_to_head_maps[record.load_scale], record);
    AppendCaseCsv(case_csv, record);
  }

  const auto aggregates = MaterializeAggregateVector(aggregate_map);

  std::ostringstream aggregate_csv;
  aggregate_csv << BuildAggregateCsvHeader();
  for (const auto& aggregate : aggregates) {
    AppendAggregateCsv(aggregate_csv, aggregate);
  }

  std::ostringstream distribution_aggregate_csv;
  distribution_aggregate_csv << BuildNamedAggregateCsvHeader("distribution_family");
  std::vector<std::string> distribution_names;
  distribution_names.reserve(family_aggregate_maps.size());
  for (const auto& [distribution_name, aggregate_values] : family_aggregate_maps) {
    (void) aggregate_values;
    distribution_names.push_back(distribution_name);
  }
  std::ranges::sort(distribution_names);
  for (const auto& distribution_name : distribution_names) {
    for (const auto& aggregate : MaterializeAggregateVector(family_aggregate_maps.at(distribution_name))) {
      AppendNamedAggregateCsv(distribution_aggregate_csv, distribution_name, aggregate);
    }
  }

  std::ostringstream load_scale_aggregate_csv;
  load_scale_aggregate_csv << BuildNamedAggregateCsvHeader("load_scale");
  std::vector<std::string> load_scale_names;
  load_scale_names.reserve(load_scale_aggregate_maps.size());
  for (const auto& [load_scale_name, aggregate_values] : load_scale_aggregate_maps) {
    (void) aggregate_values;
    load_scale_names.push_back(load_scale_name);
  }
  std::ranges::sort(load_scale_names);
  for (const auto& load_scale_name : load_scale_names) {
    for (const auto& aggregate : MaterializeAggregateVector(load_scale_aggregate_maps.at(load_scale_name))) {
      AppendNamedAggregateCsv(load_scale_aggregate_csv, load_scale_name, aggregate);
    }
  }

  std::vector<OrderDiagnostic> order_diagnostics;
  if (!skip_real_arm9) {
    std::vector<icts::LinearOrderStrategy> diagnostic_order_strategies;
    for (const auto& strategy_template : strategy_templates) {
      const auto already_added = std::ranges::find(diagnostic_order_strategies, strategy_template.order_strategy);
      if (already_added == diagnostic_order_strategies.end()) {
        diagnostic_order_strategies.push_back(strategy_template.order_strategy);
      }
    }

    order_diagnostics.reserve(diagnostic_order_strategies.size());
    for (const auto order_strategy : diagnostic_order_strategies) {
      order_diagnostics.push_back(BuildOrderDiagnostic(real_clock_loads.loads, experiment_max_fanout,
                                                       real_clock_loads.bounding_box_diameter, order_strategy, real_arm9_case));
    }
  }
  std::ostringstream order_diagnostic_csv;
  order_diagnostic_csv << BuildOrderDiagnosticCsvHeader();
  for (const auto& diagnostic : order_diagnostics) {
    AppendOrderDiagnosticCsv(order_diagnostic_csv, diagnostic);
  }

  std::vector<std::string> synthetic_head_to_head_lines = {
      "group                   cases  cont_wins  disc_wins  ties   avg_gap     avg_gap_pct",
      BuildHeadToHeadRow("synthetic_overall", synthetic_head_to_head),
  };
  const auto case_mix_lines = BuildCaseMixTableLines(synthetic_specs);
  const auto overall_ranking_lines = BuildAggregateTableLines(aggregates, synthetic_specs.size());
  const auto family_ranking_lines = BuildNamedRankingSnapshotLines(family_aggregate_maps, family_case_counts, strategy_templates.size());
  const auto load_scale_ranking_lines
      = BuildNamedRankingSnapshotLines(load_scale_aggregate_maps, load_scale_case_counts, strategy_templates.size());
  const auto benchmark_scope_lines = BuildBenchmarkScopeLines(strategy_templates);
  const auto retained_strategy_audit_lines = BuildRetainedStrategyAuditLines(aggregates, strategy_descriptors);
  const auto family_head_to_head_lines = BuildNamedHeadToHeadLines(family_head_to_head_maps);
  const auto load_scale_head_to_head_lines = BuildNamedHeadToHeadLines(load_scale_head_to_head_maps);
  const auto real_arm9_ranking_lines = skip_real_arm9
                                           ? std::vector<std::string>{"skipped (" + std::string(kSkipRealArm9Env) + "=1)"}
                                           : BuildCandidateTableLines(real_arm9_case.candidates, real_arm9_case.candidates.size());
  const auto real_arm9_head_to_head_lines = skip_real_arm9
                                                ? std::vector<std::string>{"skipped (" + std::string(kSkipRealArm9Env) + "=1)"}
                                                : std::vector<std::string>{
                                                      "group                   cases  cont_wins  disc_wins  ties   avg_gap     avg_gap_pct",
                                                      [&real_arm9_case]() -> std::string {
                                                        HeadToHeadAggregate aggregate;
                                                        RecordHeadToHead(aggregate, real_arm9_case);
                                                        return BuildHeadToHeadRow("real_arm9", aggregate);
                                                      }(),
                                                  };
  const auto order_diagnostic_lines = skip_real_arm9 ? std::vector<std::string>{"skipped (" + std::string(kSkipRealArm9Env) + "=1)"}
                                                     : BuildOrderDiagnosticTableLines(order_diagnostics);
  const std::vector<std::string> artifact_names = {
      "cts.log",
      "report.log",
      "arm9_strategy_cases.csv",
      "arm9_strategy_aggregate.csv",
      "arm9_strategy_distribution_aggregate.csv",
      "arm9_strategy_load_scale_aggregate.csv",
      "arm9_order_diagnostics.csv",
  };
  const auto report = BuildExperimentSummaryReport(
      real_clock_loads, batch_label, synthetic_specs.size(), strategy_templates.size(), experiment_max_fanout, experiment_max_cap,
      experiment_routing_layer, benchmark_scope_lines, case_mix_lines, overall_ranking_lines, family_ranking_lines,
      load_scale_ranking_lines, retained_strategy_audit_lines, synthetic_head_to_head_lines, family_head_to_head_lines,
      load_scale_head_to_head_lines, real_arm9_ranking_lines, real_arm9_head_to_head_lines, order_diagnostic_lines, artifact_names);

  WriteExperimentArtifact(MakeCaseOutputPath(output_dir, "arm9_strategy_cases.csv"), case_csv.str());
  WriteExperimentArtifact(MakeCaseOutputPath(output_dir, "arm9_strategy_aggregate.csv"), aggregate_csv.str());
  WriteExperimentArtifact(MakeCaseOutputPath(output_dir, "arm9_strategy_distribution_aggregate.csv"), distribution_aggregate_csv.str());
  WriteExperimentArtifact(MakeCaseOutputPath(output_dir, "arm9_strategy_load_scale_aggregate.csv"), load_scale_aggregate_csv.str());
  WriteExperimentArtifact(MakeCaseOutputPath(output_dir, "arm9_order_diagnostics.csv"), order_diagnostic_csv.str());
  WriteExperimentArtifact(report_path, report);
  emit_progress("[linear_clustering] arm9 experiment artifacts written: " + output_dir.string());

  ScopedLogFile log_guard(cts_log_path, "Linear Clustering Arm9 Experiment");
  EmitInfoReport(InfoReport{.title = std::string(kArm9ExperimentTitle), .content = report});
}

}  // namespace icts_test::linear_clustering::realtech
