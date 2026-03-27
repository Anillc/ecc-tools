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
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
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

constexpr unsigned kDefaultMaxNodes = 4U;
constexpr unsigned kTopologyPresentMask = 1U;
constexpr double kPercentFactor = 100.0;
constexpr double kCharClockPeriodNs = 10.0;
constexpr double kZeroPowerW = 0.0;

}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void CharBuilder::init()
{
  CTS_LOG_INFO << "CharBuilder: initialization started";
  initBufferList();
  initCharParams();
  CTS_LOG_INFO << "CharBuilder: initialization complete -- " << _sorted_buffers.size() << " buffers, " << _wire_lengths_um.size()
               << " wire lengths, " << _slews_to_test.size() << " slews, " << _loads_to_test.size() << " loads";
}

void CharBuilder::build()
{
  CTS_LOG_INFO << "CharBuilder: build started";

  if (_sorted_buffers.empty()) {
    CTS_LOG_WARNING << "CharBuilder: no buffers configured, only wire-only patterns will be generated";
  }
  if (_wire_lengths_um.empty()) {
    CTS_LOG_ERROR << "CharBuilder: no wire lengths to enumerate, aborting build";
    return;
  }

  for (const double wire_length_um : _wire_lengths_um) {
    enumerateWireLength(wire_length_um);
  }

  CTS_LOG_INFO << "CharBuilder: build complete -- " << _segment_chars.size() << " segment chars, " << _buffering_patterns.size()
               << " patterns";
}

// ---------------------------------------------------------------------------
// Initialization helpers
// ---------------------------------------------------------------------------

void CharBuilder::initBufferList()
{
  _sorted_buffers.clear();
  const auto& buffer_types = CONFIG_INST.get_buffer_types();
  if (buffer_types.empty()) {
    CTS_LOG_WARNING << "CharBuilder: no buffer types configured in Config";
    return;
  }

  // Collect buffer info from liberty via CTSAPI
  for (const auto& cell_master : buffer_types) {
    const double max_cap = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
    if (max_cap <= 0.0) {
      CTS_LOG_WARNING << "CharBuilder: buffer " << cell_master << " has invalid max_cap (" << max_cap << " pF), skipped";
      continue;
    }

    // Query input pin capacitance via CTSAPI
    const double input_cap = STA_ADAPTER_INST.queryCharInputPinCap(cell_master);

    // Resolve buffer port names from liberty
    auto [in_pin, out_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    if (in_pin.empty() || out_pin.empty()) {
      CTS_LOG_WARNING << "CharBuilder: buffer " << cell_master << " has unresolved port names, skipped";
      continue;
    }

    BufferInfo info;
    info.cell_master = cell_master;
    info.max_cap_pf = max_cap;
    info.input_cap_pf = input_cap;
    info.input_pin = in_pin;
    info.output_pin = out_pin;
    _sorted_buffers.push_back(std::move(info));
  }

  // Sort ascending by max_cap (proxy for drive strength)
  std::ranges::sort(_sorted_buffers, [](const BufferInfo& lhs_buffer_info, const BufferInfo& rhs_buffer_info) -> bool {
    return lhs_buffer_info.max_cap_pf < rhs_buffer_info.max_cap_pf;
  });

  // Near-neighbor redundancy removal (default disabled: _redundancy_pct == 0)
  _redundancy_pct = CONFIG_INST.get_char_buf_redundancy_pct();
  if (_redundancy_pct > 0.0 && _sorted_buffers.size() > 1) {
    std::vector<BufferInfo> filtered;
    filtered.push_back(_sorted_buffers.front());
    for (size_t i = 1; i < _sorted_buffers.size(); ++i) {
      const double prev_cap = filtered.back().max_cap_pf;
      const double curr_cap = _sorted_buffers.at(i).max_cap_pf;
      // Keep if the gap exceeds the threshold
      if (prev_cap <= 0.0 || (curr_cap - prev_cap) / prev_cap >= _redundancy_pct) {
        filtered.push_back(_sorted_buffers.at(i));
      } else {
        CTS_LOG_INFO << "CharBuilder: removed near-neighbor buffer " << _sorted_buffers.at(i).cell_master << " (max_cap=" << curr_cap
                     << " pF, gap=" << ((curr_cap - prev_cap) / prev_cap * kPercentFactor) << "%)";
      }
    }
    _sorted_buffers = std::move(filtered);
  }

  // Log the final sorted buffer list
  CTS_LOG_INFO << "CharBuilder: sorted buffer list (" << _sorted_buffers.size() << " buffers):";
  for (size_t i = 0; i < _sorted_buffers.size(); ++i) {
    CTS_LOG_INFO << "  [" << i << "] " << _sorted_buffers.at(i).cell_master << " max_cap=" << _sorted_buffers.at(i).max_cap_pf << " pF";
  }
}

void CharBuilder::initCharParams()
{
  // Max pattern nodes
  _max_nodes = CONFIG_INST.get_max_pattern_nodes();
  if (_max_nodes == 0) {
    _max_nodes = kDefaultMaxNodes;
  }
  CTS_LOG_INFO << "CharBuilder: max_pattern_nodes = " << _max_nodes;

  // Routing layer
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (!routing_layers.empty()) {
    _routing_layer = static_cast<int>(routing_layers.front());
  }
  CTS_LOG_INFO << "CharBuilder: routing_layer = " << _routing_layer;

  // Wire width
  {
    double wire_width = CONFIG_INST.get_wire_width();
    _wire_width = wire_width > 0.0 ? std::optional<double>(wire_width) : std::nullopt;
  }

  // Max values for discretization ranges
  _max_slew = CONFIG_INST.get_max_buf_tran();
  _max_cap = CONFIG_INST.get_max_cap();
  _max_length = CONFIG_INST.get_max_length();

  // Discretization steps
  _slew_steps = CONFIG_INST.get_slew_steps();
  _cap_steps = CONFIG_INST.get_cap_steps();
  _length_steps = CONFIG_INST.get_length_steps();

  // Compute wire lengths to test (in um)
  _wire_lengths_um.clear();
  if (_max_length > 0.0 && _length_steps > 0) {
    const double length_step_um = _max_length / _length_steps;
    for (unsigned i = 1; i <= _length_steps; ++i) {
      _wire_lengths_um.push_back(i * length_step_um);
    }
  }
  CTS_LOG_INFO << "CharBuilder: " << _wire_lengths_um.size() << " wire lengths to test"
               << " (steps=" << _length_steps << ", max=" << _max_length << " um)";

  // Compute input slews to test (in ns)
  _slews_to_test.clear();
  if (_max_slew > 0.0 && _slew_steps > 0) {
    const double slew_step_ns = _max_slew / _slew_steps;
    for (unsigned i = 1; i <= _slew_steps; ++i) {
      _slews_to_test.push_back(i * slew_step_ns);
    }
  }
  CTS_LOG_INFO << "CharBuilder: " << _slews_to_test.size() << " slew values to test"
               << " (steps=" << _slew_steps << ", max=" << _max_slew << " ns)";

  // Compute output loads to test (in pF)
  _loads_to_test.clear();
  if (_max_cap > 0.0 && _cap_steps > 0) {
    const double cap_step_pf = _max_cap / _cap_steps;
    for (unsigned i = 1; i <= _cap_steps; ++i) {
      _loads_to_test.push_back(i * cap_step_pf);
    }
  }
  CTS_LOG_INFO << "CharBuilder: " << _loads_to_test.size() << " load values to test"
               << " (steps=" << _cap_steps << ", max=" << _max_cap << " pF)";

  // Precompute sink buffer input capacitance (use the largest-drive buffer as sink)
  if (!_sorted_buffers.empty()) {
    _sink_input_cap_pf = _sorted_buffers.back().input_cap_pf;
  }
}

// ---------------------------------------------------------------------------
// Pattern enumeration
// ---------------------------------------------------------------------------

void CharBuilder::enumerateWireLength(double wire_length_um)
{
  for (unsigned num_nodes = 0; num_nodes < _max_nodes; ++num_nodes) {
    const unsigned num_topologies = 1U << num_nodes;
    for (unsigned topology_bits_value = 0; topology_bits_value < num_topologies; ++topology_bits_value) {
      enumerateTopology(wire_length_um, num_nodes, TopologyBits{topology_bits_value});
    }
  }
}

void CharBuilder::enumerateTopology(double wire_length_um, unsigned num_nodes, TopologyBits topology_bits)
{
  const TopologyDesc topo = buildTopologyDesc(wire_length_um, num_nodes, topology_bits);
  const std::size_t num_buf_positions = topo.buffer_positions.size();

  if (num_buf_positions == 0) {
    // Pure wire topology -- no buffers to assign
    const std::vector<std::string> empty_masters;
    characterizeTopology(topo, empty_masters);
    return;
  }

  // Enumerate all monotonic buffer combinations
  const std::size_t num_buf_types = _sorted_buffers.size();
  if (num_buf_types == 0) {
    return;
  }

  std::vector<std::size_t> buf_indices(num_buf_positions, 0);
  while (true) {
    std::vector<std::string> buf_masters;
    buf_masters.reserve(num_buf_positions);
    for (const std::size_t buffer_index : buf_indices) {
      buf_masters.push_back(_sorted_buffers.at(buffer_index).cell_master);
    }

    characterizeTopology(topo, buf_masters);
    if (!advanceToNextMonotonic(buf_indices, num_buf_types)) {
      break;
    }
  }
}

auto CharBuilder::isMonotonic(const std::vector<std::size_t>& buf_indices) -> bool
{
  for (std::size_t index = 1; index < buf_indices.size(); ++index) {
    if (buf_indices.at(index) < buf_indices.at(index - 1)) {
      return false;
    }
  }
  return true;
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

auto CharBuilder::buildTopologyDesc(double wire_length_um, unsigned num_nodes, TopologyBits topology_bits) -> TopologyDesc
{
  TopologyDesc desc;

  // Identify buffer positions from topology_bits
  for (unsigned node_index = 0; node_index < num_nodes; ++node_index) {
    if (((topology_bits.value >> node_index) & kTopologyPresentMask) != 0U) {
      desc.buffer_positions.push_back(node_index);
    }
  }

  // Variable wire segment lengths based on consecutive non-buffer positions.
  // Each segment spans from the previous boundary (input or buffer) to the next
  // boundary (buffer or output). The segment length is proportional to the number
  // of consecutive non-buffer node positions it spans.
  //
  // Wire segment unit = wire_length_um / (num_nodes + 1)
  // Each segment's length = (positions_in_segment) * unit
  const double unit_um = (num_nodes > 0) ? wire_length_um / (num_nodes + 1) : wire_length_um;

  unsigned nodes_without_buf = 0;
  std::size_t buffer_position_index = 0;

  for (unsigned node_index = 0; node_index < num_nodes; ++node_index) {
    ++nodes_without_buf;
    if (buffer_position_index < desc.buffer_positions.size() && desc.buffer_positions.at(buffer_position_index) == node_index) {
      // Buffer at this position: emit the wire segment before it
      desc.wire_segments_um.push_back(nodes_without_buf * unit_um);
      nodes_without_buf = 0;
      ++buffer_position_index;
    }
  }
  // Final segment: from last buffer (or input) to output
  ++nodes_without_buf;  // account for the output boundary
  desc.wire_segments_um.push_back(nodes_without_buf * unit_um);

  return desc;
}

// ---------------------------------------------------------------------------
// Characterization measurement
// ---------------------------------------------------------------------------

void CharBuilder::characterizeTopology(const TopologyDesc& topo, const std::vector<std::string>& buf_masters)
{
  // Compute total wire length (um)
  double total_length_um = 0.0;
  for (const double seg_len : topo.wire_segments_um) {
    total_length_um += seg_len;
  }

  // Compute normalized buffer positions in (0, 1]
  std::vector<double> buffer_positions_norm;
  if (!topo.buffer_positions.empty() && total_length_um > 0.0) {
    double cumulative_um = 0.0;
    size_t buf_idx = 0;
    for (size_t seg = 0; seg < topo.wire_segments_um.size() && buf_idx < topo.buffer_positions.size(); ++seg) {
      cumulative_um += topo.wire_segments_um.at(seg);
      if (seg < topo.wire_segments_um.size() - 1) {
        // A buffer exists at the boundary after this segment
        const double normalized = cumulative_um / total_length_um;
        buffer_positions_norm.push_back(normalized);
        ++buf_idx;
      }
    }
  }

  // Discretize length for BufferingPattern
  const unsigned length_idx = discretize(total_length_um, _max_length, _length_steps);

  const PatternId pid = PatternId::segment(_next_pattern_id);
  BufferingPattern pattern(length_idx, pid, buffer_positions_norm, buf_masters);
  _buffering_patterns.push_back(std::move(pattern));

  // Build the circuit once per topology+buffer combo (shared across load/slew sweeps)
  createCharCircuit(topo, buf_masters);

  // Sweep over (load, slew) combinations
  for (const double load_pf : _loads_to_test) {
    // Skip loads that are smaller than the sink's intrinsic input cap
    const double effective_load = load_pf - _sink_input_cap_pf;
    if (effective_load < 0.0) {
      continue;
    }

    // Build RC trees for this load value (rebuild parasitics for each load)
    setCharParasitics(topo, effective_load);

    // Create a propagated clock on the source buffer's output pin
    STA_ADAPTER_INST.createCharClock(_source_out_pin, _char_clock_name, kCharClockPeriodNs);

    for (const double input_slew_ns : _slews_to_test) {
      // Annotate input slew on the source buffer's output vertex
      STA_ADAPTER_INST.setCharInputSlew(_source_out_pin, input_slew_ns);

      // Full timing update (clock + slew + delay propagation)
      STA_ADAPTER_INST.updateTiming();

      // Query total delay via clock arrival time at sink input pin
      const double delay_ns = STA_ADAPTER_INST.queryCharClockAT(_sink_in_pin, _char_clock_name);

      // Query output slew at sink input pin
      const double output_slew_ns = STA_ADAPTER_INST.queryCharSlew(_sink_in_pin);

      // Power: iSTA has no per-instance power API; deferred to iPA integration
      const double power_w = kZeroPowerW;

      // Compute input capacitance (analytical, matching reference)
      double input_cap_pf = 0.0;
      if (!buf_masters.empty()) {
        // Buffered: first buffer input cap + first wire segment cap
        input_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(buf_masters.front());
        input_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, topo.wire_segments_um.front(), _wire_width);
      } else {
        // Pure wire: load + total wire cap
        input_cap_pf = load_pf;
        for (const double seg_len_um : topo.wire_segments_um) {
          input_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);
        }
      }

      // Discretize values to bin indices in [1, *_steps]
      const unsigned input_slew_idx = discretize(input_slew_ns, _max_slew, _slew_steps);
      const unsigned output_slew_idx = discretize(output_slew_ns, _max_slew, _slew_steps);
      const unsigned driven_cap_idx = discretize(input_cap_pf, _max_cap, _cap_steps);
      const unsigned load_cap_idx = discretize(load_pf, _max_cap, _cap_steps);

      // Create CharCore and SegmentChar
      const CharCore core(input_slew_idx, output_slew_idx, driven_cap_idx, load_cap_idx, delay_ns, power_w, pid);
      const SegmentChar seg_char(core, length_idx);
      _segment_chars.push_back(seg_char);
    }

    // Remove the clock definition and restore original SDC for next iteration
    STA_ADAPTER_INST.destroyCharClock();
  }

  // Cleanup temporary circuit
  destroyCharCircuit();
  ++_next_pattern_id;
}

// ---------------------------------------------------------------------------
// Temporary circuit management
// ---------------------------------------------------------------------------

void CharBuilder::createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters)
{
  _temp_inst_names.clear();
  _temp_net_names.clear();

  const std::string id_prefix = "cts_char_" + std::to_string(_char_circuit_id) + "_";

  // Use the largest-drive buffer for source and sink
  const auto& endpoint_buf = _sorted_buffers.back();

  // Create source buffer instance
  _source_inst_name = id_prefix + "source";
  STA_ADAPTER_INST.createCharInstance(endpoint_buf.cell_master, _source_inst_name);
  _source_out_pin = _source_inst_name + "/" + endpoint_buf.output_pin;

  // Create sink buffer instance
  _sink_inst_name = id_prefix + "sink";
  STA_ADAPTER_INST.createCharInstance(endpoint_buf.cell_master, _sink_inst_name);
  _sink_in_pin = _sink_inst_name + "/" + endpoint_buf.input_pin;

  // Create characterization buffer instances
  for (size_t i = 0; i < buf_masters.size(); ++i) {
    const std::string inst_name = id_prefix + "buf_" + std::to_string(i);
    STA_ADAPTER_INST.createCharInstance(buf_masters.at(i), inst_name);
    _temp_inst_names.push_back(inst_name);
  }

  // Create wire nets: one per wire segment
  // wire_segments_um has (num_buffers + 1) segments:
  //   net_0: source_out -> first_buf_in (or sink_in if pure wire)
  //   net_i: buf_{i-1}_out -> buf_i_in
  //   net_N: last_buf_out -> sink_in
  for (size_t i = 0; i < topo.wire_segments_um.size(); ++i) {
    const std::string net_name = id_prefix + "net_" + std::to_string(i);
    STA_ADAPTER_INST.createCharNet(net_name);
    _temp_net_names.push_back(net_name);
  }

  // Connect the chain: source_out -> net_0 -> [buf_0_in / buf_0_out] -> net_1 -> ... -> sink_in
  // First net: source output drives it
  STA_ADAPTER_INST.attachCharPin(_source_inst_name, endpoint_buf.output_pin, _temp_net_names.front());

  // Connect characterization buffers between nets
  for (size_t bi = 0; bi < buf_masters.size(); ++bi) {
    // Find the buffer info for port names
    const BufferInfo* buf_info = nullptr;
    for (const auto& info : _sorted_buffers) {
      if (info.cell_master == buf_masters.at(bi)) {
        buf_info = &info;
        break;
      }
    }
    if (buf_info == nullptr) {
      CTS_LOG_FATAL << "Buffer info not found for: " << buf_masters.at(bi);
      return;
    }
    const auto& buffer_info = *buf_info;

    // Buffer input connects to net[bi] (the net coming into this buffer)
    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.input_pin, _temp_net_names.at(bi));
    // Buffer output connects to net[bi+1] (the net going out of this buffer)
    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.output_pin, _temp_net_names.at(bi + 1));
  }

  // Last net: sink input receives it
  STA_ADAPTER_INST.attachCharPin(_sink_inst_name, endpoint_buf.input_pin, _temp_net_names.back());

  // Set the clock name for this circuit
  _char_clock_name = id_prefix + "clk";

  ++_char_circuit_id;
}

void CharBuilder::setCharParasitics(const TopologyDesc& topo, double load_pf)
{
  // Build Pi-model RC tree for each wire segment
  for (size_t i = 0; i < _temp_net_names.size(); ++i) {
    const double seg_len_um = topo.wire_segments_um.at(i);
    const double wire_res = STA_ADAPTER_INST.queryWireResistance(_routing_layer, seg_len_um, _wire_width);
    const double wire_cap = STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);

    // Add external load to the last segment's far-end capacitance
    const double seg_load = (i == _temp_net_names.size() - 1) ? load_pf : 0.0;
    STAAdapter::CharRcTreeConfig rc_tree_config;
    rc_tree_config.wire_res = wire_res;
    rc_tree_config.wire_cap = wire_cap;
    rc_tree_config.load_cap = seg_load;
    STA_ADAPTER_INST.buildCharRcTree(_temp_net_names.at(i), rc_tree_config);
  }
}

void CharBuilder::destroyCharCircuit()
{
  // Cleanup in reverse order: nets first, then characterization buffers, then source/sink
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
