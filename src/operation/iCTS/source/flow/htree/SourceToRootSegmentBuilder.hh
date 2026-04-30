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
 * @file SourceToRootSegmentBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Source-to-sink segment synthesis using reusable characterization frontiers.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SegmentChar.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "htree/HTreeBuilder.hh"

namespace icts {

class CharacterizationLibrary;

class SourceToRootSegmentBuilder
{
 public:
  struct BuildOptions
  {
    CharacterizationLibrary* characterization_library = nullptr;
    std::optional<double> min_input_slew_ns = std::nullopt;
    double required_load_cap_pf = 0.0;
    double source_drive_cap_pf = 0.0;
    std::string object_name_prefix;
    HTreeBuilder::LogContext log_context;
  };

  struct BuildResult
  {
    bool success = false;
    std::string failure_reason;
    std::optional<SegmentChar> best_char = std::nullopt;
    bool used_boundary_fallback = false;
    std::string boundary_fallback_reason;
    std::size_t strict_candidate_count = 0U;
    std::size_t fallback_candidate_count = 0U;
    double length_um = 0.0;
    unsigned length_idx = 0U;
    unsigned required_load_cap_idx = 0U;
    unsigned source_drive_cap_idx = 0U;
    std::optional<unsigned> min_input_slew_idx = std::nullopt;

    std::vector<std::unique_ptr<Inst>> inserted_insts;
    std::vector<std::unique_ptr<Pin>> inserted_pins;
    std::vector<std::unique_ptr<Net>> inserted_nets;
    std::vector<HTreeBuilder::InsertedInstLevel> inserted_inst_levels;
    std::vector<HTreeBuilder::InsertedNetLevel> inserted_net_levels;
  };

  SourceToRootSegmentBuilder() = delete;

  static auto build(Net& source_net, Pin* source, Pin* sink, const BuildOptions& options) -> BuildResult;
};

}  // namespace icts
