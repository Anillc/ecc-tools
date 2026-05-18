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
 * @file CharBuilderStaSampling.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Characterization STA/iPA sample execution for feasible topologies.
 */

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "CharBuilder.hh"
#include "ValueLattice.hh"
#include "adapter/sta/STAAdapter.hh"

namespace icts {
struct PatternId;
}  // namespace icts

namespace icts {
namespace {

constexpr bool kEnableCharPowerSampling = true;

}  // namespace

auto CharBuilder::sampleFeasibleTopology(unsigned length_idx, const PatternId& pid, const TopologyDesc& topo,
                                         const std::vector<std::string>& buf_masters, const PatternFeasibility& feasibility,
                                         BuildProgress& build_progress) -> void
{
  createCharCircuit(topo, buf_masters);

  bool power_context_ready = kEnableCharPowerSampling;

  for (const double load_pf : _loads_to_test) {
    if (load_pf > feasibility.max_load_pf + kCapFeasibilityEpsilonPf) {
      ++build_progress.skipped_load_points;
      build_progress.skipped_sta_samples += _slews_to_test.size();
      continue;
    }

    const double effective_load = load_pf - _sink_input_cap_pf;
    if (effective_load < 0.0) {
      ++build_progress.skipped_load_points;
      build_progress.skipped_sta_samples += _slews_to_test.size();
      continue;
    }

    double driven_cap_pf = 0.0;
    if (!buf_masters.empty()) {
      driven_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(buf_masters.front());
      driven_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, topo.wire_segments_um.front(), _wire_width);
    } else {
      driven_cap_pf = load_pf;
      for (const double seg_len_um : topo.wire_segments_um) {
        driven_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);
      }
    }

    build_progress.max_observed_driven_cap_pf = std::max(build_progress.max_observed_driven_cap_pf, driven_cap_pf);
    build_progress.max_observed_driven_cap_idx
        = std::max(build_progress.max_observed_driven_cap_idx, get_cap_lattice().coveringIndex(driven_cap_pf));
    const auto driven_cap_idx = get_cap_lattice().tryObservedIndex(driven_cap_pf);
    if (!driven_cap_idx.has_value()) {
      ++build_progress.skipped_load_points;
      build_progress.skipped_sta_samples += _slews_to_test.size();
      build_progress.driven_cap_overflow_samples += _slews_to_test.size();
      ++build_progress.driven_cap_overflow_load_points;
      continue;
    }

    sampleLoadSlews(length_idx, pid, topo, effective_load, load_pf, driven_cap_pf, power_context_ready, build_progress);
  }

  destroyCharCircuit();
}

}  // namespace icts
