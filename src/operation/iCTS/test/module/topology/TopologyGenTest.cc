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
 * @file TopologyGenTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-19
 * @brief Unit tests for TopologyGen module.
 */

#include <gtest/gtest-param-test.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "common/TestUtils.hh"
#include "database/design/Pin.hh"
#include "database/spatial/Point.hh"
#include "database/spatial/Tree.hh"
#include "module/topology/TopologyGen.hh"
#include "utils/geometry/Geometry.hh"
#include "utils/logger/Logger.hh"

namespace icts_test {
namespace {

constexpr int kCanvasWidth = 10000;
constexpr int kCanvasHeight = 8000;
constexpr unsigned kSeedNormalSmall = 12345;
constexpr unsigned kSeedNormalLarge = 54321;
constexpr unsigned kSeedMixture = 2026;
constexpr unsigned kSeedQuadrantOne = 1001;
constexpr unsigned kSeedQuadrantUneven = 1002;
constexpr std::array<double, 4> kQuadrantOneWeights = {1.0, 0.0, 0.0, 0.0};
constexpr std::array<double, 4> kQuadrantUnevenWeights = {0.5, 0.2, 0.3, 0.0};

enum class DistKind : std::uint8_t
{
  kNormal,
  kMixture,
  kQuadrants
};

struct TopologyCase
{
  std::string name;
  DistKind kind = DistKind::kNormal;
  std::size_t count = 0;
  int width = 0;
  int height = 0;
  unsigned seed = 0;
  std::array<double, 4> quadrant_weights = {1.0, 1.0, 1.0, 1.0};
};

auto generate_case(const TopologyCase& test_case) -> GeneratedPins
{
  const CanvasSize canvas{test_case.width, test_case.height};
  switch (test_case.kind) {
    case DistKind::kNormal:
      return MakeNormal(test_case.count, canvas, test_case.seed);
    case DistKind::kMixture:
      return MakeGaussianMixture(test_case.count, canvas, test_case.seed);
    case DistKind::kQuadrants:
    default:
      return MakeWeightedQuadrants(test_case.count, canvas, test_case.seed, test_case.quadrant_weights);
  }
}

class LogGuard
{
 public:
  explicit LogGuard(const std::filesystem::path& path) { LOG_INST.set_log_file(path.string()); }
  ~LogGuard() { LOG_INST.close(); }

  LogGuard(const LogGuard&) = delete;
  auto operator=(const LogGuard&) -> LogGuard& = delete;
  LogGuard(LogGuard&&) = delete;
  auto operator=(LogGuard&&) -> LogGuard& = delete;
};

auto is_valid_pos(const icts::Point<int>& position) -> bool
{
  return position.get_x() >= 0 && position.get_y() >= 0;
}

}  // namespace

class TopologyGenTestInterface : public ::testing::TestWithParam<TopologyCase>
{
};

TEST_P(TopologyGenTestInterface, BuildAndVisualize)
{
  const auto& test_case = GetParam();
  const auto output_dir = ResolveOutputDir();

  std::error_code error_code;
  std::filesystem::create_directories(output_dir, error_code);
  ASSERT_FALSE(error_code) << "Failed to create output dir: " << output_dir.string() << " (" << error_code.message() << ")";

  const auto log_path = output_dir / ("topology_" + test_case.name + ".log");
  const LogGuard guard(log_path);

  CTS_LOG_INFO << "Topology test start: " << test_case.name << ", count=" << test_case.count << ", seed=" << test_case.seed;

  auto data = generate_case(test_case);
  ASSERT_EQ(data.loads.size(), test_case.count);

  auto tree = icts::TopologyGen::build(data.loads);

  TopologyStats stats;
  std::unordered_map<const icts::Pin*, std::size_t> cluster_map;
  std::vector<icts::Point<int>> centers;
  std::string error;

  ASSERT_TRUE(AnalyzeTopology(tree, data.loads, stats, cluster_map, centers, error)) << error;

  EXPECT_EQ(stats.tree_size, tree.get_size());
  EXPECT_GE(stats.leaf_count, 1U);
  EXPECT_LE(stats.leaf_count, data.loads.size());
  EXPECT_EQ(cluster_map.size(), data.loads.size());

  std::ostringstream summary;
  summary << "Tree size=" << stats.tree_size << ", leafs=" << stats.leaf_count << ", leaf_load[min/max/avg]=" << stats.min_leaf_load << "/"
          << stats.max_leaf_load << "/" << stats.avg_leaf_load << ", empty_leafs=" << stats.empty_leaf_count;
  CTS_LOG_INFO << summary.str();

  const auto invalid_id = std::numeric_limits<std::size_t>::max();
  std::size_t invalid_parent_count = 0;
  std::size_t invalid_pos_count = 0;
  std::size_t missing_edge_count = 0;
  std::size_t zero_edge_count = 0;

  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    if (id == tree.get_root()) {
      continue;
    }
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    const auto parent_id = node->get_parent();
    if (parent_id == invalid_id || parent_id >= tree.get_size()) {
      ++invalid_parent_count;
      CTS_LOG_WARNING << "Edge issue: node=" << id << " invalid parent id=" << parent_id;
      continue;
    }
    const auto* parent = tree.get_node(parent_id);
    if (parent == nullptr) {
      ++invalid_parent_count;
      CTS_LOG_WARNING << "Edge issue: node=" << id << " parent missing id=" << parent_id;
      continue;
    }

    const auto& child_pos = node->get_position();
    const auto& parent_pos = parent->get_position();
    const bool child_valid = is_valid_pos(child_pos);
    const bool parent_valid = is_valid_pos(parent_pos);
    if (!child_valid) {
      ++invalid_pos_count;
      CTS_LOG_WARNING << "Edge issue: node=" << id << " invalid child pos=(" << child_pos.get_x() << "," << child_pos.get_y() << ")";
      continue;
    }
    if (!parent_valid) {
      ++missing_edge_count;
      CTS_LOG_WARNING << "Edge issue: node=" << id << " parent=" << parent_id << " invalid parent pos=(" << parent_pos.get_x() << ","
                      << parent_pos.get_y() << ")";
      continue;
    }

    const auto dist = icts::geometry::Manhattan(child_pos, parent_pos);
    if (dist == 0) {
      ++zero_edge_count;
      CTS_LOG_WARNING << "Edge issue: node=" << id << " parent=" << parent_id << " zero-length edge child=(" << child_pos.get_x() << ","
                      << child_pos.get_y() << ") parent=(" << parent_pos.get_x() << "," << parent_pos.get_y() << ")";
    }
  }

  EXPECT_EQ(invalid_parent_count, 0U) << "Invalid parent count: " << invalid_parent_count;
  EXPECT_EQ(invalid_pos_count, 0U) << "Invalid child positions: " << invalid_pos_count;
  EXPECT_EQ(missing_edge_count, 0U) << "Missing edges (parent position invalid): " << missing_edge_count;
  EXPECT_EQ(zero_edge_count, 0U) << "Zero-length edges: " << zero_edge_count;

  // Use first-level clustering for cleaner visualization (only biPartition)
  std::unordered_map<const icts::Pin*, std::size_t> first_level_cluster_map;
  std::vector<icts::Point<int>> first_level_centers;
  std::string first_level_error;
  ASSERT_TRUE(AnalyzeFirstLevelClusters(tree, data.loads, first_level_cluster_map, first_level_centers, first_level_error))
      << first_level_error;

  const std::string cluster_name = "cluster_" + test_case.name + "_" + std::to_string(test_case.count) + ".svg";
  const auto cluster_path = output_dir / cluster_name;
  EXPECT_TRUE(WriteClusterSvg(cluster_path.string(), data.loads, first_level_cluster_map, first_level_centers))
      << "Failed to write cluster svg: " << cluster_path.string();
  CTS_LOG_INFO << "Cluster svg saved (first-level only): " << cluster_path.string();

  const std::string topo_name = "topology_" + test_case.name + "_" + std::to_string(test_case.count) + ".svg";
  const auto topo_path = output_dir / topo_name;
  EXPECT_TRUE(WriteTopologySvg(topo_path.string(), tree, data.loads)) << "Failed to write topology svg: " << topo_path.string();
  CTS_LOG_INFO << "Topology svg saved: " << topo_path.string();
}

INSTANTIATE_TEST_SUITE_P(
    TopologyCases, TopologyGenTestInterface,
    ::testing::Values(TopologyCase{"normal_small", DistKind::kNormal, 256, kCanvasWidth, kCanvasHeight, kSeedNormalSmall},
                      TopologyCase{"normal_large", DistKind::kNormal, 1024, kCanvasWidth, kCanvasHeight, kSeedNormalLarge},
                      TopologyCase{"mixture", DistKind::kMixture, 512, kCanvasWidth, kCanvasHeight, kSeedMixture},
                      TopologyCase{"quadrant_one", DistKind::kQuadrants, 256, kCanvasWidth, kCanvasHeight, kSeedQuadrantOne,
                                   kQuadrantOneWeights},
                      TopologyCase{"quadrant_three_uneven", DistKind::kQuadrants, 768, kCanvasWidth, kCanvasHeight, kSeedQuadrantUneven,
                                   kQuadrantUnevenWeights}));

}  // namespace icts_test
