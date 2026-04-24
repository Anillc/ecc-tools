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
 * @file NumericalSample.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Physical-domain numerical characterization sample extraction.
 */

#include "NumericalSample.hh"

#include "SegmentChar.hh"
#include "ValueLattice.hh"

namespace icts {
namespace {

auto latticeValueOrIndex(const UniformValueLattice& lattice, unsigned index) -> double
{
  if (lattice.isValid()) {
    return lattice.valueForIndex(index);
  }
  return static_cast<double>(index);
}

}  // namespace

auto MakeNumericalSample(const SegmentChar& segment_char, const NumericalSampleLattices& lattices) -> NumericalSample
{
  return NumericalSample{
      .pattern_id = segment_char.get_pattern_id(),
      .length_idx = segment_char.get_length_idx(),
      .length_um = latticeValueOrIndex(lattices.length_lattice, segment_char.get_length_idx()),
      .slew_in_ns = latticeValueOrIndex(lattices.slew_lattice, segment_char.get_input_slew_idx()),
      .cap_load_pf = latticeValueOrIndex(lattices.load_cap_lattice, segment_char.get_load_cap_idx()),
      .delay_ns = segment_char.get_delay(),
      .slew_out_ns = latticeValueOrIndex(lattices.output_slew_lattice, segment_char.get_output_slew_idx()),
      .power_w = segment_char.get_power(),
      .driven_cap_pf = latticeValueOrIndex(lattices.driven_cap_lattice, segment_char.get_driven_cap_idx()),
      .source_boundary_net_switch_power_w = segment_char.get_source_boundary_net_switch_power(),
  };
}

auto ExtractNumericalSamples(const std::vector<SegmentChar>& segment_chars, const NumericalSampleLattices& lattices)
    -> std::vector<NumericalSample>
{
  std::vector<NumericalSample> samples;
  samples.reserve(segment_chars.size());
  for (const auto& segment_char : segment_chars) {
    samples.push_back(MakeNumericalSample(segment_char, lattices));
  }
  return samples;
}

}  // namespace icts
