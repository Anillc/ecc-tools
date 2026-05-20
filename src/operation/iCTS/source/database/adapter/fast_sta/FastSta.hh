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
 * @file FastSta.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS-facing facade for fast timing and power calculation.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icts {

class Clock;
class Net;
struct FastStaClockContext;
template <typename T>
class ClockSteinerTree;

#define FAST_STA_INST (icts::FastSTA::getInst())

}  // namespace icts

namespace icts_test {
class FastStaTestAccess;
}  // namespace icts_test

namespace icts {

using FastStaClockId = std::size_t;
using FastStaCharContextId = std::size_t;
using FastStaNodeId = std::size_t;
using FastStaNetId = std::size_t;
using FastStaRcNodeId = std::size_t;

constexpr auto kInvalidFastStaClockId = std::numeric_limits<FastStaClockId>::max();
constexpr auto kInvalidFastStaCharContextId = std::numeric_limits<FastStaCharContextId>::max();
constexpr auto kInvalidFastStaNodeId = std::numeric_limits<FastStaNodeId>::max();
constexpr auto kInvalidFastStaNetId = std::numeric_limits<FastStaNetId>::max();
constexpr auto kInvalidFastStaRcNodeId = std::numeric_limits<FastStaRcNodeId>::max();

struct FastStaPoint
{
  int x_dbu = 0;
  int y_dbu = 0;
};

struct FastStaRcSegment
{
  FastStaPoint begin;
  FastStaPoint end;
};

struct FastStaClockNetRouteGeometry
{
  std::string net_name;
  std::vector<FastStaRcSegment> routed_segments;
};

struct FastStaClockRouteGeometry
{
  int32_t design_dbu_per_um = 0;
  std::vector<FastStaClockNetRouteGeometry> clock_nets;
};

struct FastStaClockNetRcTreeCounts
{
  std::size_t rc_node_count = 0U;
  std::size_t rc_edge_count = 0U;
};

struct FastStaClockGraphProfile
{
  std::size_t node_count = 0U;
  std::size_t net_count = 0U;
  std::size_t sink_count = 0U;
  std::size_t buffer_input_count = 0U;
  std::size_t buffer_output_count = 0U;
};

struct FastStaClockAnalysisStatus
{
  bool timing_valid = false;
  bool power_valid = false;
};

struct FastStaClockSizingBuffer
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string inst_name;
  std::string cell_master;
};

struct FastStaClockSinkArrival
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string sink_name;
  double arrival_ns = 0.0;
};

struct FastStaClockTreeTopology
{
  FastStaNodeId source_node_id = kInvalidFastStaNodeId;
  std::vector<FastStaNodeId> parent_by_node;
  std::vector<std::vector<FastStaNodeId>> children_by_node;
};

enum class FastStaSlewRole
{
  kUnknown,
  kBufferInput,
  kSink
};

struct FastStaCapStatus
{
  FastStaNetId net_id = kInvalidFastStaNetId;
  std::string net_name;
  double load_cap_pf = 0.0;
  double max_cap_pf = 0.0;
  bool violated = false;
};

struct FastStaSlewStatus
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string node_name;
  FastStaSlewRole role = FastStaSlewRole::kUnknown;
  double slew_ns = 0.0;
  double max_slew_ns = 0.0;
  bool violated = false;
};

struct FastStaSkewSummary
{
  bool valid = false;
  FastStaNodeId min_sink_node_id = kInvalidFastStaNodeId;
  FastStaNodeId max_sink_node_id = kInvalidFastStaNodeId;
  std::string min_sink_name;
  std::string max_sink_name;
  double min_arrival_ns = 0.0;
  double max_arrival_ns = 0.0;
  double skew_ns = 0.0;
};

struct FastStaPowerSummary
{
  double switching_power_w = 0.0;
  double internal_power_w = 0.0;
  double leakage_power_w = 0.0;
  double total_power_w = 0.0;
  double area_um2 = 0.0;
};

struct FastStaBufferMasterChange
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string cell_master;
};

struct FastStaCharSegmentSpec
{
  double wirelength_um = 0.0;
  double effective_load_pf = 0.0;
};

struct FastStaCharTopologySpec
{
  std::string source_cell_master;
  std::string sink_cell_master;
  std::vector<std::string> buffer_cell_masters;
  std::vector<double> wire_segments_um;
  std::optional<int32_t> dbu_per_um = std::nullopt;
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
  double clock_period_ns = 0.0;
};

struct FastStaCharSampleResult
{
  bool valid = false;
  double delay_ns = 0.0;
  double output_slew_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_net_switch_power_w = 0.0;
};

class FastSTA
{
 public:
  static auto getInst() -> FastSTA&
  {
    static FastSTA inst;
    return inst;
  }

  FastSTA(const FastSTA& rhs) = delete;
  FastSTA(FastSTA&& rhs) = delete;
  auto operator=(const FastSTA& rhs) -> FastSTA& = delete;
  auto operator=(FastSTA&& rhs) -> FastSTA& = delete;

  static auto buildClockContext(const Clock& clock) -> FastStaClockId;
  static auto buildClockContext(const Clock& clock, const FastStaClockRouteGeometry& route_geometry) -> FastStaClockId;
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
  static auto injectNetRouteTree(FastStaClockId clock_id, const Net& net, const ClockSteinerTree<int>& route_tree) -> bool;
  static auto injectNetRouteTree(FastStaClockId clock_id, const Net& net, const ClockSteinerTree<int>& route_tree,
                                 FastStaClockNetRcTreeCounts& rc_tree_counts) -> bool;

  static auto queryClockGraphProfile(FastStaClockId clock_id) -> std::optional<FastStaClockGraphProfile>;
  static auto queryClockAnalysisStatus(FastStaClockId clock_id) -> std::optional<FastStaClockAnalysisStatus>;
  static auto queryClockTreeTopology(FastStaClockId clock_id) -> std::optional<FastStaClockTreeTopology>;
  static auto collectClockSizingBuffers(FastStaClockId clock_id) -> std::vector<FastStaClockSizingBuffer>;
  static auto collectClockSinkArrivals(FastStaClockId clock_id) -> std::vector<FastStaClockSinkArrival>;
  static auto queryClockNodeArrival(FastStaClockId clock_id, FastStaNodeId node_id) -> std::optional<double>;
  static auto querySinkArrival(FastStaClockId clock_id, std::string_view sink_pin_name) -> std::optional<double>;
  static auto querySkew(FastStaClockId clock_id) -> FastStaSkewSummary;
  static auto queryNodeSlew(FastStaClockId clock_id, FastStaNodeId node_id) -> std::optional<double>;
  static auto queryNetLoad(FastStaClockId clock_id, FastStaNetId net_id) -> std::optional<double>;
  static auto queryCapStatus(FastStaClockId clock_id, FastStaNetId net_id) -> std::optional<FastStaCapStatus>;
  static auto querySlewStatus(FastStaClockId clock_id, FastStaNodeId node_id) -> std::optional<FastStaSlewStatus>;
  static auto queryPower(FastStaClockId clock_id) -> FastStaPowerSummary;
  static auto queryArea(FastStaClockId clock_id) -> double;
  static auto queryClockIds() -> std::vector<FastStaClockId>;

 private:
  struct ContextStore;
  friend class ::icts_test::FastStaTestAccess;

  FastSTA();
  ~FastSTA();

  static auto queryClockContext(FastStaClockId clock_id) -> const FastStaClockContext*;
  static auto mutableClockContext(FastStaClockId clock_id) -> FastStaClockContext*;
  static auto registerClockContextForTest(FastStaClockContext context) -> FastStaClockId;

  std::unique_ptr<ContextStore> _contexts;
};

}  // namespace icts
