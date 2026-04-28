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
#include <string>

#include "evaluation/ClockTreeEvaluator.hh"

namespace icts {

#define FLOW_MANAGER_INST (icts::FlowManager::getInst())

struct CTSFlowRunSummary
{
  bool success = true;
  std::size_t total_clocks = 0U;
  std::size_t successful_clocks = 0U;
  std::size_t skipped_clocks = 0U;
  std::size_t failed_clocks = 0U;
  std::size_t total_sink_groups = 0U;
  std::size_t hard_macro_sinks = 0U;
  std::size_t regular_sinks = 0U;
  std::size_t selected_htree_level_count = 0U;
  unsigned selected_htree_depth = 0U;
  std::size_t htree_inserted_buffer_count = 0U;
  std::size_t htree_inserted_net_count = 0U;
};

class FlowManager
{
 public:
  static auto getInst() -> FlowManager&
  {
    static FlowManager inst;
    return inst;
  }

  auto runCTS() -> void;
  auto readData() -> void;
  auto run() -> void;
  auto evaluate() -> void;
  auto report(const std::string& save_dir) -> void;
  auto outputRuntimeSetup() -> void;
  auto outputSummary() const -> ClockTreeSummary;
  auto outputRunSummary() const -> CTSFlowRunSummary;
  auto reset() -> void;

  FlowManager(const FlowManager& other) = delete;
  FlowManager(FlowManager&& other) = delete;
  auto operator=(const FlowManager& other) -> FlowManager& = delete;
  auto operator=(FlowManager&& other) -> FlowManager& = delete;

 private:
  FlowManager() = default;
  ~FlowManager() = default;

  auto emitKeyResults(double elapsed_time_s, double peak_vmem_delta_mb) const -> void;

  CTSFlowRunSummary _run_summary;
  bool _runtime_setup_emitted = false;
  bool _evaluation_ready = false;
};

}  // namespace icts
