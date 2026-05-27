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
 * @file AnalyticalSolution.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree selected-solution build-stage contract.
 */

#pragma once

#include <string>
#include <vector>

#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/solution/finalization/SolutionFinalizer.hh"

namespace icts {

class CharBuilder;

namespace htree {
struct BoundaryConstraints;
struct BufferPatternLibrary;
struct DiagnosticBuild;
struct HTreeFanoutPruningConfig;
struct RootDriverCompensationInput;
struct SinkLoadRegionLegalityInput;
}  // namespace htree

namespace htree::analytical_solution {

struct AnalyticalHTreeSelectionBuild
{
  bool selected = false;
  std::string failure_reason;
  HTreeSelectedSolution selected_solution;
};

auto SelectAnalyticalHTreeSolution(htree::DiagnosticBuild& result, const HTree::Input& input, unsigned max_depth,
                                   const std::vector<HTree::LevelPlan>& full_level_plans, const std::vector<unsigned>& depth_candidates,
                                   BufferPatternLibrary& segment_pattern_library, const BoundaryConstraints& search_boundary_constraints,
                                   const HTreeFanoutPruningConfig& fanout_pruning_config,
                                   const RootDriverCompensationInput& root_driver_compensation_input,
                                   const SinkLoadRegionLegalityInput& sink_load_region_input, const CharBuilder& char_builder,
                                   const std::string& root_driver_clock_period_source) -> AnalyticalHTreeSelectionBuild;

}  // namespace htree::analytical_solution
}  // namespace icts
