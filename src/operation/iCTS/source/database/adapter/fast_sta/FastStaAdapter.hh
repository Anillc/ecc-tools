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
 * @file FastStaAdapter.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS-facing facade for fast timing and power calculation.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "FastStaTypes.hh"

namespace icts {

class Clock;
class ClockLayout;

#define FAST_STA_ADAPTER_INST (icts::FastStaAdapter::getInst())

class FastStaAdapter
{
 public:
  static auto getInst() -> FastStaAdapter&
  {
    static FastStaAdapter inst;
    return inst;
  }

  FastStaAdapter(const FastStaAdapter& rhs) = delete;
  FastStaAdapter(FastStaAdapter&& rhs) = delete;
  auto operator=(const FastStaAdapter& rhs) -> FastStaAdapter& = delete;
  auto operator=(FastStaAdapter&& rhs) -> FastStaAdapter& = delete;

  static auto buildClockContext(const Clock& clock) -> FastStaClockId;
  static auto buildClockContext(const Clock& clock, const ClockLayout& clock_layout, std::size_t clock_index) -> FastStaClockId;
  static auto rebuildClockContext(FastStaClockId clock_id) -> bool;
  static auto eraseClockContext(FastStaClockId clock_id) -> bool;
  static auto clear() -> void;

  static auto buildCharContext(const FastStaCharTopologySpec& spec) -> FastStaCharContextId;
  static auto eraseCharContext(FastStaCharContextId char_context_id) -> bool;
  static auto setCharLoad(FastStaCharContextId char_context_id, double effective_load_pf) -> bool;
  static auto runCharSample(FastStaCharContextId char_context_id, double input_slew_ns) -> FastStaCharSampleResult;

  static auto changeBufferMaster(FastStaClockId clock_id, FastStaNodeId node_id, std::string_view cell_master) -> bool;
  static auto changeBufferMasters(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
  static auto changeBufferMastersTimingOnly(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool;
  static auto updateTiming(FastStaClockId clock_id) -> bool;
  static auto updatePower(FastStaClockId clock_id) -> bool;

  static auto querySinkArrival(FastStaClockId clock_id, std::string_view sink_pin_name) -> std::optional<double>;
  static auto querySkew(FastStaClockId clock_id) -> FastStaSkewSummary;
  static auto queryNodeSlew(FastStaClockId clock_id, FastStaNodeId node_id) -> std::optional<double>;
  static auto queryNetLoad(FastStaClockId clock_id, FastStaNetId net_id) -> std::optional<double>;
  static auto queryCapStatus(FastStaClockId clock_id, FastStaNetId net_id) -> std::optional<FastStaCapStatus>;
  static auto querySlewStatus(FastStaClockId clock_id, FastStaNodeId node_id) -> std::optional<FastStaSlewStatus>;
  static auto queryPower(FastStaClockId clock_id) -> FastStaPowerSummary;
  static auto queryArea(FastStaClockId clock_id) -> double;
  static auto queryClockContext(FastStaClockId clock_id) -> const FastStaClockContext*;
  static auto mutableClockContext(FastStaClockId clock_id) -> FastStaClockContext*;
  static auto queryClockIds() -> std::vector<FastStaClockId>;

 private:
  FastStaAdapter() = default;
  ~FastStaAdapter() = default;

  std::vector<FastStaClockContext> _clock_contexts;
  std::vector<bool> _clock_context_valid;
  std::vector<FastStaClockContext> _char_contexts;
  std::vector<bool> _char_context_valid;
};

}  // namespace icts
