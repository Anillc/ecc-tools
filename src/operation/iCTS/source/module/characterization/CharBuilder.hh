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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "BufferingPattern.hh"
#include "SegmentChar.hh"

namespace icts {

struct CharBufferInfo
{
  std::string cell_master;
  double max_cap_pf = 0.0;  // Drive-strength proxy used for ordering and fallback selection.
  double input_cap_pf = 0.0;
  std::string input_pin;
  std::string output_pin;
};

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

  auto init() -> void;
  auto build() -> void;

  auto get_segment_chars() const -> const std::vector<SegmentChar>& { return _segment_chars; }
  auto get_buffering_patterns() const -> const std::vector<BufferingPattern>& { return _buffering_patterns; }
  auto get_wire_lengths_um() const -> const std::vector<double>& { return _wire_lengths_um; }
  auto get_wire_length_unit_um() const -> double { return _length_unit_um; }
  auto get_wire_length_iterations() const -> unsigned { return _wire_length_iterations; }
  auto get_max_slew() const -> double { return _max_slew; }
  auto get_max_cap() const -> double { return _max_cap; }
  auto get_slew_steps() const -> unsigned { return _slew_steps; }
  auto get_cap_steps() const -> unsigned { return _cap_steps; }

  struct WireLengthBuildStat
  {
    double wire_length_um = 0.0;
    std::size_t estimated_patterns = 0;
    std::size_t estimated_sta_samples = 0;
    std::size_t evaluated_patterns = 0;
    std::size_t feasible_patterns = 0;
    std::size_t skipped_patterns_infeasible = 0;
    std::size_t skipped_load_points = 0;
    std::size_t skipped_sta_samples = 0;
    std::size_t executed_sta_samples = 0;
    std::size_t added_segment_chars = 0;
    std::size_t added_patterns = 0;
    double elapsed_ms = 0.0;
  };
  auto get_wire_length_stats() const -> const std::vector<WireLengthBuildStat>& { return _wire_length_stats; }

 private:
  struct TopologyBits
  {
    std::uint64_t value = 0U;
  };

  auto calcBufferSlotCount(double wire_length_um) const -> unsigned;
  static auto countSelectedSlots(TopologyBits topology_bits) -> unsigned;
  auto estimatePatternCountPerWireLength(double wire_length_um) const -> std::size_t;
  auto enumerateWireLength(double wire_length_um, WireLengthBuildStat& build_stat) -> void;
  auto enumerateTopology(double wire_length_um, unsigned num_slots, TopologyBits topology_bits, WireLengthBuildStat& build_stat) -> void;
  static auto getMonotonicComboCount(std::size_t num_buf_types, std::size_t num_positions) -> std::size_t;
  static auto advanceToNextMonotonic(std::vector<std::size_t>& buf_indices, std::size_t num_buf_types) -> bool;

  struct TopologyDesc
  {
    std::vector<double> wire_segments_um;
    std::vector<std::size_t> buffer_positions;
  };

  struct PatternFeasibility
  {
    bool is_pattern_feasible = false;
    double max_load_pf = 0.0;
  };

  auto buildTopologyDesc(double wire_length_um, unsigned num_slots, TopologyBits topology_bits) const -> TopologyDesc;
  auto analyzePatternFeasibility(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) const -> PatternFeasibility;
  auto characterizeTopology(const TopologyDesc& topo, const std::vector<std::string>& buf_masters, WireLengthBuildStat& build_stat) -> void;
  auto createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) -> void;
  auto setCharParasitics(const TopologyDesc& topo, double load_pf) -> void;
  auto destroyCharCircuit() -> void;

  static auto discretize(double value, double max_value, unsigned steps) -> unsigned;

  auto findBufferInfo(const std::string& cell_master) const -> const CharBufferInfo*;
  std::vector<CharBufferInfo> _sorted_buffers;

  // Physical sweep grids kept in user units before discretization.
  std::vector<double> _wire_lengths_um;
  std::vector<double> _slews_to_test;
  std::vector<double> _loads_to_test;
  unsigned _max_nodes = 8;
  int _routing_layer = 1;
  std::optional<double> _wire_width = std::nullopt;
  double _max_slew = 0.0;
  double _max_cap = 0.0;
  double _max_length = 0.0;
  double _length_unit_um = 0.0;
  unsigned _slew_steps = 20;
  unsigned _cap_steps = 20;
  unsigned _wire_length_iterations = 20;

  std::string _source_inst_name;
  std::string _source_in_pin;
  std::string _sink_inst_name;
  std::string _source_out_pin;
  std::string _sink_in_pin;
  double _sink_input_cap_pf = 0.0;
  std::vector<std::string> _temp_inst_names;
  std::vector<std::string> _temp_net_names;
  std::string _char_clock_name;
  unsigned _char_circuit_id = 0;

  std::vector<SegmentChar> _segment_chars;
  std::vector<BufferingPattern> _buffering_patterns;
  std::vector<WireLengthBuildStat> _wire_length_stats;
  unsigned _next_pattern_id = 0;
};

}  // namespace icts
