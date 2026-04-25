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
 * @file FlowManager.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-25
 * @brief CTS flow orchestration manager
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace icts {

class Clock;
class Inst;
class Net;
class Pin;

class FlowManager
{
 public:
  enum class BranchKind
  {
    kHardMacro,
    kRegular
  };

  struct BranchRootBufferSpec
  {
    std::string cell_master;
    std::string input_pin;
    std::string output_pin;
    std::optional<double> output_drive_cap_pf = std::nullopt;
  };

  struct BranchSynthesisOverride
  {
    bool success = true;
    std::string failure_reason;
    std::optional<BranchRootBufferSpec> recommended_root_driver = std::nullopt;
  };

  struct RunOptions
  {
    std::optional<BranchRootBufferSpec> branch_root_buffer = std::nullopt;
    std::vector<BranchRootBufferSpec> branch_root_buffer_candidates;
    std::optional<BranchSynthesisOverride> branch_synthesis_override = std::nullopt;
  };

  struct BranchSummary
  {
    BranchKind kind = BranchKind::kRegular;
    bool success = false;
    bool used_direct_connection = false;
    bool used_synthesis = false;
    std::string failure_reason;
    std::size_t sink_count = 0U;
    std::string root_buffer_cell_master;
    bool used_recommended_root_driver = false;
    bool used_minimum_drive_root_driver = false;
    Inst* root_buffer_inst = nullptr;
    Pin* root_buffer_input_pin = nullptr;
    Pin* root_buffer_output_pin = nullptr;
    Net* direct_sink_net = nullptr;
    Net* synthesis_source_to_root_net = nullptr;
  };

  struct ClockSummary
  {
    bool success = false;
    bool skipped = false;
    std::string failure_reason;
    std::string clock_name;
    std::string clock_net_name;
    std::size_t total_sinks = 0U;
    std::size_t valid_sinks = 0U;
    std::size_t hard_macro_sinks = 0U;
    std::size_t regular_sinks = 0U;
    Net* source_to_branch_roots_net = nullptr;
    std::vector<BranchSummary> branches;
  };

  struct RunSummary
  {
    bool success = true;
    std::size_t total_clocks = 0U;
    std::size_t successful_clocks = 0U;
    std::size_t skipped_clocks = 0U;
    std::size_t failed_clocks = 0U;
    std::size_t total_branches = 0U;
    std::size_t hard_macro_sinks = 0U;
    std::size_t regular_sinks = 0U;
    std::vector<ClockSummary> clocks;
  };

  FlowManager() = default;
  ~FlowManager() = default;

  static auto run() -> RunSummary;
  static auto run(const RunOptions& options) -> RunSummary;
  static auto runClock(Clock& clock, std::size_t clock_index, const RunOptions& options) -> ClockSummary;
};

}  // namespace icts
