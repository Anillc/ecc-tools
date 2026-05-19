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

#include "logger/Schema.hh"
#include "synthesis/htree/HTree.hh"

namespace icts {

class CharBuilder;

namespace htree {
struct BoundaryConstraints;
struct BufferPatternLibrary;
struct HTreeFanoutPruningOptions;
struct RootDriverCompensationOptions;
class SegmentFrontierCatalog;
}  // namespace htree

namespace htree::analytical_solution {

auto TryBuildAnalyticalHTreeResult(HTree::BuildResult& result, const HTree::BuildOptions& options,
                                   schema::SchemaWriter::StageScope& build_stage, unsigned max_depth,
                                   const std::vector<HTree::LevelPlan>& full_level_plans, const std::vector<unsigned>& depth_candidates,
                                   const SegmentFrontierCatalog& segment_frontier_catalog, BufferPatternLibrary& segment_pattern_library,
                                   const BoundaryConstraints& search_boundary_constraints,
                                   const HTreeFanoutPruningOptions& fanout_pruning_options,
                                   const RootDriverCompensationOptions& root_driver_compensation_options, const CharBuilder& char_builder,
                                   const std::string& root_driver_clock_period_source) -> bool;

}  // namespace htree::analytical_solution
}  // namespace icts
