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
 * @file NumericalSample.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Physical-domain numerical characterization sample extraction.
 */

#pragma once

#include <vector>

#include "characterization/PatternId.hh"
#include "characterization/ValueLattice.hh"

namespace icts {

class SegmentChar;

/**
 * @brief One physical-domain characterization sample for a fixed segment pattern.
 */
struct NumericalSample
{
  PatternId pattern_id = PatternId::segment(0U);
  unsigned length_idx = 0U;
  double length_um = 0.0;
  double slew_in_ns = 0.0;
  double cap_load_pf = 0.0;
  double delay_ns = 0.0;
  double slew_out_ns = 0.0;
  double power_w = 0.0;
  double driven_cap_pf = 0.0;
  double source_boundary_net_switch_power_w = 0.0;
};

/**
 * @brief Lattice metadata needed to reconstruct physical sample coordinates.
 */
struct NumericalSampleLattices
{
  UniformValueLattice slew_lattice;
  UniformValueLattice load_cap_lattice;
  UniformValueLattice output_slew_lattice;
  UniformValueLattice driven_cap_lattice;
  UniformValueLattice length_lattice;
};

auto MakeNumericalSample(const SegmentChar& segment_char, const NumericalSampleLattices& lattices) -> NumericalSample;

auto ExtractNumericalSamples(const std::vector<SegmentChar>& segment_chars, const NumericalSampleLattices& lattices)
    -> std::vector<NumericalSample>;

}  // namespace icts
