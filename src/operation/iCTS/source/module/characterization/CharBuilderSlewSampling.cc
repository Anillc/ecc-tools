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

#include <glog/logging.h>

#include <optional>
#include <ostream>
#include <vector>

#include "CharBuilder.hh"
#include "CharCore.hh"
#include "Log.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"
#include "adapter/sta/STAAdapter.hh"

namespace icts {
namespace {

constexpr bool kEnableCharPowerSampling = true;

}  // namespace

auto CharBuilder::sampleLoadSlews(unsigned length_idx, const PatternId& pid, const TopologyDesc& topo, double effective_load_pf,
                                  double load_pf, double driven_cap_pf, bool& power_context_ready, BuildProgress& build_progress) -> void
{
  setCharParasitics(topo, effective_load_pf);
  if (kEnableCharPowerSampling && power_context_ready) {
    const bool refreshed_power_load = STA_ADAPTER_INST.refreshCharPowerLoad();
    if (!refreshed_power_load) {
      LOG_WARNING << "CharBuilder: external iPA load refresh failed, remaining samples for this topology use zero power.";
      power_context_ready = false;
      STA_ADAPTER_INST.destroyCharPower();
    }
  }

  bool is_first_slew_sample_for_load = true;
  const unsigned load_cap_idx = get_cap_lattice().coveringIndex(load_pf);
  for (const double input_slew_ns : _slews_to_test) {
    if (is_first_slew_sample_for_load) {
      STA_ADAPTER_INST.prepareCharTimingSample();
    }

    if (is_first_slew_sample_for_load) {
      STA_ADAPTER_INST.setCharBufferInputSlew(input_slew_ns);
    } else {
      STA_ADAPTER_INST.setCharBufferInputSlewIncremental(input_slew_ns);
    }

    if (is_first_slew_sample_for_load) {
      STA_ADAPTER_INST.updateCharTimingSample();
    } else {
      STA_ADAPTER_INST.updateCharTimingIncrementalSample();
    }
    ++build_progress.executed_sta_samples;

    const double delay_ns = STA_ADAPTER_INST.queryCharClockAT(_char_clock_name);
    const unsigned input_slew_idx = get_slew_lattice().coveringIndex(input_slew_ns);

    const double output_slew_ns = STA_ADAPTER_INST.queryCharSlew();

    double power_w = 0.0;
    double source_boundary_net_switch_power_w = 0.0;
    if (kEnableCharPowerSampling && power_context_ready) {
      const bool power_updated = STA_ADAPTER_INST.updateCharPower();
      if (power_updated) {
        power_w = STA_ADAPTER_INST.queryCharPower();
        if (!_temp_net_names.empty()) {
          source_boundary_net_switch_power_w = STA_ADAPTER_INST.queryCharNetSwitchPower(_temp_net_names.front());
        }
      } else {
        LOG_WARNING << "CharBuilder: iPA characterization update failed, remaining samples for this topology use zero power.";
        power_context_ready = false;
        STA_ADAPTER_INST.destroyCharPower();
      }
    }

    const auto stored_sample_indices
        = tryMakeStoredSampleIndices(input_slew_idx, load_cap_idx, output_slew_ns, driven_cap_pf, build_progress);
    is_first_slew_sample_for_load = false;
    if (!stored_sample_indices.has_value()) {
      continue;
    }

    const CharCore core(stored_sample_indices->input_slew_idx, stored_sample_indices->output_slew_idx,
                        stored_sample_indices->driven_cap_idx, stored_sample_indices->load_cap_idx, delay_ns, power_w, pid,
                        source_boundary_net_switch_power_w);
    const SegmentChar seg_char(core, length_idx);
    _segment_chars.push_back(seg_char);
  }
}

}  // namespace icts
