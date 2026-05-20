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
 * @date 2026-04-18
 * @brief Segment characterization builder: enumerate buffering patterns and
 *        obtain timing/power via CTSAPI.
 */

#pragma once
// IWYU pragma: private, include "characterization/builder/CharBuilder.hh"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "BufferingPattern.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"
#include "characterization/buffer_cell/CharacterizationBufferCell.hh"
#include "routing/ClockRouteSegmentRc.hh"

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
  struct InitOptions
  {
    std::optional<double> wirelength_unit_um = std::nullopt;
    std::optional<unsigned> wirelength_iterations = std::nullopt;
    std::optional<std::vector<unsigned>> wirelength_indices = std::nullopt;
    bool allow_auto_wirelength_unit = false;
    std::optional<double> max_slew_ns = std::nullopt;
    std::optional<double> max_cap_pf = std::nullopt;
    std::vector<std::string> buffer_types;
    std::vector<CharacterizationBufferCell> characterization_buffer_cells;
    std::optional<double> char_buf_redundancy_pct = std::nullopt;
    std::optional<unsigned> slew_steps = std::nullopt;
    std::optional<unsigned> cap_steps = std::nullopt;
    std::optional<int> routing_layer = std::nullopt;
    std::optional<double> wire_width = std::nullopt;
    ClockRouteSegmentRc clock_route_segment_rc;
  };

  CharBuilder() = default;
  ~CharBuilder() = default;

  auto init(const InitOptions& options) -> void;
  auto build() -> void;

  auto get_segment_chars() const -> const std::vector<SegmentChar>& { return _segment_chars; }
  auto get_buffering_patterns() const -> const std::vector<BufferingPattern>& { return _buffering_patterns; }
  auto get_wirelengths_um() const -> const std::vector<double>& { return _wirelengths_um; }
  auto get_wirelength_indices() const -> const std::vector<unsigned>& { return _wirelength_indices; }
  auto get_wirelength_unit_um() const -> double { return _length_unit_um; }
  auto get_wirelength_unit_source() const -> const std::string& { return _wirelength_unit_source; }
  auto get_wirelength_unit_detail() const -> const std::string& { return _wirelength_unit_detail; }
  auto get_wirelength_iterations() const -> unsigned { return _wirelength_iterations; }
  auto get_max_slew() const -> double { return _max_slew; }
  auto get_max_cap() const -> double { return _max_cap; }
  auto get_slew_steps() const -> unsigned { return _slew_steps; }
  auto get_cap_steps() const -> unsigned { return _cap_steps; }
  auto get_routing_layer() const -> int { return _routing_layer; }
  auto get_wire_width() const -> std::optional<double> { return _wire_width; }
  auto get_clock_route_segment_rc() const -> const ClockRouteSegmentRc& { return _clock_route_segment_rc; }
  auto get_characterization_buffer_cells() const -> const std::vector<CharacterizationBufferCell>& { return _sorted_buffers; }
  auto get_length_lattice() const -> UniformValueLattice { return UniformValueLattice(_length_unit_um, _wirelength_iterations); }
  auto get_slew_lattice() const -> UniformValueLattice { return UniformValueLattice::buildFromMax(_max_slew, _slew_steps); }
  auto get_cap_lattice() const -> UniformValueLattice { return UniformValueLattice::buildFromMax(_max_cap, _cap_steps); }
  auto get_executed_sta_samples() const -> std::size_t { return _executed_sta_samples; }
  auto get_skipped_sta_samples() const -> std::size_t { return _skipped_sta_samples; }
  auto get_output_slew_overflow_samples() const -> std::size_t { return _output_slew_overflow_samples; }
  auto get_driven_cap_overflow_samples() const -> std::size_t { return _driven_cap_overflow_samples; }
  auto get_driven_cap_overflow_load_points() const -> std::size_t { return _driven_cap_overflow_load_points; }
  auto get_max_observed_output_slew_ns() const -> double { return _max_observed_output_slew_ns; }
  auto get_max_observed_output_slew_idx() const -> unsigned { return _max_observed_output_slew_idx; }
  auto get_max_observed_driven_cap_pf() const -> double { return _max_observed_driven_cap_pf; }
  auto get_max_observed_driven_cap_idx() const -> unsigned { return _max_observed_driven_cap_idx; }

 private:
  static constexpr double kCapFeasibilityEpsilonPf = 1e-6;

  struct BuildProgress;
  struct TopologyBits;
  struct TopologyDesc;
  struct StoredSampleIndices;
  struct PatternFeasibility;

  auto calcTopologySlotCount(double wirelength_um) const -> unsigned;
  static auto countSelectedSlots(TopologyBits topology_bits) -> unsigned;
  auto estimatePatternCountPerWirelength(double wirelength_um) const -> std::size_t;
  auto enumerateWirelength(unsigned length_idx, double wirelength_um, BuildProgress& build_progress) -> void;
  auto enumerateTopology(unsigned length_idx, double wirelength_um, unsigned num_slots, TopologyBits topology_bits,
                         BuildProgress& build_progress) -> void;
  static auto getMonotonicComboCount(std::size_t num_buf_types, std::size_t num_positions) -> std::size_t;
  static auto advanceToNextMonotonic(std::vector<std::size_t>& buf_indices, std::size_t num_buf_types) -> bool;

  auto buildTopologyDesc(double wirelength_um, unsigned num_slots, TopologyBits topology_bits) const -> TopologyDesc;
  auto analyzePatternFeasibility(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) const -> PatternFeasibility;
  auto tryMakeStoredSampleIndices(unsigned input_slew_idx, unsigned load_cap_idx, double output_slew_ns, double driven_cap_pf,
                                  BuildProgress& build_progress) const -> std::optional<StoredSampleIndices>;
  auto storeBufferingPattern(unsigned length_idx, const TopologyDesc& topo, const std::vector<std::string>& buf_masters,
                             double total_length_um) -> PatternId;
  auto sampleFeasibleTopology(unsigned length_idx, const PatternId& pid, const TopologyDesc& topo,
                              const std::vector<std::string>& buf_masters, const PatternFeasibility& feasibility,
                              BuildProgress& build_progress) -> void;
  auto sampleLoadSlews(unsigned length_idx, const PatternId& pid, const TopologyDesc& topo, double effective_load_pf, double load_pf,
                       double driven_cap_pf, bool& power_context_ready, BuildProgress& build_progress) -> void;
  auto characterizeTopology(unsigned length_idx, const TopologyDesc& topo, const std::vector<std::string>& buf_masters,
                            BuildProgress& build_progress) -> void;
  auto createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) -> void;
  auto setCharParasitics(const TopologyDesc& topo, double load_pf) const -> void;
  auto destroyCharCircuit() -> void;

  auto findCharacterizationBufferCell(const std::string& cell_master) const -> const CharacterizationBufferCell*;
  auto calcClockRouteWireCapPf(double wirelength_um) const -> double;
  std::vector<CharacterizationBufferCell> _sorted_buffers;

  // Physical sweep grids kept in user units before discretization.
  std::vector<unsigned> _wirelength_indices;
  std::vector<double> _wirelengths_um;
  std::vector<double> _slews_to_test;
  std::vector<double> _loads_to_test;
  int _routing_layer = 0;
  std::optional<double> _wire_width = std::nullopt;
  ClockRouteSegmentRc _clock_route_segment_rc;
  double _max_slew = 0.0;
  double _max_cap = 0.0;
  double _max_length = 0.0;
  double _length_unit_um = 0.0;
  std::string _wirelength_unit_source;
  std::string _wirelength_unit_detail;
  unsigned _slew_steps = 15;
  unsigned _cap_steps = 15;
  unsigned _wirelength_iterations = 3;

  std::string _source_inst_name;
  std::string _source_in_pin;
  std::string _sink_inst_name;
  std::string _source_out_pin;
  std::string _sink_in_pin;
  std::string _timing_observation_pin;
  double _sink_input_cap_pf = 0.0;
  std::vector<std::string> _temp_inst_names;
  std::vector<std::string> _temp_net_names;
  std::string _char_clock_name;
  unsigned _char_circuit_id = 0;
  std::size_t _fast_sta_char_context_id = std::numeric_limits<std::size_t>::max();

  std::vector<SegmentChar> _segment_chars;
  std::vector<BufferingPattern> _buffering_patterns;
  unsigned _next_pattern_id = 0;
  std::size_t _executed_sta_samples = 0;
  std::size_t _skipped_sta_samples = 0;
  std::size_t _output_slew_overflow_samples = 0;
  std::size_t _driven_cap_overflow_samples = 0;
  std::size_t _driven_cap_overflow_load_points = 0;
  double _max_observed_output_slew_ns = 0.0;
  unsigned _max_observed_output_slew_idx = 0;
  double _max_observed_driven_cap_pf = 0.0;
  unsigned _max_observed_driven_cap_idx = 0;
};

}  // namespace icts
