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
 * @file ClockSynthesisRealTechHTreeAssertions.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Real-tech ClockSynthesis H-tree result assertions.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "ClockSynthesisRealTechSmokeSupport.hh"
#include "Inst.hh"
#include "Pin.hh"
#include "Tree.hh"
#include "database/config/Config.hh"
#include "database/design/Net.hh"
#include "htree/HTreeBuilder.hh"

namespace icts_test::synthesis_realtech_smoke {
namespace {

auto FindLeafLevelPlan(const icts::HTreeBuilder::BuildResult& htree_result) -> const icts::HTreeBuilder::LevelPlan*
{
  const auto level_it
      = std::ranges::find_if(htree_result.levels, [](const icts::HTreeBuilder::LevelPlan& level) -> bool { return level.is_leaf_level; });
  if (level_it == htree_result.levels.end()) {
    return nullptr;
  }
  return &(*level_it);
}

}  // namespace

auto AssertNoSingleLoadExternalLeafBuffer(const icts::HTreeBuilder::BuildResult& htree_result) -> void
{
  const std::unordered_set<const icts::Inst*> inserted_insts(htree_result.inserted_insts.begin(), htree_result.inserted_insts.end());
  for (const auto* inst : htree_result.inserted_insts) {
    if (inst == nullptr || !inst->is_buffer()) {
      continue;
    }

    const auto* output_pin = inst->findDriverPin();
    ASSERT_NE(output_pin, nullptr);
    const auto* output_net = output_pin->get_net();
    if (output_net == nullptr || output_net->get_loads().size() != 1U || output_net->get_loads().front() == nullptr) {
      continue;
    }

    const auto* downstream_load = output_net->get_loads().front();
    const auto* downstream_inst = downstream_load->get_inst();
    const bool drives_internal_htree_inst = downstream_inst != nullptr && inserted_insts.contains(downstream_inst);
    EXPECT_TRUE(drives_internal_htree_inst) << "Expected leaf single-load buffer pruning to remove " << inst->get_name()
                                            << " but it still drives " << downstream_load->get_name();
  }
}

auto AssertUnrestrictedFrontierHTree(const icts::HTreeBuilder::BuildResult& htree_result) -> void
{
  ASSERT_TRUE(htree_result.success);
  EXPECT_FALSE(htree_result.force_branch_buffer);
  ASSERT_FALSE(htree_result.levels.empty());
}

auto AssertBranchBufferedHTree(const icts::HTreeBuilder::BuildResult& htree_result) -> void
{
  ASSERT_TRUE(htree_result.success);
  EXPECT_TRUE(htree_result.force_branch_buffer);
  ASSERT_FALSE(htree_result.levels.empty());

  const auto* leaf_level = FindLeafLevelPlan(htree_result);
  ASSERT_NE(leaf_level, nullptr);
  EXPECT_TRUE(leaf_level->selected_has_terminal_branch_buffer);
  EXPECT_FALSE(leaf_level->selected_terminal_cell_master.empty());
}

auto AssertDepthCandidateCoverage(const icts::HTreeBuilder::BuildResult& result) -> void
{
  ASSERT_FALSE(result.depth_candidates.empty());
  ASSERT_TRUE(result.selected_depth.has_value());

  const auto topology_levels = result.topology.levels();
  ASSERT_GT(topology_levels.size(), 1U);
  const auto max_depth = static_cast<unsigned>(topology_levels.size() - 1U);
  EXPECT_EQ(result.depth_candidates.size(), std::min<std::size_t>(CONFIG_INST.get_htree_depth_explore_window(), max_depth));

  const auto* selected_summary = FindSelectedDepthSummary(result);
  ASSERT_NE(selected_summary, nullptr);
  EXPECT_EQ(selected_summary->depth, result.selected_depth.value_or(0U));
  EXPECT_EQ(selected_summary->depth, result.levels.size());
  EXPECT_TRUE(selected_summary->success);
}

auto AssertSelectedHTreeLoadDistribution(const icts::HTreeBuilder::BuildResult& result) -> void
{
  const auto* selected_summary = FindSelectedDepthSummary(result);
  ASSERT_NE(selected_summary, nullptr);
  ASSERT_GT(selected_summary->htree_load_group_count, 0U);
  EXPECT_LE(selected_summary->htree_load_cap_min_pf, selected_summary->htree_load_cap_mean_pf);
  EXPECT_LE(selected_summary->htree_load_cap_min_pf, selected_summary->htree_load_cap_median_pf);
  EXPECT_LE(selected_summary->htree_load_cap_mean_pf, selected_summary->htree_load_cap_max_pf);
  EXPECT_LE(selected_summary->htree_load_cap_median_pf, selected_summary->htree_load_cap_max_pf);
}

}  // namespace icts_test::synthesis_realtech_smoke
