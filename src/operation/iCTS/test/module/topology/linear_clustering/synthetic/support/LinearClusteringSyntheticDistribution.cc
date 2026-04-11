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
 * @file LinearClusteringSyntheticDistribution.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Synthetic distribution sweep support.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/io/TestArtifactIO.hh"
#include "common/linear_clustering/artifact/ClusterArtifactSupport.hh"
#include "common/types/TestDataTypes.hh"
#include "common/visualization/TestVisualization.hh"
#include "database/spatial/Point.hh"
#include "module/topology/clustering/Clustering.hh"
#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts_test::linear_clustering::synthetic {
namespace {

struct DistributionArtifacts
{
  std::unordered_map<const icts::Pin*, std::size_t> cluster_map;
  std::vector<icts::Point<int>> centers;
  std::vector<std::size_t> cluster_sizes;
};

auto BuildDistributionArtifacts(const icts::ClusterResult& result, const std::vector<icts::Pin*>& loads, DistributionArtifacts& artifacts,
                                std::string& error) -> bool
{
  return common::linear_clustering::BuildClusterArtifacts(result, loads, artifacts.cluster_map, artifacts.centers, artifacts.cluster_sizes,
                                                          error);
}

auto FormatAverageClusterSize(double avg_cluster_size) -> std::string
{
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream << std::setprecision(2) << avg_cluster_size;
  return stream.str();
}

auto BuildDistributionReport(const SyntheticSweepCase& test_case, const detail::ClusteringInvocation& invocation,
                             const std::vector<std::size_t>& cluster_sizes) -> std::string
{
  const auto minmax = std::ranges::minmax_element(cluster_sizes);
  const auto singleton_cluster_count = static_cast<std::size_t>(std::ranges::count(cluster_sizes, detail::kSingletonClusterSize));
  const std::size_t total_loads = std::accumulate(cluster_sizes.begin(), cluster_sizes.end(), std::size_t{0});
  const double avg_cluster_size
      = cluster_sizes.empty() ? 0.0 : static_cast<double>(total_loads) / static_cast<double>(cluster_sizes.size());
  const auto average_cluster_size = FormatAverageClusterSize(avg_cluster_size);

  std::ostringstream report;
  report << "Test: LinearSyntheticSweeps/LinearClusteringTestInterface.SyntheticSweepAndVisualization\n";
  report << "Mode: synthetic distribution sweep\n";
  report << "Case: " << test_case.name << "_" << test_case.load_count << "\n";
  report << "Distribution: " << DistName(test_case.kind) << "\n";
  report << "Seed: " << test_case.seed << "\n";
  report << "Input: load_count=" << total_loads << ", max_fanout=" << invocation.max_fanout << ", max_diameter=" << invocation.max_diameter
         << ", api=" << invocation.api_name << "\n";
  report << "Observed result: clusters=" << cluster_sizes.size() << ", singleton_clusters=" << singleton_cluster_count
         << ", cluster_size[min/max/avg]=" << *minmax.min << "/" << *minmax.max << "/" << average_cluster_size << "\n";
  report << "Artifacts: clusters.svg, report.log\n";
  return report.str();
}

auto ExpectDistributionArtifacts(const DistributionArtifacts& artifacts, std::size_t expected_load_count) -> void
{
  ASSERT_FALSE(artifacts.cluster_sizes.empty());
  EXPECT_EQ(artifacts.cluster_map.size(), expected_load_count);
  EXPECT_GE(artifacts.centers.size(), 1U);
  EXPECT_EQ(std::accumulate(artifacts.cluster_sizes.begin(), artifacts.cluster_sizes.end(), std::size_t{0}), expected_load_count);
}

auto EmitDistributionReport(const std::filesystem::path& output_dir, const std::string& case_tag, const std::string& report) -> void
{
  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(common::io::WriteTextLog(report_path, report)) << "Failed to write report: " << report_path.string();
  common::io::EmitInfoReport(InfoReport{.title = case_tag, .content = report});
}

}  // namespace

auto RunSyntheticSweepAndVisualization(const SyntheticSweepCase& test_case) -> void
{
  auto generated = detail::GenerateSyntheticCase(test_case);
  ASSERT_EQ(generated.loads.size(), test_case.load_count);

  const std::string case_tag = test_case.name + "_" + std::to_string(test_case.load_count);
  const auto output_dir = detail::PrepareSyntheticOutputDir("synthetic_sweep_" + case_tag);
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare output dir for " << case_tag;

  detail::ClusteringInvocation invocation;
  const auto result = detail::RunLinearClustering(generated.loads, detail::kDefaultMinClusterSize, invocation);

  DistributionArtifacts artifacts;
  std::string error;
  ASSERT_TRUE(BuildDistributionArtifacts(result, generated.loads, artifacts, error)) << error;
  ASSERT_TRUE(detail::ValidateClusterLegality(result, invocation, error)) << error;
  ExpectDistributionArtifacts(artifacts, generated.loads.size());

  const auto svg_path = output_dir / "clusters.svg";
  EXPECT_TRUE(common::visualization::WriteClusterSvg(svg_path.string(), generated.loads, artifacts.cluster_map, artifacts.centers))
      << "Failed to write svg: " << svg_path.string();

  const auto report = BuildDistributionReport(test_case, invocation, artifacts.cluster_sizes);
  EmitDistributionReport(output_dir, case_tag, report);
}

}  // namespace icts_test::linear_clustering::synthetic
