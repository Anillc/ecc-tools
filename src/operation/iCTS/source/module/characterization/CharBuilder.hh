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
 * @file CharBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @brief Segment characterization builder: enumerate buffering patterns and
 *        obtain timing/power via CTSAPI.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "BufferingPattern.hh"
#include "SegmentChar.hh"

namespace icts {

/**
 * @brief Builds segment characterization entries by enumerating buffering
 *        patterns and querying STA/PA through CTSAPI.
 *
 * Physical values (slew ns, cap pF, length um) are discretized to integer
 * bin indices in [1, *_steps] for storage in CharCore/SegmentChar.
 */
class CharBuilder
{
 public:
  CharBuilder() = default;
  ~CharBuilder() = default;

  // Main flow
  void init();
  void build();

  // Results
  const std::vector<SegmentChar>& get_segment_chars() const { return _segment_chars; }
  const std::vector<BufferingPattern>& get_buffering_patterns() const { return _buffering_patterns; }

 private:
  // Initialization helpers
  void initBufferList();
  void initCharParams();

  // Pattern enumeration
  void enumerateWireLength(double wire_length_um);
  void enumerateTopology(double wire_length_um, unsigned num_nodes, unsigned topology_bits);
  bool isMonotonic(const std::vector<size_t>& buf_indices) const;
  size_t getMonotonicComboCount(size_t num_buf_types, size_t num_positions) const;
  bool advanceToNextMonotonic(std::vector<size_t>& buf_indices, size_t num_buf_types) const;

  // Characterization circuit construction & measurement
  struct TopologyDesc
  {
    std::vector<double> wire_segments_um;  // wire length per segment (um)
    std::vector<size_t> buffer_positions;  // node indices that have buffers
  };

  TopologyDesc buildTopologyDesc(double wire_length_um, unsigned num_nodes, unsigned topology_bits) const;
  void characterizeTopology(const TopologyDesc& topo, const std::vector<std::string>& buf_masters);
  void createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters);
  void setCharParasitics(const TopologyDesc& topo, double load_pf);
  void destroyCharCircuit();

  // Discretization: physical value → bin index in [1, steps]
  static unsigned discretize(double value, double max_value, unsigned steps);

  // Sorted buffer list (ascending max_cap)
  struct BufferInfo
  {
    std::string cell_master;
    double max_cap_pf;       // max capacitance limit (pF) -- proxy for drive strength
    double input_cap_pf;     // input pin capacitance (pF)
    std::string input_pin;   // input port name
    std::string output_pin;  // output port name
  };
  std::vector<BufferInfo> _sorted_buffers;

  // Characterization parameters (from Config)
  std::vector<double> _wire_lengths_um;              // wire lengths to enumerate (um)
  std::vector<double> _slews_to_test;                // input slew values (ns)
  std::vector<double> _loads_to_test;                // output load values (pF)
  unsigned _max_nodes = 4;                           // max nodes per wire segment
  int _routing_layer = 1;                            // routing layer for wire R/C
  std::optional<double> _wire_width = std::nullopt;  // absent = default width
  double _redundancy_pct = 0.0;                      // buffer near-neighbor removal threshold

  // Max values and steps for discretization (from Config)
  double _max_slew = 0.0;       // max slew (ns)
  double _max_cap = 0.0;        // max capacitance (pF)
  double _max_length = 0.0;     // max wire length (um)
  unsigned _slew_steps = 20;    // number of slew bins
  unsigned _cap_steps = 20;     // number of cap bins
  unsigned _length_steps = 20;  // number of length bins

  // Temporary circuit tracking
  std::string _source_inst_name;
  std::string _sink_inst_name;
  std::string _source_out_pin;
  std::string _sink_in_pin;
  double _sink_input_cap_pf = 0.0;
  std::vector<std::string> _temp_inst_names;
  std::vector<std::string> _temp_net_names;
  std::string _char_clock_name;
  unsigned _char_circuit_id = 0;

  // Results
  std::vector<SegmentChar> _segment_chars;
  std::vector<BufferingPattern> _buffering_patterns;
  unsigned _next_pattern_id = 0;
};

}  // namespace icts
