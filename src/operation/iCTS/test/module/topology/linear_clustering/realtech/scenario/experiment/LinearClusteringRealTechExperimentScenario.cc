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
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Log.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "common/types/TestDataTypes.hh"
#include "database/config/Config.hh"
#include "module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechShared.hh"

namespace icts {
enum class LinearOrderStrategy;
}  // namespace icts

namespace icts_test::linear_clustering::realtech {

namespace {
using namespace detail;
using namespace experiment;
using common::io::EmitInfoReport;
using common::io::WriteRawTextLog;
using common::logging::ScopedLogFile;
using common::realtech::EnsureRealTechSetup;
using common::realtech::RealTechMode;

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
