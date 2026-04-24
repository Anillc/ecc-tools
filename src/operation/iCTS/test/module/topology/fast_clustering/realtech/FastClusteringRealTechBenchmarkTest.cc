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
 * @file FastClusteringRealTechBenchmarkTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Real-tech CTS clustering benchmark entry point for fast and linear clustering.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "Clustering.hh"
#include "FastClusteringRealTechBenchmarkInternal.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/types/TestDataTypes.hh"
#include "module/topology/fast_clustering/FastClustering.hh"
#include "module/topology/linear_clustering/LinearClustering.hh"
#include "utils/logger/Schema.hh"

namespace icts {
class Pin;
struct LinearClusteringConfig;
}  // namespace icts

namespace icts_test::fast_clustering::realtech {
namespace {

using common::io::PrepareCleanOutputDir;
using common::io::ResolveOutputDir;
using common::io::WriteRawTextLog;
using common::logging::ScopedLogFile;

TEST(FastClusteringRealTechBenchmarkTest, CompareTwentyPlacementCases)
{
  const auto output_dir = PrepareCleanOutputDir(ResolveOutputDir() / "fast_clustering" / "realtech_benchmark" / "current_run");
  const auto cts_log_path = output_dir / "cts.log";
  ScopedLogFile scoped_log(cts_log_path, "Fast Clustering RealTech Benchmark");

  const auto cases = DiscoverBenchmarkCases();
  const auto assets = ResolveTechAssets();
  common::io::EmitInfoReport(InfoReport{.title = "CTS Clustering Benchmark Inventory", .content = BuildInventoryReport(cases, assets)});

  if (cases.empty()) {
    GTEST_SKIP() << "benchmark root unavailable: " << kBenchmarkRoot;
  }
  ASSERT_EQ(cases.size(), kRequiredCaseCount);

  std::string tech_error;
  ASSERT_TRUE(ValidateTechAssets(assets, tech_error)) << tech_error;

  std::vector<CaseResult> results;
  results.reserve(cases.size());
  double linear_runtime_ms = 0.0;
  double fast_runtime_ms = 0.0;
  double linear_score = 0.0;
  double fast_score = 0.0;
  const auto svg_dir = output_dir / std::string(kClusterSvgDirName);

  for (const auto& benchmark_case : cases) {
    auto loaded = LoadBenchmarkCase(benchmark_case, assets, output_dir);
    ASSERT_TRUE(loaded.ok) << benchmark_case.case_name << ": " << loaded.error;
    common::io::EmitInfoReport(
        InfoReport{.title = "CTS Clustering Case Statistics", .content = BuildLoadedCaseReport(benchmark_case, loaded)});

    auto config = BuildBenchmarkConfig();
    auto linear_run
        = RunAndMeasure("linear", loaded.loads, config,
                        [](const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& run_config) -> icts::ClusterResult {
                          return icts::LinearClustering::runDefault(loads, run_config);
                        });
    auto fast_run
        = RunAndMeasure("fast", loaded.loads, config,
                        [](const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& run_config) -> icts::ClusterResult {
                          return icts::FastClustering::runDefault(loads, run_config);
                        });

    linear_runtime_ms += linear_run.metrics.runtime_ms;
    fast_runtime_ms += fast_run.metrics.runtime_ms;
    linear_score += linear_run.metrics.total_score;
    fast_score += fast_run.metrics.total_score;

    std::string svg_error;
    const auto svg_path = WriteCaseClusterSvg(svg_dir, benchmark_case, loaded.loads, linear_run.result, fast_run.result, svg_error);
    EXPECT_FALSE(svg_path.empty()) << benchmark_case.case_name << ": " << svg_error;
    std::string cluster_svg;
    if (!svg_path.empty()) {
      cluster_svg = (std::filesystem::path(std::string(kClusterSvgDirName)) / svg_path.filename()).string();
      icts::schema::EmitArtifact("CTS clustering structure svg", svg_path);
    }

    loaded.loads.clear();
    results.push_back(CaseResult{.benchmark_case = benchmark_case,
                                 .loaded = std::move(loaded),
                                 .linear = std::move(linear_run.metrics),
                                 .fast = std::move(fast_run.metrics),
                                 .cluster_svg = std::move(cluster_svg)});
  }

  const auto linear_routing_cap_variance = SumLinearRoutingCapProxyVariance(results);
  const auto fast_routing_cap_variance = SumFastRoutingCapProxyVariance(results);
  auto summary = BuildSummaryReport(results, linear_runtime_ms, fast_runtime_ms, linear_score, fast_score);
  summary += "visualization_svg_dir=" + std::string(kClusterSvgDirName) + "\n";
  summary += "visualization_svg_count=" + std::to_string(results.size()) + "\n";
  WriteRawTextLog(output_dir / "cts_clustering_cases.csv", BuildCasesCsv(results));
  WriteRawTextLog(output_dir / "cts_clustering_comparison.csv", BuildComparisonCsv(results));
  WriteRawTextLog(output_dir / "cts_clustering_visualizations.csv", BuildVisualizationCsv(results));
  WriteRawTextLog(output_dir / "cts_clustering_ranking.csv", BuildRankingCsv(linear_runtime_ms, fast_runtime_ms, linear_score, fast_score,
                                                                             linear_routing_cap_variance, fast_routing_cap_variance));
  WriteRawTextLog(output_dir / "report.log", summary);
  common::io::EmitInfoReport(InfoReport{.title = "CTS Clustering Benchmark Summary", .content = summary});

  ASSERT_LT(fast_runtime_ms, linear_runtime_ms);
  ASSERT_LT(fast_score, linear_score);
  ASSERT_LT(fast_routing_cap_variance, linear_routing_cap_variance);
  for (const auto& result : results) {
    EXPECT_TRUE(result.linear.legal) << result.benchmark_case.case_name << " linear illegal";
    EXPECT_TRUE(result.fast.legal) << result.benchmark_case.case_name << " fast illegal";
  }
}

}  // namespace
}  // namespace icts_test::fast_clustering::realtech
