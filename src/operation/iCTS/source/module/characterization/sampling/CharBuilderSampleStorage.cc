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
 * @file CharBuilderSampleStorage.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Characterization lattice-index validation for stored STA samples.
 */

#include <algorithm>
#include <optional>

#include "ValueLattice.hh"
#include "characterization/builder/CharBuilder.hh"
#include "characterization/builder/CharBuilderSweepState.hh"

namespace icts {

auto CharBuilder::tryMakeStoredSampleIndices(unsigned input_slew_idx, unsigned load_cap_idx, double output_slew_ns, double driven_cap_pf,
                                             BuildProgress& build_progress) const -> std::optional<StoredSampleIndices>
{
  const auto slew_lattice = get_slew_lattice();
  const auto cap_lattice = get_cap_lattice();
  const unsigned output_slew_idx = slew_lattice.coveringIndex(output_slew_ns);
  const unsigned driven_cap_idx = cap_lattice.coveringIndex(driven_cap_pf);

  build_progress.max_observed_output_slew_ns = std::max(build_progress.max_observed_output_slew_ns, output_slew_ns);
  build_progress.max_observed_output_slew_idx = std::max(build_progress.max_observed_output_slew_idx, output_slew_idx);
  build_progress.max_observed_driven_cap_pf = std::max(build_progress.max_observed_driven_cap_pf, driven_cap_pf);
  build_progress.max_observed_driven_cap_idx = std::max(build_progress.max_observed_driven_cap_idx, driven_cap_idx);

  const auto observed_output_slew_idx = slew_lattice.tryObservedIndex(output_slew_ns);
  if (!observed_output_slew_idx.has_value()) {
    ++build_progress.output_slew_overflow_samples;
    return std::nullopt;
  }
  const auto observed_driven_cap_idx = cap_lattice.tryObservedIndex(driven_cap_pf);
  if (!observed_driven_cap_idx.has_value()) {
    ++build_progress.driven_cap_overflow_samples;
    return std::nullopt;
  }

  return StoredSampleIndices{
      .input_slew_idx = input_slew_idx,
      .output_slew_idx = *observed_output_slew_idx,
      .driven_cap_idx = *observed_driven_cap_idx,
      .load_cap_idx = load_cap_idx,
  };
}

}  // namespace icts
