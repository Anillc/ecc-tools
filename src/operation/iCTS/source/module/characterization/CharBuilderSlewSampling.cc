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
 * @file CharBuilderSlewSampling.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Characterization per-load slew sampling and SegmentChar emission.
 */

#include <optional>
#include <vector>

#include "CharBuilder.hh"
#include "CharCore.hh"
#include "FastStaTypes.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"
#include "adapter/fast_sta/FastStaAdapter.hh"

namespace icts {
namespace {

constexpr bool kEnableCharPowerSampling = true;

}  // namespace

auto CharBuilder::sampleLoadSlews(unsigned length_idx, const PatternId& pid, const TopologyDesc& topo, double effective_load_pf,
                                  double load_pf, double driven_cap_pf, bool& power_context_ready, BuildProgress& build_progress) -> void
{
  setCharParasitics(topo, effective_load_pf);

  const unsigned load_cap_idx = get_cap_lattice().coveringIndex(load_pf);
  for (const double input_slew_ns : _slews_to_test) {
    const auto sample_result = FastStaAdapter::runCharSample(_fast_sta_char_context_id, input_slew_ns);
    ++build_progress.executed_sta_samples;
    const unsigned input_slew_idx = get_slew_lattice().coveringIndex(input_slew_ns);

    double power_w = 0.0;
    double source_boundary_net_switch_power_w = 0.0;
    if (!sample_result.valid) {
      ++build_progress.skipped_sta_samples;
      continue;
    }
    if (kEnableCharPowerSampling && power_context_ready) {
      power_w = sample_result.power_w;
      source_boundary_net_switch_power_w = sample_result.source_boundary_net_switch_power_w;
    }

    const auto stored_sample_indices
        = tryMakeStoredSampleIndices(input_slew_idx, load_cap_idx, sample_result.output_slew_ns, driven_cap_pf, build_progress);
    if (!stored_sample_indices.has_value()) {
      continue;
    }

    const CharCore core(stored_sample_indices->input_slew_idx, stored_sample_indices->output_slew_idx,
                        stored_sample_indices->driven_cap_idx, stored_sample_indices->load_cap_idx, sample_result.delay_ns, power_w, pid,
                        source_boundary_net_switch_power_w);
    const SegmentChar seg_char(core, length_idx);
    _segment_chars.push_back(seg_char);
  }
}

}  // namespace icts
