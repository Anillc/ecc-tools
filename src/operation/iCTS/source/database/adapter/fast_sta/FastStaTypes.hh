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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file FastStaTypes.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS-owned data types for fast timing and power calculation.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

enum class FastStaNodeKind
{
  kSource,
  kBufferInput,
  kBufferOutput,
  kSink
};

enum class FastStaTransition
{
  kRise,
  kFall
};

enum class FastStaLibertyTableKind
{
  kCellDelay,
  kOutputSlew,
  kInternalPower
};

enum class FastStaLibertyAxisKind
{
  kInputSlew,
  kOutputLoad,
  kUnknown
};

enum class FastStaDmpAlgorithm
{
  kCap,
  kPi,
  kZeroC2
};

struct FastStaPoint
{
  int x_dbu = 0;
  int y_dbu = 0;
};

struct FastStaPointKeyHash
{
  auto operator()(const std::pair<int, int>& key) const -> std::size_t
  {
    const auto x_hash = std::hash<int>{}(key.first);
    const auto y_hash = std::hash<int>{}(key.second);
    return x_hash ^ (y_hash + 0x9e3779b9U + (x_hash << 6U) + (x_hash >> 2U));
  }
};

struct FastStaRcSegment
{
  FastStaPoint begin;
  FastStaPoint end;
};

struct FastStaPiModel
{
  double near_cap_pf = 0.0;
  double resistance_ohm = 0.0;
  double far_cap_pf = 0.0;
};

struct FastStaRcNode
{
  std::string name;
  double wire_cap_pf = 0.0;
  double pin_cap_pf = 0.0;
  double cap_pf = 0.0;
  double downstream_cap_pf = 0.0;
  double elmore_delay_ns = 0.0;
  FastStaNodeId terminal_node_id = kInvalidFastStaNodeId;
};

struct FastStaRcEdge
{
  FastStaRcNodeId from = kInvalidFastStaRcNodeId;
  FastStaRcNodeId to = kInvalidFastStaRcNodeId;
  double resistance_ohm = 0.0;
};

struct FastStaNetParasitic
{
  std::vector<FastStaRcNode> rc_nodes;
  std::vector<FastStaRcEdge> rc_edges;
  std::unordered_map<std::string, FastStaRcNodeId> rc_node_id_by_name;
  FastStaRcNodeId root_rc_node_id = kInvalidFastStaRcNodeId;
  FastStaPiModel pi;
  double total_cap_pf = 0.0;
  bool pre_reduced_pi_elmore = false;
  bool valid = false;
};

struct FastStaTimingPoint
{
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  bool valid = false;
};

struct FastStaDmpDriverResult
{
  bool valid = false;
  bool driver_waveform_valid = false;
  FastStaDmpAlgorithm algorithm = FastStaDmpAlgorithm::kCap;
  FastStaTransition transition = FastStaTransition::kRise;
  std::string driver_cell_master;
  double ceff_pf = 0.0;
  double gate_delay_ns = 0.0;
  double driver_slew_ns = 0.0;
  double driver_waveform_delay_ns = 0.0;
  double t0_ns = 0.0;
  double dt_ns = 0.0;
  double near_cap_pf = 0.0;
  double far_cap_pf = 0.0;
  double rpi_ns_per_pf = 0.0;
  double rd_ns_per_pf = 0.0;
  double input_threshold = 0.5;
  double output_threshold = 0.5;
  double slew_lower_threshold = 0.3;
  double slew_upper_threshold = 0.7;
  double slew_derate = 1.0;
  double pole1_per_ns = 0.0;
  double pole2_per_ns = 0.0;
  double zero1_per_ns = 0.0;
  double k0 = 0.0;
  double k1 = 0.0;
  double k2 = 0.0;
  double k3 = 0.0;
  double k4 = 0.0;
};

struct FastStaDmpLoadResult
{
  bool valid = false;
  double wire_delay_ns = 0.0;
  double load_slew_ns = 0.0;
};

struct FastStaLibertyAxis
{
  FastStaLibertyAxisKind kind = FastStaLibertyAxisKind::kUnknown;
  std::vector<double> values;
};

struct FastStaLibertyTable
{
  FastStaLibertyTableKind kind = FastStaLibertyTableKind::kCellDelay;
  FastStaTransition transition = FastStaTransition::kRise;
  std::vector<FastStaLibertyAxis> axes;
  std::vector<double> values;

  [[nodiscard]] auto empty() const -> bool { return values.empty(); }
  [[nodiscard]] auto lookup(double input_slew_ns, double output_load_pf) const -> std::optional<double>;
};

struct FastStaLibertyArc
{
  std::string from_port;
  std::string to_port;
  bool negative_unate = false;
  std::vector<FastStaLibertyTable> delay_tables;
  std::vector<FastStaLibertyTable> slew_tables;
  std::vector<FastStaLibertyTable> internal_power_tables;
};

struct FastStaLibertyCell
{
  std::string cell_master;
  std::string input_port;
  std::string output_port;
  double input_cap_pf = 0.0;
  double output_cap_limit_pf = 0.0;
  double input_slew_limit_ns = 0.0;
  double input_threshold_rise = 0.5;
  double input_threshold_fall = 0.5;
  double output_threshold_rise = 0.5;
  double output_threshold_fall = 0.5;
  double slew_lower_threshold_rise = 0.3;
  double slew_lower_threshold_fall = 0.3;
  double slew_upper_threshold_rise = 0.7;
  double slew_upper_threshold_fall = 0.7;
  double slew_derate_from_library = 1.0;
  double area_um2 = 0.0;
  double voltage_v = 0.0;
  double leakage_power_w = 0.0;
  FastStaLibertyArc timing_arc;
};

struct FastStaPowerSummary
{
  double switching_power_w = 0.0;
  double internal_power_w = 0.0;
  double leakage_power_w = 0.0;
  double total_power_w = 0.0;
  double area_um2 = 0.0;
};

struct FastStaDirtyRegion
{
  bool valid = false;
  FastStaNodeId start_node_id = kInvalidFastStaNodeId;
  std::vector<FastStaNodeId> node_ids;
  std::vector<FastStaNetId> net_ids;
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
  double slew_ns = 0.0;
  double max_slew_ns = 0.0;
  bool violated = false;
};

struct FastStaBufferMasterChange
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  std::string cell_master;
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

struct FastStaNode
{
  FastStaNodeKind kind = FastStaNodeKind::kSink;
  std::string name;
  std::string inst_name;
  std::string pin_name;
  std::string cell_master;
  FastStaPoint location;
  double input_cap_pf = 0.0;
  double max_slew_ns = 0.0;
  FastStaNetId incoming_net_id = kInvalidFastStaNetId;
  std::vector<FastStaNetId> output_net_ids;
  FastStaTimingPoint timing;
  double internal_power_w = 0.0;
  double leakage_power_w = 0.0;
  double area_um2 = 0.0;
};

struct FastStaNet
{
  std::string name;
  FastStaNodeId driver_node_id = kInvalidFastStaNodeId;
  std::vector<FastStaNodeId> load_node_ids;
  double wire_resistance_ohm = 0.0;
  double wire_cap_pf = 0.0;
  double load_cap_pf = 0.0;
  double max_cap_pf = 0.0;
  FastStaNetParasitic parasitic;
  FastStaDmpDriverResult driver_dmp;
  double switching_power_w = 0.0;
};

struct FastStaClockContext
{
  std::string clock_name;
  std::string clock_net_name;
  double clock_period_ns = 0.0;
  int32_t dbu_per_um = 1;
  int routing_layer = 1;
  std::optional<double> wire_width_um;
  FastStaNodeId source_node_id = kInvalidFastStaNodeId;
  std::vector<FastStaNode> nodes;
  std::vector<FastStaNet> nets;
  std::unordered_map<std::string, FastStaNodeId> node_id_by_name;
  std::unordered_map<std::pair<int, int>, FastStaNodeId, FastStaPointKeyHash> node_id_by_location;
  std::unordered_map<std::string, FastStaNetId> net_id_by_name;
  std::unordered_map<std::string, FastStaLibertyCell> liberty_cell_by_master;
  FastStaSkewSummary skew;
  FastStaPowerSummary power;
  bool timing_valid = false;
  bool power_valid = false;
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
  int routing_layer = 1;
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

}  // namespace icts
