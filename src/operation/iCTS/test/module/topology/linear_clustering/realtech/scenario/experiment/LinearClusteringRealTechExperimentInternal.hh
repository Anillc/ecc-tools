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
 * @file LinearClusteringRealTechExperimentInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Internal data and reporting helpers for real-tech linear clustering experiments.
 */

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Pin.hh"
#include "common/types/TestDataTypes.hh"
#include "module/topology/linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"

namespace icts_test::linear_clustering::realtech {
namespace experiment {

using namespace detail;

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

auto ClampToIntRange(double value) -> int;
auto CalcBoundingBoxDiameter(const std::vector<icts::Pin*>& loads) -> int;
auto HasExactCapContext(const icts::Pin* pin) -> bool;
auto CalcPercentileValue(std::vector<double> values, double percentile) -> double;
auto SelectRoutingLayerFromConfig() -> int;
auto BuildArm9ExperimentConfig(const FanoutConfigSpec& spec) -> icts::LinearClusteringConfig;
auto BuildStrategyLabel(const StrategySweepCandidate& candidate) -> std::string;
auto SyntheticDistributionFamilyName(SyntheticDistributionFamily family) -> const char*;
auto SyntheticLoadScaleName(SyntheticLoadScale load_scale) -> const char*;
auto StrategyClassName(StrategyClass strategy_class) -> const char*;
auto IsDiscreteOrderStrategy(icts::LinearOrderStrategy order_strategy) -> bool;
auto ClampUnit(double value) -> double;
auto ReadEnvSizeValue(std::string_view env_name) -> std::optional<std::size_t>;
auto ReadEnvFlag(std::string_view env_name) -> bool;
auto BuildRetainedBenchmarkTemplates() -> std::vector<BenchmarkStrategyTemplate>;
auto BuildStrategyDescriptors(const std::vector<BenchmarkStrategyTemplate>& templates)
    -> std::unordered_map<std::string, StrategyDescriptor>;
auto BuildBenchmarkSpecs(std::size_t max_fanout, int max_diameter, const std::vector<BenchmarkStrategyTemplate>& templates)
    -> std::vector<FanoutConfigSpec>;
auto CollectRankedCandidateIndices(const std::vector<StrategySweepCandidate>& candidates) -> std::vector<std::size_t>;
auto CompareStrategyCandidates(const StrategySweepCandidate& lhs, const StrategySweepCandidate& rhs) -> bool;
auto ResolveSelectedIndex(const std::vector<StrategySweepCandidate>& candidates) -> std::optional<std::size_t>;
auto EvaluateExperimentCase(const std::vector<icts::Pin*>& loads, const std::vector<FanoutConfigSpec>& candidate_specs)
    -> ExperimentCaseRecord;
auto BuildOrderDiagnostic(const std::vector<icts::Pin*>& loads, std::size_t max_fanout, int max_diameter,
                          icts::LinearOrderStrategy order_strategy, const ExperimentCaseRecord& real_arm9_case) -> OrderDiagnostic;
auto BuildSourcePointCloud(const RealClockLoads& real_clock_loads) -> SourcePointCloud;
auto ResolveSyntheticLoadCount(SyntheticLoadScale load_scale) -> std::size_t;
auto SampleSyntheticDieScale(std::mt19937& generator, std::size_t real_load_count, SyntheticDistributionFamily family,
                             std::size_t synthetic_load_count) -> std::pair<double, double>;
auto SampleJitterRatio(std::mt19937& generator, SyntheticDistributionFamily family, std::size_t synthetic_load_count) -> double;
auto BuildRepresentativeCaseSpecs(std::size_t real_load_count) -> std::vector<SyntheticCaseSpec>;
auto GenerateArm9SubsetPoints(const SourcePointCloud& source_cloud, std::size_t count, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>;
auto GenerateGaussianHotspotPoints(const SourcePointCloud& source_cloud, std::size_t count, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>;
auto GenerateAxisBandPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>;
auto GenerateDiagonalBandPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>;
auto GenerateUniformSpreadPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>;
auto GenerateRepresentativeNormalizedPoints(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>;
auto BuildPinsFromNormalizedPoints(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec,
                                   const std::vector<std::pair<double, double>>& normalized_points, unsigned seed) -> GeneratedPins;
auto BuildRepresentativePins(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec, unsigned seed) -> GeneratedPins;

auto BuildCandidateTableLines(const std::vector<StrategySweepCandidate>& candidates, std::size_t limit) -> std::vector<std::string>;
auto BuildAggregateTableLines(const std::vector<StrategyAggregate>& aggregates, std::size_t total_synthetic_cases)
    -> std::vector<std::string>;
auto BuildCaseCsvHeader() -> std::string;
auto AppendCaseCsv(std::ostringstream& output_stream, const ExperimentCaseRecord& record) -> void;
auto BuildAggregateCsvHeader() -> std::string;
auto BuildOrderDiagnosticCsvHeader() -> std::string;
auto AppendAggregateCsv(std::ostringstream& output_stream, const StrategyAggregate& aggregate) -> void;
auto AppendOrderDiagnosticCsv(std::ostringstream& output_stream, const OrderDiagnostic& diagnostic) -> void;
auto MaterializeAggregateVector(const AggregateMap& aggregate_map) -> std::vector<StrategyAggregate>;
auto BuildCaseMixTableLines(const std::vector<SyntheticCaseSpec>& specs) -> std::vector<std::string>;
auto BuildNamedRankingSnapshotLines(const std::unordered_map<std::string, AggregateMap>& grouped_aggregates,
                                    const std::unordered_map<std::string, std::size_t>& group_case_counts, std::size_t top_n)
    -> std::vector<std::string>;
auto BuildNamedAggregateCsvHeader(std::string_view group_name_label) -> std::string;
auto AppendNamedAggregateCsv(std::ostringstream& output_stream, std::string_view group_name, const StrategyAggregate& aggregate) -> void;
auto BuildBenchmarkScopeLines(const std::vector<BenchmarkStrategyTemplate>& strategy_templates) -> std::vector<std::string>;
auto BuildRetainedStrategyAuditLines(const std::vector<StrategyAggregate>& overall_aggregates,
                                     const std::unordered_map<std::string, StrategyDescriptor>& descriptors) -> std::vector<std::string>;
auto MakeCaseOutputPath(const std::filesystem::path& output_dir, std::string_view file_name) -> std::filesystem::path;
auto WriteExperimentArtifact(const std::filesystem::path& path, const std::string& content) -> void;
auto RecordAggregate(StrategyAggregate& aggregate, const StrategySweepCandidate& candidate, std::size_t rank) -> void;
auto RecordHeadToHead(HeadToHeadAggregate& aggregate, const ExperimentCaseRecord& record) -> void;
auto BuildHeadToHeadRow(std::string_view group_name, const HeadToHeadAggregate& aggregate) -> std::string;
auto BuildNamedHeadToHeadLines(const std::unordered_map<std::string, HeadToHeadAggregate>& grouped_aggregates) -> std::vector<std::string>;
auto BuildOrderDiagnosticTableLines(std::vector<OrderDiagnostic> diagnostics) -> std::vector<std::string>;
auto BuildExperimentSummaryReport(
    const RealClockLoads& real_clock_loads, std::string_view batch_label, std::size_t synthetic_case_count,
    std::size_t retained_candidate_count, std::size_t max_fanout, double max_cap_pf, int routing_layer,
    const std::vector<std::string>& benchmark_scope_lines, const std::vector<std::string>& case_mix_lines,
    const std::vector<std::string>& overall_ranking_lines, const std::vector<std::string>& family_ranking_lines,
    const std::vector<std::string>& load_scale_ranking_lines, const std::vector<std::string>& retained_strategy_audit_lines,
    const std::vector<std::string>& synthetic_head_to_head_lines, const std::vector<std::string>& family_head_to_head_lines,
    const std::vector<std::string>& load_scale_head_to_head_lines, const std::vector<std::string>& real_arm9_lines,
    const std::vector<std::string>& real_arm9_head_to_head_lines, const std::vector<std::string>& order_diagnostic_lines,
    const std::vector<std::string>& artifact_names) -> std::string;

}  // namespace experiment
}  // namespace icts_test::linear_clustering::realtech
