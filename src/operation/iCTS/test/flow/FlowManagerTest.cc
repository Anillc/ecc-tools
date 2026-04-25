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
 * @file FlowManagerTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-25
 * @brief Lightweight interface and branch-wiring tests for FlowManager.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CTSAPI.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/design/Net.hh"
#include "database/design/Pin.hh"
#include "database/spatial/Point.hh"
#include "flow/FlowManager.hh"

namespace icts_test {
namespace {

class ScopedDesignReset
{
 public:
  ScopedDesignReset() { DESIGN_INST.reset(); }
  ~ScopedDesignReset() { DESIGN_INST.reset(); }
};

auto MakeTestOptions() -> icts::FlowManager::RunOptions
{
  icts::FlowManager::RunOptions options;
  options.branch_root_buffer = icts::FlowManager::BranchRootBufferSpec{
      .cell_master = "CTS_TEST_BUF",
      .input_pin = "A",
      .output_pin = "Y",
      .output_drive_cap_pf = 1.0,
  };
  return options;
}

auto MakeFallbackCandidateOptions() -> icts::FlowManager::RunOptions
{
  icts::FlowManager::RunOptions options;
  options.branch_root_buffer = icts::FlowManager::BranchRootBufferSpec{
      .cell_master = "CTS_TEST_SEED_BUF",
      .input_pin = "I",
      .output_pin = "Z",
      .output_drive_cap_pf = std::nullopt,
  };
  options.branch_root_buffer_candidates = {
      icts::FlowManager::BranchRootBufferSpec{
          .cell_master = "CTS_TEST_BUF_X4",
          .input_pin = "A4",
          .output_pin = "Y4",
          .output_drive_cap_pf = 4.0,
      },
      icts::FlowManager::BranchRootBufferSpec{
          .cell_master = "CTS_TEST_BUF_X1",
          .input_pin = "A1",
          .output_pin = "Y1",
          .output_drive_cap_pf = 1.0,
      },
  };
  return options;
}

auto FindBranch(const icts::FlowManager::ClockSummary& clock_summary, icts::FlowManager::BranchKind kind)
    -> const icts::FlowManager::BranchSummary*
{
  const auto iter = std::ranges::find_if(clock_summary.branches, [kind](const auto& branch) -> bool { return branch.kind == kind; });
  return iter == clock_summary.branches.end() ? nullptr : &(*iter);
}

auto ContainsPin(const std::vector<icts::Pin*>& pins, const icts::Pin* pin) -> bool
{
  return std::ranges::find(pins, pin) != pins.end();
}

TEST(FlowManagerTest, EmptyAPIFlowEntryIsCallable)
{
  const ScopedDesignReset scoped_design_reset;

  icts::CTSAPI::ctsFlow();
}

TEST(FlowManagerTest, MixedMacroAndRegularSingleSinkBranchesUseSeparateDirectNets)
{
  const ScopedDesignReset scoped_design_reset;

  icts::Pin source("top/clk", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Inst macro_inst("macro0", "MACRO_CELL", icts::InstType::kMacroBlock, icts::Point<int>(100, 0));
  icts::Pin macro_sink("macro0/CLK", icts::PinType::kClock, icts::Point<int>(100, 0), &macro_inst);
  macro_inst.add_pin(&macro_sink);

  icts::Inst reg_inst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(200, 0));
  icts::Pin regular_sink("reg0/CLK", icts::PinType::kClock, icts::Point<int>(200, 0), &reg_inst);
  reg_inst.add_pin(&regular_sink);

  icts::Net original_net("clk_net");
  original_net.set_driver(&source);
  original_net.add_load(&macro_sink);
  original_net.add_load(&regular_sink);
  source.set_net(&original_net);
  macro_sink.set_net(&original_net);
  regular_sink.set_net(&original_net);

  auto clock = std::make_unique<icts::Clock>("clk", "clk_net", &source, std::vector<icts::Pin*>{&macro_sink, &regular_sink});
  auto* clock_ptr = DESIGN_INST.add_clock(std::move(clock));

  const auto summary = icts::FlowManager::run(MakeTestOptions());

  ASSERT_TRUE(summary.success);
  ASSERT_EQ(summary.total_clocks, 1U);
  ASSERT_EQ(summary.successful_clocks, 1U);
  ASSERT_EQ(summary.failed_clocks, 0U);
  ASSERT_EQ(summary.clocks.size(), 1U);
  const auto& clock_summary = summary.clocks.front();
  EXPECT_TRUE(clock_summary.success);
  EXPECT_EQ(clock_summary.hard_macro_sinks, 1U);
  EXPECT_EQ(clock_summary.regular_sinks, 1U);
  ASSERT_EQ(clock_summary.branches.size(), 2U);

  const auto* macro_branch = FindBranch(clock_summary, icts::FlowManager::BranchKind::kHardMacro);
  const auto* regular_branch = FindBranch(clock_summary, icts::FlowManager::BranchKind::kRegular);
  ASSERT_NE(macro_branch, nullptr);
  ASSERT_NE(regular_branch, nullptr);

  EXPECT_TRUE(macro_branch->used_direct_connection);
  EXPECT_TRUE(regular_branch->used_direct_connection);
  ASSERT_NE(clock_summary.source_to_branch_roots_net, nullptr);
  EXPECT_EQ(clock_summary.source_to_branch_roots_net->get_driver(), &source);
  EXPECT_EQ(source.get_net(), clock_summary.source_to_branch_roots_net);
  EXPECT_EQ(clock_summary.source_to_branch_roots_net->get_loads().size(), 2U);
  EXPECT_TRUE(ContainsPin(clock_summary.source_to_branch_roots_net->get_loads(), macro_branch->root_buffer_input_pin));
  EXPECT_TRUE(ContainsPin(clock_summary.source_to_branch_roots_net->get_loads(), regular_branch->root_buffer_input_pin));
  EXPECT_EQ(original_net.get_driver(), nullptr);
  EXPECT_TRUE(original_net.get_loads().empty());

  ASSERT_NE(macro_branch->direct_sink_net, nullptr);
  ASSERT_NE(regular_branch->direct_sink_net, nullptr);
  EXPECT_NE(macro_branch->direct_sink_net, regular_branch->direct_sink_net);
  EXPECT_EQ(macro_sink.get_net(), macro_branch->direct_sink_net);
  EXPECT_EQ(regular_sink.get_net(), regular_branch->direct_sink_net);
  EXPECT_EQ(macro_branch->direct_sink_net->get_driver(), macro_branch->root_buffer_output_pin);
  EXPECT_EQ(regular_branch->direct_sink_net->get_driver(), regular_branch->root_buffer_output_pin);
  ASSERT_EQ(macro_branch->direct_sink_net->get_loads().size(), 1U);
  ASSERT_EQ(regular_branch->direct_sink_net->get_loads().size(), 1U);
  EXPECT_EQ(macro_branch->direct_sink_net->get_loads().front(), &macro_sink);
  EXPECT_EQ(regular_branch->direct_sink_net->get_loads().front(), &regular_sink);

  ASSERT_NE(clock_ptr, nullptr);
  EXPECT_EQ(clock_ptr->get_inserted_insts().size(), 2U);
  EXPECT_EQ(clock_ptr->get_inserted_nets().size(), 3U);
  EXPECT_NE(clock_ptr->get_inserted_insts().front()->get_name().find("root_buf"), std::string::npos);
}

TEST(FlowManagerTest, DirectBranchUsesMinimumDriveFallbackAndKeepsConnectionsAfterResize)
{
  const ScopedDesignReset scoped_design_reset;

  icts::Pin source("top/clk", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Inst reg_inst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  icts::Pin regular_sink("reg0/CLK", icts::PinType::kClock, icts::Point<int>(100, 0), &reg_inst);
  reg_inst.add_pin(&regular_sink);

  icts::Net original_net("clk_net");
  original_net.set_driver(&source);
  original_net.add_load(&regular_sink);
  source.set_net(&original_net);
  regular_sink.set_net(&original_net);

  auto clock = std::make_unique<icts::Clock>("clk", "clk_net", &source, std::vector<icts::Pin*>{&regular_sink});
  DESIGN_INST.add_clock(std::move(clock));

  const auto summary = icts::FlowManager::run(MakeFallbackCandidateOptions());

  ASSERT_TRUE(summary.success);
  ASSERT_EQ(summary.clocks.size(), 1U);
  const auto& clock_summary = summary.clocks.front();
  ASSERT_TRUE(clock_summary.success);
  ASSERT_EQ(clock_summary.branches.size(), 1U);
  const auto& branch = clock_summary.branches.front();

  ASSERT_TRUE(branch.used_direct_connection);
  EXPECT_FALSE(branch.used_synthesis);
  EXPECT_TRUE(branch.used_minimum_drive_root_driver);
  EXPECT_FALSE(branch.used_recommended_root_driver);
  ASSERT_NE(branch.root_buffer_inst, nullptr);
  ASSERT_NE(branch.root_buffer_input_pin, nullptr);
  ASSERT_NE(branch.root_buffer_output_pin, nullptr);
  ASSERT_NE(branch.direct_sink_net, nullptr);

  EXPECT_EQ(branch.root_buffer_cell_master, "CTS_TEST_BUF_X1");
  EXPECT_EQ(branch.root_buffer_inst->get_cell_master(), "CTS_TEST_BUF_X1");
  EXPECT_EQ(branch.root_buffer_input_pin->get_name(), branch.root_buffer_inst->get_name() + "/A1");
  EXPECT_EQ(branch.root_buffer_output_pin->get_name(), branch.root_buffer_inst->get_name() + "/Y1");
  EXPECT_EQ(branch.direct_sink_net->get_driver(), branch.root_buffer_output_pin);
  EXPECT_EQ(branch.root_buffer_output_pin->get_net(), branch.direct_sink_net);
  EXPECT_EQ(regular_sink.get_net(), branch.direct_sink_net);

  ASSERT_NE(clock_summary.source_to_branch_roots_net, nullptr);
  EXPECT_EQ(clock_summary.source_to_branch_roots_net->get_driver(), &source);
  EXPECT_EQ(branch.root_buffer_input_pin->get_net(), clock_summary.source_to_branch_roots_net);
  EXPECT_TRUE(ContainsPin(clock_summary.source_to_branch_roots_net->get_loads(), branch.root_buffer_input_pin));
}

TEST(FlowManagerTest, SyntheticSynthesisRecommendationResizesBranchRootBuffer)
{
  const ScopedDesignReset scoped_design_reset;

  icts::Pin source("top/clk", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Inst reg_inst0("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  icts::Pin sink0("reg0/CLK", icts::PinType::kClock, icts::Point<int>(100, 0), &reg_inst0);
  reg_inst0.add_pin(&sink0);
  icts::Inst reg_inst1("reg1", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(200, 0));
  icts::Pin sink1("reg1/CLK", icts::PinType::kClock, icts::Point<int>(200, 0), &reg_inst1);
  reg_inst1.add_pin(&sink1);

  icts::Net original_net("clk_net");
  original_net.set_driver(&source);
  original_net.add_load(&sink0);
  original_net.add_load(&sink1);
  source.set_net(&original_net);
  sink0.set_net(&original_net);
  sink1.set_net(&original_net);

  auto clock = std::make_unique<icts::Clock>("clk", "clk_net", &source, std::vector<icts::Pin*>{&sink0, &sink1});
  DESIGN_INST.add_clock(std::move(clock));

  auto options = MakeFallbackCandidateOptions();
  options.branch_synthesis_override = icts::FlowManager::BranchSynthesisOverride{
      .success = true,
      .failure_reason = "",
      .recommended_root_driver = icts::FlowManager::BranchRootBufferSpec{
          .cell_master = "CTS_TEST_HTREE_REC_BUF",
          .input_pin = "REC_A",
          .output_pin = "REC_Y",
          .output_drive_cap_pf = 8.0,
      },
  };

  const auto summary = icts::FlowManager::run(options);

  ASSERT_TRUE(summary.success);
  ASSERT_EQ(summary.clocks.size(), 1U);
  const auto& clock_summary = summary.clocks.front();
  ASSERT_TRUE(clock_summary.success);
  ASSERT_EQ(clock_summary.branches.size(), 1U);
  const auto& branch = clock_summary.branches.front();

  EXPECT_TRUE(branch.used_synthesis);
  EXPECT_FALSE(branch.used_direct_connection);
  EXPECT_TRUE(branch.used_recommended_root_driver);
  EXPECT_FALSE(branch.used_minimum_drive_root_driver);
  ASSERT_NE(branch.root_buffer_inst, nullptr);
  ASSERT_NE(branch.root_buffer_input_pin, nullptr);
  ASSERT_NE(branch.root_buffer_output_pin, nullptr);
  ASSERT_NE(branch.synthesis_source_to_root_net, nullptr);

  EXPECT_EQ(branch.root_buffer_cell_master, "CTS_TEST_HTREE_REC_BUF");
  EXPECT_EQ(branch.root_buffer_inst->get_cell_master(), "CTS_TEST_HTREE_REC_BUF");
  EXPECT_EQ(branch.root_buffer_input_pin->get_name(), branch.root_buffer_inst->get_name() + "/REC_A");
  EXPECT_EQ(branch.root_buffer_output_pin->get_name(), branch.root_buffer_inst->get_name() + "/REC_Y");
  EXPECT_EQ(branch.synthesis_source_to_root_net->get_driver(), branch.root_buffer_output_pin);
  EXPECT_EQ(branch.root_buffer_output_pin->get_net(), branch.synthesis_source_to_root_net);
  EXPECT_NE(sink0.get_net(), &original_net);
  EXPECT_NE(sink1.get_net(), &original_net);
  EXPECT_EQ(sink0.get_net(), sink1.get_net());

  ASSERT_NE(clock_summary.source_to_branch_roots_net, nullptr);
  EXPECT_EQ(clock_summary.source_to_branch_roots_net->get_driver(), &source);
  EXPECT_EQ(branch.root_buffer_input_pin->get_net(), clock_summary.source_to_branch_roots_net);
  EXPECT_TRUE(ContainsPin(clock_summary.source_to_branch_roots_net->get_loads(), branch.root_buffer_input_pin));
}

TEST(FlowManagerTest, SyntheticSynthesisWithoutUsableRecommendationUsesMinimumDriveFallback)
{
  const ScopedDesignReset scoped_design_reset;

  icts::Pin source("top/clk", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Inst reg_inst0("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  icts::Pin sink0("reg0/CLK", icts::PinType::kClock, icts::Point<int>(100, 0), &reg_inst0);
  reg_inst0.add_pin(&sink0);
  icts::Inst reg_inst1("reg1", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(200, 0));
  icts::Pin sink1("reg1/CLK", icts::PinType::kClock, icts::Point<int>(200, 0), &reg_inst1);
  reg_inst1.add_pin(&sink1);

  icts::Net original_net("clk_net");
  original_net.set_driver(&source);
  original_net.add_load(&sink0);
  original_net.add_load(&sink1);
  source.set_net(&original_net);
  sink0.set_net(&original_net);
  sink1.set_net(&original_net);

  auto clock = std::make_unique<icts::Clock>("clk", "clk_net", &source, std::vector<icts::Pin*>{&sink0, &sink1});
  DESIGN_INST.add_clock(std::move(clock));

  auto options = MakeFallbackCandidateOptions();
  options.branch_synthesis_override = icts::FlowManager::BranchSynthesisOverride{
      .success = true,
      .failure_reason = "",
      .recommended_root_driver = std::nullopt,
  };

  const auto summary = icts::FlowManager::run(options);

  ASSERT_TRUE(summary.success);
  ASSERT_EQ(summary.clocks.size(), 1U);
  const auto& clock_summary = summary.clocks.front();
  ASSERT_TRUE(clock_summary.success);
  ASSERT_EQ(clock_summary.branches.size(), 1U);
  const auto& branch = clock_summary.branches.front();

  EXPECT_TRUE(branch.used_synthesis);
  EXPECT_FALSE(branch.used_direct_connection);
  EXPECT_FALSE(branch.used_recommended_root_driver);
  EXPECT_TRUE(branch.used_minimum_drive_root_driver);
  ASSERT_NE(branch.root_buffer_inst, nullptr);
  ASSERT_NE(branch.root_buffer_input_pin, nullptr);
  ASSERT_NE(branch.root_buffer_output_pin, nullptr);
  ASSERT_NE(branch.synthesis_source_to_root_net, nullptr);

  EXPECT_EQ(branch.root_buffer_cell_master, "CTS_TEST_BUF_X1");
  EXPECT_EQ(branch.root_buffer_inst->get_cell_master(), "CTS_TEST_BUF_X1");
  EXPECT_EQ(branch.root_buffer_input_pin->get_name(), branch.root_buffer_inst->get_name() + "/A1");
  EXPECT_EQ(branch.root_buffer_output_pin->get_name(), branch.root_buffer_inst->get_name() + "/Y1");
  EXPECT_EQ(branch.synthesis_source_to_root_net->get_driver(), branch.root_buffer_output_pin);
  EXPECT_EQ(branch.root_buffer_output_pin->get_net(), branch.synthesis_source_to_root_net);
  EXPECT_NE(sink0.get_net(), &original_net);
  EXPECT_NE(sink1.get_net(), &original_net);
  EXPECT_EQ(sink0.get_net(), sink1.get_net());
}

}  // namespace
}  // namespace icts_test
