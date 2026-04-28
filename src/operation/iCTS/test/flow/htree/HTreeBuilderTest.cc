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
 * @file HTreeBuilderTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief Unit coverage for HTreeBuilder degenerate cases.
 */

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Net.hh"
#include "Pin.hh"
#include "Point.hh"
#include "Tree.hh"
#include "flow/htree/HTreeBuildObservation.hh"
#include "flow/htree/HTreeBuilder.hh"

namespace icts_test {
namespace {

auto ConnectRootNet(icts::Net& root_net, icts::Pin& root_driver, const std::vector<icts::Pin*>& loads) -> void
{
  root_net.set_driver(&root_driver);
  root_driver.set_net(&root_net);
  root_net.set_loads(loads);
  for (auto* load : loads) {
    if (load != nullptr) {
      load->set_net(&root_net);
    }
  }
}

TEST(HTreeBuilderTest, EmptyLoadsReturnsEmptyResult)
{
  icts::Pin root_driver("root_out", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Net root_net("root_net");
  ConnectRootNet(root_net, root_driver, {});

  const auto result = icts::HTreeBuilder::build(root_net);
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.failure_reason, "empty_root_net_loads");
  EXPECT_EQ(result.topology.get_size(), 0U);
  EXPECT_TRUE(result.levels.empty());
  EXPECT_FALSE(result.best_char.has_value());
  EXPECT_FALSE(result.best_pattern.has_value());
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.inserted_insts.empty());
  EXPECT_TRUE(result.inserted_nets.empty());
  EXPECT_EQ(result.root_net, &root_net);
  EXPECT_EQ(result.root_output_pin, &root_driver);
}

TEST(HTreeBuilderTest, EmptyLoadsAcceptExplicitBuildOptions)
{
  icts::Pin root_driver("root_out", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Net root_net("root_net");
  ConnectRootNet(root_net, root_driver, {});

  const auto result = icts::HTreeBuilder::build(root_net, icts::HTreeBuilder::BuildOptions{
                                                              .force_branch_buffer = true,
                                                              .min_top_input_slew_ns = 0.05,
                                                              .target_depth = std::nullopt,
                                                              .depth_explore_window = std::nullopt,
                                                              .htree_topology_tolerance = std::nullopt,
                                                              .fixed_topology_root_location = std::nullopt,
                                                              .characterization_library = nullptr,
                                                              .additional_characterization_lengths_um = {},
                                                              .enable_root_driver_sizing = true,
                                                              .topology_loads_are_local_buffers = false,
                                                              .log_context = {},
                                                              .object_name_prefix = "",
                                                          });
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.failure_reason, "empty_root_net_loads");
  EXPECT_EQ(result.topology.get_size(), 0U);
  EXPECT_TRUE(result.levels.empty());
  EXPECT_FALSE(result.best_char.has_value());
  EXPECT_FALSE(result.best_pattern.has_value());
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.inserted_insts.empty());
  EXPECT_TRUE(result.inserted_nets.empty());
  EXPECT_EQ(result.root_net, &root_net);
  EXPECT_EQ(result.root_output_pin, &root_driver);
}

TEST(HTreeBuilderTest, MissingRootDriverStopsBeforeTopology)
{
  auto load = std::make_unique<icts::Pin>("load0", icts::PinType::kClock, icts::Point<int>(100, 200));
  icts::Net root_net("root_net");
  root_net.set_loads({load.get()});

  const auto result = icts::HTreeBuilder::build(root_net);
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.failure_reason, "missing_root_driver_pin");
  EXPECT_EQ(result.topology.get_size(), 0U);
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.inserted_insts.empty());
  EXPECT_TRUE(result.inserted_nets.empty());
  EXPECT_EQ(result.root_net, &root_net);
  EXPECT_EQ(result.root_output_pin, nullptr);
  EXPECT_EQ(load->get_net(), nullptr);
}

TEST(HTreeBuilderTest, SingleLoadStopsBeforeCharacterization)
{
  icts::Pin root_driver("root_out", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Net root_net("root_net");
  auto load = std::make_unique<icts::Pin>("load0", icts::PinType::kClock, icts::Point<int>(100, 200));
  std::vector<icts::Pin*> loads{load.get()};
  ConnectRootNet(root_net, root_driver, loads);

  const auto result = icts::HTreeBuilder::build(root_net);
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.topology.get_size(), 1U);
  EXPECT_TRUE(result.levels.empty());
  EXPECT_FALSE(result.best_char.has_value());
  EXPECT_FALSE(result.best_pattern.has_value());
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.inserted_insts.empty());
  EXPECT_TRUE(result.inserted_nets.empty());
  EXPECT_EQ(result.root_net, &root_net);
  EXPECT_EQ(result.root_output_pin, &root_driver);
  EXPECT_EQ(root_net.get_driver(), &root_driver);
  ASSERT_EQ(root_net.get_loads().size(), 1U);
  EXPECT_EQ(root_net.get_loads().front(), load.get());
  EXPECT_EQ(load->get_net(), &root_net);
}

}  // namespace
}  // namespace icts_test
