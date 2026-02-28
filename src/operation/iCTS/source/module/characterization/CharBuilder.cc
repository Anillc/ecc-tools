// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
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
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "CTSAPI.hh"
#include "database/config/Config.hh"
#include "utils/logger/Logger.hh"

namespace icts {

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

  for (double wire_length_um : _wire_lengths_um) {
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
  const auto& buffer_types = CTSConfigInst.get_buffer_types();
  if (buffer_types.empty()) {
    CTS_LOG_WARNING << "CharBuilder: no buffer types configured in Config";
    return;
  }

  // Collect buffer info from liberty via CTSAPI
  for (const auto& cell_master : buffer_types) {
    double max_cap = CTSAPIInst.queryCellOutPinCapLimit(cell_master);
    if (max_cap <= 0.0) {
      CTS_LOG_WARNING << "CharBuilder: buffer " << cell_master << " has invalid max_cap (" << max_cap << " pF), skipped";
      continue;
    }

    // Query input pin capacitance via CTSAPI
    double input_cap = CTSAPIInst.queryCharInputPinCap(cell_master);

    // Resolve buffer port names from liberty
    auto [in_pin, out_pin] = CTSAPIInst.queryBufferPorts(cell_master);
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
  std::ranges::sort(_sorted_buffers, [](const BufferInfo& a, const BufferInfo& b) { return a.max_cap_pf < b.max_cap_pf; });

  // Near-neighbor redundancy removal (default disabled: _redundancy_pct == 0)
  _redundancy_pct = CTSConfigInst.get_char_buf_redundancy_pct();
  if (_redundancy_pct > 0.0 && _sorted_buffers.size() > 1) {
    std::vector<BufferInfo> filtered;
    filtered.push_back(_sorted_buffers.front());
    for (size_t i = 1; i < _sorted_buffers.size(); ++i) {
      double prev_cap = filtered.back().max_cap_pf;
      double curr_cap = _sorted_buffers[i].max_cap_pf;
      // Keep if the gap exceeds the threshold
      if (prev_cap <= 0.0 || (curr_cap - prev_cap) / prev_cap >= _redundancy_pct) {
        filtered.push_back(_sorted_buffers[i]);
      } else {
        CTS_LOG_INFO << "CharBuilder: removed near-neighbor buffer " << _sorted_buffers[i].cell_master << " (max_cap=" << curr_cap
                     << " pF, gap=" << ((curr_cap - prev_cap) / prev_cap * 100.0) << "%)";
      }
    }
    _sorted_buffers = std::move(filtered);
  }

  // Log the final sorted buffer list
  CTS_LOG_INFO << "CharBuilder: sorted buffer list (" << _sorted_buffers.size() << " buffers):";
  for (size_t i = 0; i < _sorted_buffers.size(); ++i) {
    CTS_LOG_INFO << "  [" << i << "] " << _sorted_buffers[i].cell_master << " max_cap=" << _sorted_buffers[i].max_cap_pf << " pF";
  }
}

void CharBuilder::initCharParams()
{
  // Max pattern nodes
  _max_nodes = CTSConfigInst.get_max_pattern_nodes();
  if (_max_nodes == 0) {
    _max_nodes = 4;
  }
  CTS_LOG_INFO << "CharBuilder: max_pattern_nodes = " << _max_nodes;

  // Routing layer
  const auto& routing_layers = CTSConfigInst.get_routing_layers();
  if (!routing_layers.empty()) {
    _routing_layer = static_cast<int>(routing_layers.front());
  }
  CTS_LOG_INFO << "CharBuilder: routing_layer = " << _routing_layer;

  // Wire width
  _wire_width = CTSConfigInst.get_wire_width();

  // Max values for discretization ranges
  _max_slew = CTSConfigInst.get_max_buf_tran();
  _max_cap = CTSConfigInst.get_max_cap();
  _max_length = CTSConfigInst.get_max_length();

  // Discretization steps
  _slew_steps = CTSConfigInst.get_slew_steps();
  _cap_steps = CTSConfigInst.get_cap_steps();
  _length_steps = CTSConfigInst.get_length_steps();

  // Compute wire lengths to test (in um)
  _wire_lengths_um.clear();
  if (_max_length > 0.0 && _length_steps > 0) {
    double length_step_um = _max_length / _length_steps;
    for (unsigned i = 1; i <= _length_steps; ++i) {
      _wire_lengths_um.push_back(static_cast<double>(i) * length_step_um);
    }
  }
  CTS_LOG_INFO << "CharBuilder: " << _wire_lengths_um.size() << " wire lengths to test"
               << " (steps=" << _length_steps << ", max=" << _max_length << " um)";

  // Compute input slews to test (in ns)
  _slews_to_test.clear();
  if (_max_slew > 0.0 && _slew_steps > 0) {
    double slew_step_ns = _max_slew / _slew_steps;
    for (unsigned i = 1; i <= _slew_steps; ++i) {
      _slews_to_test.push_back(static_cast<double>(i) * slew_step_ns);
    }
  }
  CTS_LOG_INFO << "CharBuilder: " << _slews_to_test.size() << " slew values to test"
               << " (steps=" << _slew_steps << ", max=" << _max_slew << " ns)";

  // Compute output loads to test (in pF)
  _loads_to_test.clear();
  if (_max_cap > 0.0 && _cap_steps > 0) {
    double cap_step_pf = _max_cap / _cap_steps;
    for (unsigned i = 1; i <= _cap_steps; ++i) {
      _loads_to_test.push_back(static_cast<double>(i) * cap_step_pf);
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
    unsigned num_topologies = 1u << num_nodes;
    for (unsigned topo_bits = 0; topo_bits < num_topologies; ++topo_bits) {
      enumerateTopology(wire_length_um, num_nodes, topo_bits);
    }
  }
}

void CharBuilder::enumerateTopology(double wire_length_um, unsigned num_nodes, unsigned topology_bits)
{
  TopologyDesc topo = buildTopologyDesc(wire_length_um, num_nodes, topology_bits);
  size_t num_buf_positions = topo.buffer_positions.size();

  if (num_buf_positions == 0) {
    // Pure wire topology -- no buffers to assign
    std::vector<std::string> empty_masters;
    characterizeTopology(topo, empty_masters);
    return;
  }

  // Enumerate all monotonic buffer combinations
  size_t num_buf_types = _sorted_buffers.size();
  if (num_buf_types == 0) {
    return;
  }

  std::vector<size_t> buf_indices(num_buf_positions, 0);

  do {
    std::vector<std::string> buf_masters;
    buf_masters.reserve(num_buf_positions);
    for (size_t idx : buf_indices) {
      buf_masters.push_back(_sorted_buffers[idx].cell_master);
    }

    characterizeTopology(topo, buf_masters);

  } while (advanceToNextMonotonic(buf_indices, num_buf_types));
}

bool CharBuilder::isMonotonic(const std::vector<size_t>& buf_indices) const
{
  for (size_t i = 1; i < buf_indices.size(); ++i) {
    if (buf_indices[i] < buf_indices[i - 1]) {
      return false;
    }
  }
  return true;
}

size_t CharBuilder::getMonotonicComboCount(size_t num_buf_types, size_t num_positions) const
{
  if (num_buf_types == 0 || num_positions == 0) {
    return 0;
  }
  size_t n = num_buf_types + num_positions - 1;
  size_t k = num_positions;
  if (k > n - k) {
    k = n - k;
  }
  size_t result = 1;
  for (size_t i = 0; i < k; ++i) {
    result = result * (n - i) / (i + 1);
  }
  return result;
}

bool CharBuilder::advanceToNextMonotonic(std::vector<size_t>& buf_indices, size_t num_buf_types) const
{
  if (buf_indices.empty() || num_buf_types == 0) {
    return false;
  }

  int pos = static_cast<int>(buf_indices.size()) - 1;

  while (pos >= 0) {
    if (buf_indices[pos] + 1 < num_buf_types) {
      ++buf_indices[pos];
      for (size_t j = static_cast<size_t>(pos) + 1; j < buf_indices.size(); ++j) {
        buf_indices[j] = buf_indices[pos];
      }
      return true;
    }
    --pos;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Topology construction
// ---------------------------------------------------------------------------

CharBuilder::TopologyDesc CharBuilder::buildTopologyDesc(double wire_length_um, unsigned num_nodes, unsigned topology_bits) const
{
  TopologyDesc desc;

  // Identify buffer positions from topology_bits
  for (unsigned i = 0; i < num_nodes; ++i) {
    if ((topology_bits >> i) & 1u) {
      desc.buffer_positions.push_back(i);
    }
  }

  // Variable wire segment lengths based on consecutive non-buffer positions.
  // Each segment spans from the previous boundary (input or buffer) to the next
  // boundary (buffer or output). The segment length is proportional to the number
  // of consecutive non-buffer node positions it spans.
  //
  // Wire segment unit = wire_length_um / (num_nodes + 1)
  // Each segment's length = (positions_in_segment) * unit
  double unit_um = (num_nodes > 0) ? wire_length_um / (num_nodes + 1) : wire_length_um;

  unsigned nodes_without_buf = 0;
  size_t buf_ptr = 0;

  for (unsigned i = 0; i < num_nodes; ++i) {
    ++nodes_without_buf;
    if (buf_ptr < desc.buffer_positions.size() && desc.buffer_positions[buf_ptr] == i) {
      // Buffer at this position: emit the wire segment before it
      desc.wire_segments_um.push_back(nodes_without_buf * unit_um);
      nodes_without_buf = 0;
      ++buf_ptr;
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
  for (double seg_len : topo.wire_segments_um) {
    total_length_um += seg_len;
  }

  // Compute normalized buffer positions in (0, 1]
  std::vector<double> buffer_positions_norm;
  if (!topo.buffer_positions.empty() && total_length_um > 0.0) {
    double cumulative_um = 0.0;
    size_t buf_idx = 0;
    for (size_t seg = 0; seg < topo.wire_segments_um.size() && buf_idx < topo.buffer_positions.size(); ++seg) {
      cumulative_um += topo.wire_segments_um[seg];
      if (seg < topo.wire_segments_um.size() - 1) {
        // A buffer exists at the boundary after this segment
        double normalized = cumulative_um / total_length_um;
        buffer_positions_norm.push_back(normalized);
        ++buf_idx;
      }
    }
  }

  // Discretize length for BufferingPattern
  unsigned length_idx = discretize(total_length_um, _max_length, _length_steps);

  PatternId pid = PatternId::segment(_next_pattern_id);
  BufferingPattern pattern(length_idx, pid, buffer_positions_norm, buf_masters);
  _buffering_patterns.push_back(std::move(pattern));

  // Build the circuit once per topology+buffer combo (shared across load/slew sweeps)
  createCharCircuit(topo, buf_masters);

  // Sweep over (load, slew) combinations
  for (double load_pf : _loads_to_test) {
    // Skip loads that are smaller than the sink's intrinsic input cap
    double effective_load = load_pf - _sink_input_cap_pf;
    if (effective_load < 0.0) {
      continue;
    }

    // Build RC trees for this load value (rebuild parasitics for each load)
    setCharParasitics(topo, effective_load);

    // Create a propagated clock on the source buffer's output pin
    CTSAPIInst.createCharClock(_source_out_pin, _char_clock_name, 10.0);

    for (double input_slew_ns : _slews_to_test) {
      // Annotate input slew on the source buffer's output vertex
      CTSAPIInst.setCharInputSlew(_source_out_pin, input_slew_ns);

      // Full timing update (clock + slew + delay propagation)
      CTSAPIInst.updateCharTiming();

      // Query total delay via clock arrival time at sink input pin
      double delay_ns = CTSAPIInst.queryCharClockAT(_sink_in_pin, _char_clock_name);

      // Query output slew at sink input pin
      double output_slew_ns = CTSAPIInst.queryCharSlew(_sink_in_pin);

      // Power: iSTA has no per-instance power API; deferred to iPA integration
      double power_w = 0.0;

      // Compute input capacitance (analytical, matching reference)
      double input_cap_pf = 0.0;
      if (!buf_masters.empty()) {
        // Buffered: first buffer input cap + first wire segment cap
        input_cap_pf = CTSAPIInst.queryCharInputPinCap(buf_masters.front());
        input_cap_pf += CTSAPIInst.queryWireCapacitance(_routing_layer, topo.wire_segments_um.front(), _wire_width);
      } else {
        // Pure wire: load + total wire cap
        input_cap_pf = load_pf;
        for (double seg_len_um : topo.wire_segments_um) {
          input_cap_pf += CTSAPIInst.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);
        }
      }

      // Discretize values to bin indices in [1, *_steps]
      unsigned input_slew_idx = discretize(input_slew_ns, _max_slew, _slew_steps);
      unsigned output_slew_idx = discretize(output_slew_ns, _max_slew, _slew_steps);
      unsigned driven_cap_idx = discretize(input_cap_pf, _max_cap, _cap_steps);
      unsigned load_cap_idx = discretize(load_pf, _max_cap, _cap_steps);

      // Create CharCore and SegmentChar
      CharCore core(input_slew_idx, output_slew_idx, driven_cap_idx, load_cap_idx, delay_ns, power_w, pid);
      SegmentChar seg_char(core, length_idx);
      _segment_chars.push_back(seg_char);
    }

    // Remove the clock definition and restore original SDC for next iteration
    CTSAPIInst.destroyCharClock();
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

  std::string id_prefix = "cts_char_" + std::to_string(_char_circuit_id) + "_";

  // Use the largest-drive buffer for source and sink
  const auto& endpoint_buf = _sorted_buffers.back();

  // Create source buffer instance
  _source_inst_name = id_prefix + "source";
  CTSAPIInst.createCharInstance(endpoint_buf.cell_master, _source_inst_name);
  _source_out_pin = _source_inst_name + "/" + endpoint_buf.output_pin;

  // Create sink buffer instance
  _sink_inst_name = id_prefix + "sink";
  CTSAPIInst.createCharInstance(endpoint_buf.cell_master, _sink_inst_name);
  _sink_in_pin = _sink_inst_name + "/" + endpoint_buf.input_pin;

  // Create characterization buffer instances
  for (size_t i = 0; i < buf_masters.size(); ++i) {
    std::string inst_name = id_prefix + "buf_" + std::to_string(i);
    CTSAPIInst.createCharInstance(buf_masters[i], inst_name);
    _temp_inst_names.push_back(inst_name);
  }

  // Create wire nets: one per wire segment
  // wire_segments_um has (num_buffers + 1) segments:
  //   net_0: source_out -> first_buf_in (or sink_in if pure wire)
  //   net_i: buf_{i-1}_out -> buf_i_in
  //   net_N: last_buf_out -> sink_in
  for (size_t i = 0; i < topo.wire_segments_um.size(); ++i) {
    std::string net_name = id_prefix + "net_" + std::to_string(i);
    CTSAPIInst.createCharNet(net_name);
    _temp_net_names.push_back(net_name);
  }

  // Connect the chain: source_out -> net_0 -> [buf_0_in / buf_0_out] -> net_1 -> ... -> sink_in
  // First net: source output drives it
  CTSAPIInst.attachCharPin(_source_inst_name, endpoint_buf.output_pin, _temp_net_names.front());

  // Connect characterization buffers between nets
  for (size_t bi = 0; bi < buf_masters.size(); ++bi) {
    // Find the buffer info for port names
    const BufferInfo* buf_info = nullptr;
    for (const auto& info : _sorted_buffers) {
      if (info.cell_master == buf_masters[bi]) {
        buf_info = &info;
        break;
      }
    }
    CTS_LOG_FATAL_IF(buf_info == nullptr) << "Buffer info not found for: " << buf_masters[bi];

    // Buffer input connects to net[bi] (the net coming into this buffer)
    CTSAPIInst.attachCharPin(_temp_inst_names[bi], buf_info->input_pin, _temp_net_names[bi]);
    // Buffer output connects to net[bi+1] (the net going out of this buffer)
    CTSAPIInst.attachCharPin(_temp_inst_names[bi], buf_info->output_pin, _temp_net_names[bi + 1]);
  }

  // Last net: sink input receives it
  CTSAPIInst.attachCharPin(_sink_inst_name, endpoint_buf.input_pin, _temp_net_names.back());

  // Set the clock name for this circuit
  _char_clock_name = id_prefix + "clk";

  ++_char_circuit_id;
}

void CharBuilder::setCharParasitics(const TopologyDesc& topo, double load_pf)
{
  // Build Pi-model RC tree for each wire segment
  for (size_t i = 0; i < _temp_net_names.size(); ++i) {
    double seg_len_um = topo.wire_segments_um[i];
    double wire_res = CTSAPIInst.queryWireResistance(_routing_layer, seg_len_um, _wire_width);
    double wire_cap = CTSAPIInst.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);

    // Add external load to the last segment's far-end capacitance
    double seg_load = (i == _temp_net_names.size() - 1) ? load_pf : 0.0;
    CTSAPIInst.buildCharRcTree(_temp_net_names[i], wire_res, wire_cap, seg_load);
  }
}

void CharBuilder::destroyCharCircuit()
{
  // Cleanup in reverse order: nets first, then characterization buffers, then source/sink
  for (auto it = _temp_net_names.rbegin(); it != _temp_net_names.rend(); ++it) {
    CTSAPIInst.destroyCharNet(*it);
  }
  for (auto it = _temp_inst_names.rbegin(); it != _temp_inst_names.rend(); ++it) {
    CTSAPIInst.destroyCharInstance(*it);
  }
  if (!_sink_inst_name.empty()) {
    CTSAPIInst.destroyCharInstance(_sink_inst_name);
    _sink_inst_name.clear();
  }
  if (!_source_inst_name.empty()) {
    CTSAPIInst.destroyCharInstance(_source_inst_name);
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

unsigned CharBuilder::discretize(double value, double max_value, unsigned steps)
{
  if (max_value <= 0.0 || steps == 0 || value <= 0.0) {
    return 0;
  }
  double step_size = max_value / steps;
  return static_cast<unsigned>(std::ceil(value / step_size));
}

}  // namespace icts
