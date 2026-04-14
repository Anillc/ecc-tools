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
 * @file CharBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @brief Segment characterization builder implementation.
 */

#include "CharBuilder.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <ranges>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharCore.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "logger/Logger.hh"

namespace icts {
namespace {

constexpr unsigned kDefaultMaxPatternNodes = 8U;
constexpr std::uint64_t kTopologyPresentMask = 1ULL;
constexpr double kPercentFactor = 100.0;
constexpr double kCharClockPeriodNs = 10.0;
constexpr double kCapFeasibilityEpsilonPf = 1e-6;
constexpr double kLengthLatticeEpsilon = 1e-9;
constexpr unsigned kMaxTopologySlots = std::numeric_limits<std::uint64_t>::digits - 1U;
constexpr std::size_t kCharProgressLogStride = 32U;
constexpr bool kEnableCharPowerSampling = true;

auto makeUniformSweepSamples(double max_value, unsigned steps) -> std::vector<double>
{
  std::vector<double> samples;
  if (max_value <= 0.0 || steps == 0U) {
    return samples;
  }

  const double step_value = max_value / static_cast<double>(steps);
  for (unsigned step_index = 1; step_index <= steps; ++step_index) {
    samples.push_back(step_index * step_value);
  }
  return samples;
}

auto makeWireLengths(double unit_um, unsigned iterations) -> std::vector<double>
{
  std::vector<double> wire_lengths_um;
  if (unit_um <= 0.0 || iterations == 0U) {
    return wire_lengths_um;
  }

  for (unsigned wirelength_index = 1; wirelength_index <= iterations; ++wirelength_index) {
    wire_lengths_um.push_back(static_cast<double>(wirelength_index) * unit_um);
  }
  return wire_lengths_um;
}

auto calcLatticeSlotCount(double wire_length_um, double length_unit_um) -> unsigned
{
  if (wire_length_um <= 0.0 || length_unit_um <= 0.0) {
    return 0U;
  }
  return static_cast<unsigned>(std::floor((wire_length_um / length_unit_um) + kLengthLatticeEpsilon));
}

auto resolveRoutingLayer() -> int
{
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (routing_layers.empty()) {
    return 1;
  }
  return static_cast<int>(routing_layers.front());
}

auto resolveWireWidth() -> std::optional<double>
{
  const double wire_width = CONFIG_INST.get_wire_width();
  return wire_width > 0.0 ? std::optional<double>(wire_width) : std::nullopt;
}

// Output-port cap limit is the primary drive-strength proxy; table-axis max is the fallback.
auto resolveBufferDriveCap(const std::string& cell_master) -> double
{
  double max_cap = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (max_cap <= 0.0) {
    max_cap = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return max_cap;
}

auto resolveMaxSlew() -> double
{
  const double configured_max_slew_ns = CONFIG_INST.get_max_buf_tran();
  if (CONFIG_INST.has_max_buf_tran() && configured_max_slew_ns > 0.0) {
    CTS_LOG_INFO << "CharBuilder: max_slew resolved from Config = " << configured_max_slew_ns << " ns";
    return configured_max_slew_ns;
  }

  double liberty_port_min_slew = std::numeric_limits<double>::infinity();
  bool found_port_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double slew_limit = STA_ADAPTER_INST.queryCellInPinSlewLimit(cell_master);
    if (slew_limit > 0.0) {
      liberty_port_min_slew = std::min(liberty_port_min_slew, slew_limit);
      found_port_limit = true;
    }
  }
  if (found_port_limit) {
    CTS_LOG_INFO << "CharBuilder: max_slew resolved from liberty input pin limits = " << liberty_port_min_slew << " ns";
    return liberty_port_min_slew;
  }

  double liberty_table_min_slew = std::numeric_limits<double>::infinity();
  bool found_table_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double table_axis_max = STA_ADAPTER_INST.queryCellInPinSlewTableAxisMax(cell_master);
    if (table_axis_max > 0.0) {
      liberty_table_min_slew = std::min(liberty_table_min_slew, table_axis_max);
      found_table_limit = true;
    }
  }
  if (found_table_limit) {
    CTS_LOG_INFO << "CharBuilder: max_slew resolved from liberty table axes = " << liberty_table_min_slew << " ns";
    return liberty_table_min_slew;
  }

  CTS_LOG_WARNING << "CharBuilder: failed to resolve max_slew from Config/liberty limits/liberty tables";
  return 0.0;
}

auto resolveMaxCap() -> double
{
  const double configured_max_cap_pf = CONFIG_INST.get_max_cap();
  if (CONFIG_INST.has_max_cap() && configured_max_cap_pf > 0.0) {
    CTS_LOG_INFO << "CharBuilder: max_cap resolved from Config = " << configured_max_cap_pf << " pF";
    return configured_max_cap_pf;
  }

  double liberty_port_min_cap = std::numeric_limits<double>::infinity();
  bool found_port_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double cap_limit = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
    if (cap_limit > 0.0) {
      liberty_port_min_cap = std::min(liberty_port_min_cap, cap_limit);
      found_port_limit = true;
    }
  }
  if (found_port_limit) {
    CTS_LOG_INFO << "CharBuilder: max_cap resolved from liberty output pin limits = " << liberty_port_min_cap << " pF";
    return liberty_port_min_cap;
  }

  double liberty_table_min_cap = std::numeric_limits<double>::infinity();
  bool found_table_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double table_axis_max = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
    if (table_axis_max > 0.0) {
      liberty_table_min_cap = std::min(liberty_table_min_cap, table_axis_max);
      found_table_limit = true;
    }
  }
  if (found_table_limit) {
    CTS_LOG_INFO << "CharBuilder: max_cap resolved from liberty table axes = " << liberty_table_min_cap << " pF";
    return liberty_table_min_cap;
  }

  CTS_LOG_WARNING << "CharBuilder: failed to resolve max_cap from Config/liberty limits/liberty tables";
  return 0.0;
}

auto collectSortedBuffers() -> std::vector<CharBufferInfo>
{
  std::vector<CharBufferInfo> sorted_buffers;
  const auto& buffer_types = CONFIG_INST.get_buffer_types();
  if (buffer_types.empty()) {
    CTS_LOG_WARNING << "CharBuilder: no buffer types configured in Config";
    return sorted_buffers;
  }

  for (const auto& cell_master : buffer_types) {
    const double max_cap = resolveBufferDriveCap(cell_master);
    if (max_cap <= 0.0) {
      CTS_LOG_WARNING << "CharBuilder: buffer " << cell_master << " has invalid max_cap (" << max_cap << " pF), skipped";
      continue;
    }

    const double input_cap = STA_ADAPTER_INST.queryCharInputPinCap(cell_master);
    auto [in_pin, out_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    if (in_pin.empty() || out_pin.empty()) {
      CTS_LOG_WARNING << "CharBuilder: buffer " << cell_master << " has unresolved port names, skipped";
      continue;
    }

    sorted_buffers.push_back(CharBufferInfo{
        .cell_master = cell_master,
        .max_cap_pf = max_cap,
        .input_cap_pf = input_cap,
        .input_pin = std::move(in_pin),
        .output_pin = std::move(out_pin),
    });
  }

  std::ranges::sort(sorted_buffers, [](const CharBufferInfo& lhs_buffer_info, const CharBufferInfo& rhs_buffer_info) -> bool {
    return lhs_buffer_info.max_cap_pf < rhs_buffer_info.max_cap_pf;
  });

  const double buffer_redundancy_pct = CONFIG_INST.get_char_buf_redundancy_pct();
  if (buffer_redundancy_pct > 0.0 && sorted_buffers.size() > 1U) {
    std::vector<CharBufferInfo> filtered_buffers;
    filtered_buffers.push_back(sorted_buffers.front());
    for (std::size_t buffer_index = 1; buffer_index < sorted_buffers.size(); ++buffer_index) {
      const double prev_cap = filtered_buffers.back().max_cap_pf;
      const double curr_cap = sorted_buffers.at(buffer_index).max_cap_pf;
      if (prev_cap <= 0.0 || (curr_cap - prev_cap) / prev_cap >= buffer_redundancy_pct) {
        filtered_buffers.push_back(sorted_buffers.at(buffer_index));
      } else {
        CTS_LOG_INFO << "CharBuilder: removed near-neighbor buffer " << sorted_buffers.at(buffer_index).cell_master
                     << " (max_cap=" << curr_cap << " pF, gap=" << ((curr_cap - prev_cap) / prev_cap * kPercentFactor) << "%)";
      }
    }
    sorted_buffers = std::move(filtered_buffers);
  }

  return sorted_buffers;
}

auto resolveWireLengthUnitUm(const std::vector<CharBufferInfo>& sorted_buffers) -> double
{
  const double configured_unit_um = CONFIG_INST.get_wire_length_unit_um();
  if (configured_unit_um > 0.0) {
    CTS_LOG_INFO << "CharBuilder: wire_length_unit resolved from Config = " << configured_unit_um << " um";
    return configured_unit_um;
  }

  // choose the strongest usable buffer and use 10x cell height.
  double strongest_cap_pf = -1.0;
  double strongest_height_um = 0.0;
  std::string strongest_master;
  for (const auto& buffer_info : sorted_buffers) {
    const double cell_height_um = STA_ADAPTER_INST.queryCellHeightUm(buffer_info.cell_master);
    if (cell_height_um <= 0.0) {
      CTS_LOG_WARNING << "CharBuilder: cannot derive wire_length_unit from " << buffer_info.cell_master
                      << " because its physical height is unavailable";
      continue;
    }

    if (buffer_info.max_cap_pf >= strongest_cap_pf) {
      strongest_cap_pf = buffer_info.max_cap_pf;
      strongest_height_um = cell_height_um;
      strongest_master = buffer_info.cell_master;
    }
  }

  if (strongest_height_um <= 0.0) {
    CTS_LOG_WARNING << "CharBuilder: failed to resolve wire_length_unit from Config or strongest buffer height";
    return 0.0;
  }

  const double fallback_unit_um = strongest_height_um * 10.0;
  CTS_LOG_INFO << "CharBuilder: wire_length_unit resolved from strongest buffer " << strongest_master << " height = " << strongest_height_um
               << " um -> " << fallback_unit_um << " um";
  return fallback_unit_um;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

auto CharBuilder::init() -> void
{
  CTS_LOG_INFO << "CharBuilder: initialization started";

  _segment_chars.clear();
  _buffering_patterns.clear();
  _wire_length_stats.clear();
  _temp_inst_names.clear();
  _temp_net_names.clear();
  _source_inst_name.clear();
  _source_in_pin.clear();
  _sink_inst_name.clear();
  _source_out_pin.clear();
  _sink_in_pin.clear();
  _char_clock_name.clear();
  _next_pattern_id = 0U;
  _char_circuit_id = 0U;

  _sorted_buffers = collectSortedBuffers();
  _max_slew = resolveMaxSlew();
  _max_cap = resolveMaxCap();
  _length_unit_um = resolveWireLengthUnitUm(_sorted_buffers);
  _wire_length_iterations = CONFIG_INST.get_wire_length_iterations();
  _slew_steps = CONFIG_INST.get_slew_steps();
  _cap_steps = CONFIG_INST.get_cap_steps();
  _wire_lengths_um = makeWireLengths(_length_unit_um, _wire_length_iterations);
  _max_length = _wire_lengths_um.empty() ? 0.0 : _wire_lengths_um.back();
  _slews_to_test = makeUniformSweepSamples(_max_slew, _slew_steps);
  _loads_to_test = makeUniformSweepSamples(_max_cap, _cap_steps);
  _max_nodes = CONFIG_INST.get_max_pattern_nodes();
  _routing_layer = resolveRoutingLayer();
  _wire_width = resolveWireWidth();
  if (_max_nodes == 0U) {
    _max_nodes = kDefaultMaxPatternNodes;
    CTS_LOG_WARNING << "CharBuilder: max_pattern_nodes is 0, fallback to default = " << _max_nodes;
  }
  _sink_input_cap_pf = _sorted_buffers.empty() ? 0.0 : _sorted_buffers.front().input_cap_pf;

  CTS_LOG_INFO << "CharBuilder: max_pattern_nodes = " << _max_nodes;
  CTS_LOG_INFO << "CharBuilder: routing_layer = " << _routing_layer;
  CTS_LOG_INFO << "CharBuilder: wire_length_unit = " << _length_unit_um << " um, iterations = " << _wire_length_iterations
               << ", max = " << _max_length << " um";
  CTS_LOG_INFO << "CharBuilder: " << _slews_to_test.size() << " slew values to test"
               << " (steps=" << _slew_steps << ", max=" << _max_slew << " ns)";
  CTS_LOG_INFO << "CharBuilder: " << _loads_to_test.size() << " load values to test"
               << " (steps=" << _cap_steps << ", max=" << _max_cap << " pF)";
  CTS_LOG_INFO << "CharBuilder: sorted buffer list (" << _sorted_buffers.size() << " buffers):";
  for (std::size_t buffer_index = 0; buffer_index < _sorted_buffers.size(); ++buffer_index) {
    CTS_LOG_INFO << "  [" << buffer_index << "] " << _sorted_buffers.at(buffer_index).cell_master
                 << " max_cap=" << _sorted_buffers.at(buffer_index).max_cap_pf << " pF";
  }
  CTS_LOG_INFO << "CharBuilder: initialization complete -- " << _sorted_buffers.size() << " buffers, " << _wire_lengths_um.size()
               << " wire lengths, " << _slews_to_test.size() << " slews, " << _loads_to_test.size() << " loads";
}

auto CharBuilder::build() -> void
{
  CTS_LOG_INFO << "CharBuilder: build started";
  _wire_length_stats.clear();

  if (_sorted_buffers.empty()) {
    CTS_LOG_WARNING << "CharBuilder: no usable buffers remain after Config/liberty filtering, skip characterization build";
    STA_ADAPTER_INST.finishCharOnly();
    return;
  }
  if (_wire_lengths_um.empty()) {
    CTS_LOG_ERROR << "CharBuilder: no wire lengths to enumerate, aborting build";
    STA_ADAPTER_INST.finishCharOnly();
    return;
  }
  if (_slews_to_test.empty() || _loads_to_test.empty()) {
    CTS_LOG_WARNING << "CharBuilder: characterization limits are unresolved"
                    << " (max_slew_ns=" << _max_slew << ", max_cap_pf=" << _max_cap << "), skip characterization build";
    STA_ADAPTER_INST.finishCharOnly();
    return;
  }

  for (const double wire_length_um : _wire_lengths_um) {
    const unsigned lattice_slots = calcLatticeSlotCount(wire_length_um, _length_unit_um);
    const unsigned enumerated_slots = calcBufferSlotCount(wire_length_um);
    const std::size_t char_count_before = _segment_chars.size();
    const std::size_t pattern_count_before = _buffering_patterns.size();
    const auto start_time = std::chrono::steady_clock::now();
    const std::size_t estimated_patterns_per_wire_length = estimatePatternCountPerWireLength(wire_length_um);
    const std::size_t estimated_sta_samples_per_wire_length
        = estimated_patterns_per_wire_length * _loads_to_test.size() * _slews_to_test.size();
    WireLengthBuildStat build_stat;
    build_stat.wire_length_um = wire_length_um;
    build_stat.estimated_patterns = estimated_patterns_per_wire_length;
    build_stat.estimated_sta_samples = estimated_sta_samples_per_wire_length;
    CTS_LOG_INFO << "CharBuilder: wire_length=" << wire_length_um << " um estimated upper bound = " << estimated_patterns_per_wire_length
                 << " patterns, " << estimated_sta_samples_per_wire_length << " STA samples"
                 << " (lattice_slots=" << lattice_slots << ", enumerated_slots=" << enumerated_slots << ")";
    if (enumerated_slots < lattice_slots) {
      CTS_LOG_INFO << "CharBuilder: wire_length=" << wire_length_um << " um slot enumeration is capped by max_pattern_nodes=" << _max_nodes;
    }
    enumerateWireLength(wire_length_um, build_stat);

    build_stat.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    build_stat.added_segment_chars = _segment_chars.size() - char_count_before;
    build_stat.added_patterns = _buffering_patterns.size() - pattern_count_before;
    _wire_length_stats.push_back(build_stat);
    CTS_LOG_INFO << "CharBuilder: wire_length=" << wire_length_um << " um generated " << _wire_length_stats.back().added_segment_chars
                 << " segment chars, " << _wire_length_stats.back().added_patterns << " patterns in "
                 << _wire_length_stats.back().elapsed_ms << " ms"
                 << " (feasible_patterns=" << _wire_length_stats.back().feasible_patterns
                 << ", skipped_patterns=" << _wire_length_stats.back().skipped_patterns_infeasible
                 << ", executed_sta_samples=" << _wire_length_stats.back().executed_sta_samples
                 << ", skipped_sta_samples=" << _wire_length_stats.back().skipped_sta_samples << ")";
  }

  CTS_LOG_INFO << "CharBuilder: build complete -- " << _segment_chars.size() << " segment chars, " << _buffering_patterns.size()
               << " patterns";
  STA_ADAPTER_INST.finishCharOnly();
}

// ---------------------------------------------------------------------------
// Pattern enumeration
// ---------------------------------------------------------------------------

auto CharBuilder::calcBufferSlotCount(double wire_length_um) const -> unsigned
{
  auto slot_count = calcLatticeSlotCount(wire_length_um, _length_unit_um);

  if (_max_nodes > 0U) {
    slot_count = std::min(slot_count, _max_nodes);
  }
  if (slot_count > kMaxTopologySlots) {
    static bool has_logged_slot_clamp = false;
    if (!has_logged_slot_clamp) {
      CTS_LOG_WARNING << "CharBuilder: slot count exceeds topology bit capacity, clamp to " << kMaxTopologySlots;
      has_logged_slot_clamp = true;
    }
    slot_count = kMaxTopologySlots;
  }
  return slot_count;
}

auto CharBuilder::countSelectedSlots(TopologyBits topology_bits) -> unsigned
{
  unsigned slot_count = 0U;
  auto remaining_bits = topology_bits.value;
  while (remaining_bits != 0U) {
    slot_count += static_cast<unsigned>(remaining_bits & 1U);
    remaining_bits >>= 1U;
  }
  return slot_count;
}

auto CharBuilder::estimatePatternCountPerWireLength(double wire_length_um) const -> std::size_t
{
  const unsigned num_slots = calcBufferSlotCount(wire_length_um);
  CTS_LOG_FATAL_IF(num_slots >= std::numeric_limits<std::uint64_t>::digits)
      << "CharBuilder: buffer slot count " << num_slots << " exceeds topology bit capacity.";

  std::size_t total_patterns = 0;
  const std::uint64_t num_topologies = std::uint64_t{1} << num_slots;
  for (std::uint64_t topology_bits_value = 0; topology_bits_value < num_topologies; ++topology_bits_value) {
    const unsigned num_buffer_positions = countSelectedSlots(TopologyBits{topology_bits_value});
    total_patterns += (num_buffer_positions == 0U) ? 1U : getMonotonicComboCount(_sorted_buffers.size(), num_buffer_positions);
  }
  return total_patterns;
}

auto CharBuilder::enumerateWireLength(double wire_length_um, WireLengthBuildStat& build_stat) -> void
{
  const unsigned num_slots = calcBufferSlotCount(wire_length_um);
  CTS_LOG_FATAL_IF(num_slots >= std::numeric_limits<std::uint64_t>::digits)
      << "CharBuilder: buffer slot count " << num_slots << " exceeds topology bit capacity.";

  const std::uint64_t num_topologies = std::uint64_t{1} << num_slots;
  for (std::uint64_t topology_bits_value = 0; topology_bits_value < num_topologies; ++topology_bits_value) {
    enumerateTopology(wire_length_um, num_slots, TopologyBits{topology_bits_value}, build_stat);
  }
}

auto CharBuilder::enumerateTopology(double wire_length_um, unsigned num_slots, TopologyBits topology_bits, WireLengthBuildStat& build_stat)
    -> void
{
  const TopologyDesc topo = buildTopologyDesc(wire_length_um, num_slots, topology_bits);
  const std::size_t num_buf_positions = topo.buffer_positions.size();

  if (num_buf_positions == 0) {
    const std::vector<std::string> empty_masters;
    characterizeTopology(topo, empty_masters, build_stat);
    return;
  }

  const std::size_t num_buf_types = _sorted_buffers.size();
  if (num_buf_types == 0) {
    return;
  }

  std::vector<std::size_t> buf_indices(num_buf_positions, 0);
  while (true) {
    std::vector<std::string> buf_masters;
    buf_masters.reserve(num_buf_positions);
    for (const auto buffer_index : std::ranges::reverse_view(buf_indices)) {
      buf_masters.push_back(_sorted_buffers.at(buffer_index).cell_master);
    }

    characterizeTopology(topo, buf_masters, build_stat);
    if (!advanceToNextMonotonic(buf_indices, num_buf_types)) {
      break;
    }
  }
}

auto CharBuilder::getMonotonicComboCount(std::size_t num_buf_types, std::size_t num_positions) -> std::size_t
{
  if (num_buf_types == 0 || num_positions == 0) {
    return 0;
  }
  const std::size_t combination_n = num_buf_types + num_positions - 1;
  std::size_t combination_k = num_positions;
  combination_k = std::min(combination_k, combination_n - combination_k);
  std::size_t result = 1;
  for (std::size_t index = 0; index < combination_k; ++index) {
    result = result * (combination_n - index) / (index + 1);
  }
  return result;
}

auto CharBuilder::advanceToNextMonotonic(std::vector<std::size_t>& buf_indices, std::size_t num_buf_types) -> bool
{
  if (buf_indices.empty() || num_buf_types == 0) {
    return false;
  }

  int position = static_cast<int>(buf_indices.size()) - 1;

  while (position >= 0) {
    const auto index = static_cast<std::size_t>(position);
    if (buf_indices.at(index) + 1 < num_buf_types) {
      ++buf_indices.at(index);
      for (std::size_t tail_index = index + 1; tail_index < buf_indices.size(); ++tail_index) {
        buf_indices.at(tail_index) = buf_indices.at(index);
      }
      return true;
    }
    --position;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Topology construction
// ---------------------------------------------------------------------------

auto CharBuilder::buildTopologyDesc(double wire_length_um, unsigned num_slots, TopologyBits topology_bits) const -> TopologyDesc
{
  TopologyDesc desc;

  if (num_slots == 0U || _length_unit_um <= 0.0) {
    desc.wire_segments_um.push_back(wire_length_um);
    return desc;
  }

  // Slots are pinned to the global wire-length lattice. If slot enumeration is
  // capped by max_pattern_nodes, the remaining suffix stays in the terminal segment.
  double previous_boundary_um = 0.0;

  for (unsigned slot_index = 0; slot_index < num_slots; ++slot_index) {
    const double slot_boundary_um = std::min((static_cast<double>(slot_index) + 1.0) * _length_unit_um, wire_length_um);
    if (((topology_bits.value >> slot_index) & kTopologyPresentMask) != 0U) {
      desc.buffer_positions.push_back(slot_index);
      desc.wire_segments_um.push_back(slot_boundary_um - previous_boundary_um);
      previous_boundary_um = slot_boundary_um;
    }
  }

  // Keep terminal segment so createCharCircuit always sees buffer_count + 1 nets.
  desc.wire_segments_um.push_back(std::max(0.0, wire_length_um - previous_boundary_um));

  return desc;
}

// ---------------------------------------------------------------------------
// Characterization measurement
// ---------------------------------------------------------------------------

auto CharBuilder::findBufferInfo(const std::string& cell_master) const -> const CharBufferInfo*
{
  auto it = std::ranges::find_if(_sorted_buffers,
                                 [&cell_master](const CharBufferInfo& info) -> bool { return info.cell_master == cell_master; });
  return it == _sorted_buffers.end() ? nullptr : &(*it);
}

auto CharBuilder::analyzePatternFeasibility(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) const
    -> PatternFeasibility
{
  if (_sorted_buffers.empty()) {
    return {};
  }

  // Intermediate stages must absorb wire plus next-buffer input cap locally.
  // Only the terminal stage contributes residual budget for external load sweep.
  const auto* source_buffer = &_sorted_buffers.back();
  const auto* sink_buffer = &_sorted_buffers.front();
  double max_external_load_pf = std::numeric_limits<double>::infinity();

  for (std::size_t segment_index = 0; segment_index < topo.wire_segments_um.size(); ++segment_index) {
    const CharBufferInfo* driver_buffer = nullptr;
    if (segment_index == 0U) {
      driver_buffer = source_buffer;
    } else {
      driver_buffer = findBufferInfo(buf_masters.at(segment_index - 1U));
    }
    if (driver_buffer == nullptr || driver_buffer->max_cap_pf <= 0.0) {
      return {};
    }

    const double wire_cap_pf = STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, topo.wire_segments_um.at(segment_index), _wire_width);
    const bool is_last_segment = (segment_index + 1U == topo.wire_segments_um.size());
    const CharBufferInfo* next_buffer = is_last_segment ? sink_buffer : findBufferInfo(buf_masters.at(segment_index));
    const double next_input_cap_pf = next_buffer != nullptr ? next_buffer->input_cap_pf : 0.0;
    if (!is_last_segment && next_input_cap_pf <= 0.0) {
      return {};
    }

    const double static_stage_cap_pf = wire_cap_pf + next_input_cap_pf;
    if (!is_last_segment && static_stage_cap_pf > driver_buffer->max_cap_pf + kCapFeasibilityEpsilonPf) {
      return {};
    }
    if (is_last_segment) {
      max_external_load_pf = std::min(max_external_load_pf, driver_buffer->max_cap_pf - static_stage_cap_pf);
    }
  }

  if (!std::isfinite(max_external_load_pf)) {
    return PatternFeasibility{.is_pattern_feasible = true, .max_load_pf = std::numeric_limits<double>::infinity()};
  }
  if (max_external_load_pf + kCapFeasibilityEpsilonPf < 0.0) {
    return {};
  }
  return PatternFeasibility{.is_pattern_feasible = true, .max_load_pf = max_external_load_pf};
}

auto CharBuilder::characterizeTopology(const TopologyDesc& topo, const std::vector<std::string>& buf_masters,
                                       WireLengthBuildStat& build_stat) -> void
{
  ++build_stat.evaluated_patterns;

  double total_length_um = 0.0;
  for (const double seg_len : topo.wire_segments_um) {
    total_length_um += seg_len;
  }

  std::vector<double> buffer_positions_norm;
  if (!topo.buffer_positions.empty() && total_length_um > 0.0) {
    double cumulative_um = 0.0;
    size_t buf_idx = 0;
    for (size_t seg = 0; seg < topo.wire_segments_um.size() && buf_idx < topo.buffer_positions.size(); ++seg) {
      cumulative_um += topo.wire_segments_um.at(seg);
      if (seg < topo.wire_segments_um.size() - 1) {
        const double normalized = cumulative_um / total_length_um;
        buffer_positions_norm.push_back(normalized);
        ++buf_idx;
      }
    }
  }

  const unsigned length_idx = discretize(total_length_um, _max_length, _wire_length_iterations);

  const PatternId pid = PatternId::segment(_next_pattern_id);
  BufferingPattern pattern(length_idx, pid, buffer_positions_norm, buf_masters);
  _buffering_patterns.push_back(std::move(pattern));

  const PatternFeasibility feasibility = analyzePatternFeasibility(topo, buf_masters);
  if (!feasibility.is_pattern_feasible) {
    ++build_stat.skipped_patterns_infeasible;
    if ((build_stat.evaluated_patterns % kCharProgressLogStride) == 0U) {
      CTS_LOG_INFO << "CharBuilder: wire_length=" << total_length_um << " um progress " << build_stat.evaluated_patterns << "/"
                   << build_stat.estimated_patterns << " patterns"
                   << " (feasible=" << build_stat.feasible_patterns << ", skipped=" << build_stat.skipped_patterns_infeasible
                   << ", executed_sta_samples=" << build_stat.executed_sta_samples << ")";
    }
    ++_next_pattern_id;
    return;
  }
  ++build_stat.feasible_patterns;

  createCharCircuit(topo, buf_masters);
  STA_ADAPTER_INST.createCharClock(_source_out_pin, _char_clock_name, kCharClockPeriodNs);

  std::vector<std::string> power_inst_names = _temp_inst_names;
  power_inst_names.push_back(_source_inst_name);
  power_inst_names.push_back(_sink_inst_name);
  bool power_context_attempted = false;
  bool power_context_ready = false;
  if (kEnableCharPowerSampling) {
    power_context_attempted = true;
    power_context_ready = STA_ADAPTER_INST.prepareCharPower(power_inst_names, _temp_net_names, _source_in_pin);
    if (!power_context_ready) {
      CTS_LOG_WARNING << "CharBuilder: iPA characterization power is unavailable for this topology; affected samples use zero power.";
    }
  }

  for (const double load_pf : _loads_to_test) {
    if (load_pf > feasibility.max_load_pf + kCapFeasibilityEpsilonPf) {
      ++build_stat.skipped_load_points;
      build_stat.skipped_sta_samples += _slews_to_test.size();
      continue;
    }

    const double effective_load = load_pf - _sink_input_cap_pf;
    if (effective_load < 0.0) {
      ++build_stat.skipped_load_points;
      build_stat.skipped_sta_samples += _slews_to_test.size();
      continue;
    }

    setCharParasitics(topo, effective_load);

    for (const double input_slew_ns : _slews_to_test) {
      // Keep the injected char clock stable and only clear transient timing data.
      STA_ADAPTER_INST.prepareCharTiming();

      // The source clock is rooted at the buffer output to exclude source-cell delay,
      // but the output slew/current must still be derived from the source buffer arc.
      STA_ADAPTER_INST.setCharBufferInputSlew(_source_in_pin, _source_out_pin, input_slew_ns);

      STA_ADAPTER_INST.updateCharTiming();
      ++build_stat.executed_sta_samples;

      const double delay_ns = STA_ADAPTER_INST.queryCharClockAT(_sink_in_pin, _char_clock_name);
      const double output_slew_ns = STA_ADAPTER_INST.queryCharSlew(_sink_in_pin);

      double power_w = 0.0;
      if (kEnableCharPowerSampling) {
        if (power_context_ready) {
          if (STA_ADAPTER_INST.updateCharPower()) {
            power_w = STA_ADAPTER_INST.queryCharPower();
          } else {
            CTS_LOG_WARNING << "CharBuilder: iPA characterization update failed, remaining samples for this topology use zero power.";
            power_context_ready = false;
            STA_ADAPTER_INST.destroyCharPower();
          }
        }
      }

      double input_cap_pf = 0.0;
      if (!buf_masters.empty()) {
        input_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(buf_masters.front());
        input_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, topo.wire_segments_um.front(), _wire_width);
      } else {
        input_cap_pf = load_pf;
        for (const double seg_len_um : topo.wire_segments_um) {
          input_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);
        }
      }

      const unsigned input_slew_idx = discretize(input_slew_ns, _max_slew, _slew_steps);
      const unsigned output_slew_idx = discretize(output_slew_ns, _max_slew, _slew_steps);
      const unsigned driven_cap_idx = discretize(input_cap_pf, _max_cap, _cap_steps);
      const unsigned load_cap_idx = discretize(load_pf, _max_cap, _cap_steps);

      const CharCore core(input_slew_idx, output_slew_idx, driven_cap_idx, load_cap_idx, delay_ns, power_w, pid);
      const SegmentChar seg_char(core, length_idx);
      _segment_chars.push_back(seg_char);
    }
  }

  if (kEnableCharPowerSampling && power_context_attempted) {
    STA_ADAPTER_INST.destroyCharPower();
  }
  STA_ADAPTER_INST.destroyCharClock();

  destroyCharCircuit();
  if ((build_stat.evaluated_patterns % kCharProgressLogStride) == 0U) {
    CTS_LOG_INFO << "CharBuilder: wire_length=" << total_length_um << " um progress " << build_stat.evaluated_patterns << "/"
                 << build_stat.estimated_patterns << " patterns"
                 << " (feasible=" << build_stat.feasible_patterns << ", skipped=" << build_stat.skipped_patterns_infeasible
                 << ", executed_sta_samples=" << build_stat.executed_sta_samples
                 << ", skipped_sta_samples=" << build_stat.skipped_sta_samples << ")";
  }
  ++_next_pattern_id;
}

// ---------------------------------------------------------------------------
// Temporary circuit management
// ---------------------------------------------------------------------------

auto CharBuilder::createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) -> void
{
  _temp_inst_names.clear();
  _temp_net_names.clear();

  const std::string id_prefix = "cts_char_" + std::to_string(_char_circuit_id) + "_";

  // Keep the fixture chain source-to-sink non-increasing even when the pattern is empty.
  const auto& source_buf = _sorted_buffers.back();
  const auto& sink_buf = _sorted_buffers.front();

  _source_inst_name = id_prefix + "source";
  STA_ADAPTER_INST.createCharInstance(source_buf.cell_master, _source_inst_name);
  _source_in_pin = _source_inst_name + "/" + source_buf.input_pin;
  _source_out_pin = _source_inst_name + "/" + source_buf.output_pin;

  _sink_inst_name = id_prefix + "sink";
  STA_ADAPTER_INST.createCharInstance(sink_buf.cell_master, _sink_inst_name);
  _sink_in_pin = _sink_inst_name + "/" + sink_buf.input_pin;

  for (size_t i = 0; i < buf_masters.size(); ++i) {
    const std::string inst_name = id_prefix + "buf_" + std::to_string(i);
    STA_ADAPTER_INST.createCharInstance(buf_masters.at(i), inst_name);
    _temp_inst_names.push_back(inst_name);
  }

  for (size_t i = 0; i < topo.wire_segments_um.size(); ++i) {
    const std::string net_name = id_prefix + "net_" + std::to_string(i);
    STA_ADAPTER_INST.createCharNet(net_name);
    _temp_net_names.push_back(net_name);
  }

  STA_ADAPTER_INST.attachCharPin(_source_inst_name, source_buf.output_pin, _temp_net_names.front());

  for (size_t bi = 0; bi < buf_masters.size(); ++bi) {
    const CharBufferInfo* buf_info = findBufferInfo(buf_masters.at(bi));
    if (buf_info == nullptr) {
      CTS_LOG_FATAL << "Buffer info not found for: " << buf_masters.at(bi);
      return;
    }
    const auto& buffer_info = *buf_info;

    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.input_pin, _temp_net_names.at(bi));
    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.output_pin, _temp_net_names.at(bi + 1));
  }

  STA_ADAPTER_INST.attachCharPin(_sink_inst_name, sink_buf.input_pin, _temp_net_names.back());

  for (const auto& net_name : _temp_net_names) {
    STA_ADAPTER_INST.buildCharNetGraph(net_name);
  }

  _char_clock_name = id_prefix + "clk";

  ++_char_circuit_id;
}

auto CharBuilder::setCharParasitics(const TopologyDesc& topo, double load_pf) -> void
{
  for (size_t i = 0; i < _temp_net_names.size(); ++i) {
    const double seg_len_um = topo.wire_segments_um.at(i);
    const double wire_res = STA_ADAPTER_INST.queryWireResistance(_routing_layer, seg_len_um, _wire_width);
    const double wire_cap = STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);

    const double seg_load = (i == _temp_net_names.size() - 1) ? load_pf : 0.0;
    STAAdapter::CharRcTreeConfig rc_tree_config;
    rc_tree_config.wire_res = wire_res;
    rc_tree_config.wire_cap = wire_cap;
    rc_tree_config.load_cap = seg_load;
    STA_ADAPTER_INST.buildCharRcTree(_temp_net_names.at(i), rc_tree_config);
  }
}

auto CharBuilder::destroyCharCircuit() -> void
{
  for (const auto& net_name : std::ranges::reverse_view(_temp_net_names)) {
    STA_ADAPTER_INST.destroyCharNet(net_name);
  }
  for (const auto& inst_name : std::ranges::reverse_view(_temp_inst_names)) {
    STA_ADAPTER_INST.destroyCharInstance(inst_name);
  }
  if (!_sink_inst_name.empty()) {
    STA_ADAPTER_INST.destroyCharInstance(_sink_inst_name);
    _sink_inst_name.clear();
  }
  if (!_source_inst_name.empty()) {
    STA_ADAPTER_INST.destroyCharInstance(_source_inst_name);
    _source_inst_name.clear();
  }
  _temp_net_names.clear();
  _temp_inst_names.clear();
  _source_in_pin.clear();
  _source_out_pin.clear();
  _sink_in_pin.clear();
}

// ---------------------------------------------------------------------------
// Discretization: physical value -> bin index in [1, steps]
// ---------------------------------------------------------------------------

auto CharBuilder::discretize(double value, double max_value, unsigned steps) -> unsigned
{
  if (max_value <= 0.0 || steps == 0 || value <= 0.0) {
    return 0;
  }
  const double step_size = max_value / steps;
  return static_cast<unsigned>(std::ceil(value / step_size));
}

}  // namespace icts
