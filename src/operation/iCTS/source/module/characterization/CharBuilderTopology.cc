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
 * @file CharBuilderTopology.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Converts topology bitsets into segment-wire descriptions.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "CharBuilder.hh"
#include "ValueLattice.hh"

namespace icts {
namespace {

constexpr std::uint64_t kTopologyPresentMask = 1ULL;

auto hasTerminalLatticeBuffer(double wire_length_um, double length_unit_um, unsigned num_slots, std::uint64_t topology_bits_value) -> bool
{
  if (wire_length_um <= 0.0 || length_unit_um <= 0.0 || num_slots == 0U) {
    return false;
  }

  const unsigned terminal_slot_index = num_slots - 1U;
  if (((topology_bits_value >> terminal_slot_index) & kTopologyPresentMask) == 0U) {
    return false;
  }

  const double terminal_slot_boundary_um = std::min(static_cast<double>(num_slots) * length_unit_um, wire_length_um);
  return std::abs(terminal_slot_boundary_um - wire_length_um) <= kValueLatticeEpsilon;
}

}  // namespace

auto CharBuilder::buildTopologyDesc(double wire_length_um, unsigned num_slots, TopologyBits topology_bits) const -> TopologyDesc
{
  TopologyDesc desc;
  desc.has_terminal_branch_buffer = hasTerminalLatticeBuffer(wire_length_um, _length_unit_um, num_slots, topology_bits.value);

  if (num_slots == 0U || _length_unit_um <= 0.0) {
    desc.wire_segments_um.push_back(wire_length_um);
    return desc;
  }

  double previous_boundary_um = 0.0;

  for (unsigned slot_index = 0; slot_index < num_slots; ++slot_index) {
    const double slot_boundary_um = std::min((static_cast<double>(slot_index) + 1.0) * _length_unit_um, wire_length_um);
    if (((topology_bits.value >> slot_index) & kTopologyPresentMask) != 0U) {
      desc.buffer_positions.push_back(slot_index);
      desc.wire_segments_um.push_back(slot_boundary_um - previous_boundary_um);
      previous_boundary_um = slot_boundary_um;
    }
  }

  desc.wire_segments_um.push_back(std::max(0.0, wire_length_um - previous_boundary_um));

  return desc;
}

}  // namespace icts
