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
 * @file LinearClusteringRealTechExperimentReporting.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief CSV and report rendering for real-tech linear clustering experiments.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/io/TestArtifactIO.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"

namespace icts_test::linear_clustering::realtech::experiment {

using common::io::WriteRawTextLog;
using common::realtech::EnsureRealTechSetup;

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

}  // namespace icts_test::linear_clustering::realtech::experiment
