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
 * @file LongWireBufferingOperatorTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include <string>

#include "LongWireBufferingOperator.hh"
#include "SolverPipelineData.hh"
#include "TreeBuilder.hh"
#include "gtest/gtest.h"

namespace {

int countInsertedBuffersOnPath(icts::Pin* parent_driver, icts::Pin* child_load)
{
  int inserted_count = 0;
  auto* current_load = child_load;

  while (current_load != nullptr) {
    auto* current_net = current_load->get_net();
    if (current_net == nullptr) {
      return -1;
    }

    auto* current_driver = current_net->get_driver_pin();
    if (current_driver == parent_driver) {
      return inserted_count;
    }

    auto* current_inst = current_driver == nullptr ? nullptr : current_driver->get_inst();
    if (current_inst == nullptr) {
      return -1;
    }

    current_load = current_inst->get_load_pin();
    ++inserted_count;
  }

  return -1;
}

TEST(LongWireBufferingOperatorTest, UniformlyBuffersAllEdgesAtSameDepth)
{
  size_t next_id = 0;
  icts::SolverRuntimeContext runtime;
  runtime.gen_id_provider = [&next_id]() { return ++next_id; };
  runtime.save_log = [](const std::string&) {};

  icts::SolverPipelineState state;
  state.net_name = "clk";
  state.min_buffering_length = 100.0;

  auto* source = new icts::Inst("source", icts::Point(0, 0), icts::InstType::kBuffer);
  auto* root = icts::TreeBuilder::genBufInst("root", icts::Point(250, 0));
  auto* left = icts::TreeBuilder::genBufInst("left", icts::Point(90, 100));
  auto* right = icts::TreeBuilder::genBufInst("right", icts::Point(210, 0));

  auto* source_driver = source->get_driver_pin();
  auto* root_driver = root->get_driver_pin();
  auto* root_load = root->get_load_pin();
  auto* left_load = left->get_load_pin();
  auto* right_load = right->get_load_pin();

  icts::TreeBuilder::directConnectTree(source_driver, root_load);
  icts::TreeBuilder::directConnectTree(root_driver, left_load);
  icts::TreeBuilder::directConnectTree(root_driver, right_load);

  auto* root_net = new icts::Net("clk_root", source_driver, {root_load});
  auto* branch_net = new icts::Net("clk_branch", root_driver, {left_load, right_load});

  state.nets = {root_net, branch_net};
  state.net_records = {{root_net, -1}, {branch_net, -1}};
  state.buffer_depths.emplace(root, 0);
  state.buffer_depths.emplace(left, 1);
  state.buffer_depths.emplace(right, 1);
  state.buffers_by_depth = {{root}, {left, right}};
  state.max_depth = 1;

  icts::LongWireBufferingOperator(runtime).run(state);

  EXPECT_EQ(state.buffers_by_depth.size(), 2U);
  EXPECT_EQ(state.buffers_by_depth[0].size(), 3U);
  EXPECT_EQ(state.buffers_by_depth[1].size(), 6U);
  EXPECT_EQ(state.net_records.size(), 8U);

  ASSERT_EQ(root_net->get_load_pins().size(), 1U);
  EXPECT_NE(root_net->get_load_pins().front(), root_load);

  ASSERT_EQ(branch_net->get_load_pins().size(), 2U);
  EXPECT_NE(branch_net->get_load_pins()[0], left_load);
  EXPECT_NE(branch_net->get_load_pins()[1], right_load);

  auto* rewritten_root_driver = root_load->get_net() == nullptr ? nullptr : root_load->get_net()->get_driver_pin();
  auto* rewritten_left_driver = left_load->get_net() == nullptr ? nullptr : left_load->get_net()->get_driver_pin();
  auto* rewritten_right_driver = right_load->get_net() == nullptr ? nullptr : right_load->get_net()->get_driver_pin();
  ASSERT_NE(rewritten_root_driver, nullptr);
  ASSERT_NE(rewritten_left_driver, nullptr);
  ASSERT_NE(rewritten_right_driver, nullptr);
  EXPECT_NE(rewritten_root_driver, source_driver);
  EXPECT_NE(rewritten_left_driver, root_driver);
  EXPECT_NE(rewritten_right_driver, root_driver);

  EXPECT_EQ(countInsertedBuffersOnPath(source_driver, root_load), 2);
  EXPECT_EQ(countInsertedBuffersOnPath(root_driver, left_load), 2);
  EXPECT_EQ(countInsertedBuffersOnPath(root_driver, right_load), 2);
}

}  // namespace
