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
 * @file CharBuilderSampling.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief iSTA/iPA sampling and SegmentChar storage for one characterization topology.
 */

#include <glog/logging.h>

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

#include "CharBuilder.hh"
#include "Log.hh"

namespace icts {
namespace {

constexpr std::size_t kCharProgressLogStride = 32U;

}  // namespace

auto CharBuilder::characterizeTopology(unsigned length_idx, const TopologyDesc& topo, const std::vector<std::string>& buf_masters,
                                       BuildProgress& build_progress) -> void
{
  ++build_progress.evaluated_patterns;

  double total_length_um = 0.0;
  for (const double seg_len : topo.wire_segments_um) {
    total_length_um += seg_len;
  }

  const auto pid = storeBufferingPattern(length_idx, topo, buf_masters, total_length_um);

  const PatternFeasibility feasibility = analyzePatternFeasibility(topo, buf_masters);
  if (!feasibility.is_pattern_feasible) {
    ++build_progress.skipped_patterns_infeasible;
    if ((build_progress.evaluated_patterns % kCharProgressLogStride) == 0U) {
      LOG_INFO << "CharBuilder: wire_length=" << total_length_um << " um progress " << build_progress.evaluated_patterns << "/"
               << build_progress.estimated_patterns << " patterns"
               << " (feasible=" << build_progress.feasible_patterns << ", skipped=" << build_progress.skipped_patterns_infeasible
               << ", executed_sta_samples=" << build_progress.executed_sta_samples << ")";
    }
    ++_next_pattern_id;
    return;
  }
  ++build_progress.feasible_patterns;

  sampleFeasibleTopology(length_idx, pid, topo, buf_masters, feasibility, build_progress);
  if ((build_progress.evaluated_patterns % kCharProgressLogStride) == 0U) {
    LOG_INFO << "CharBuilder: wire_length=" << total_length_um << " um progress " << build_progress.evaluated_patterns << "/"
             << build_progress.estimated_patterns << " patterns"
             << " (feasible=" << build_progress.feasible_patterns << ", skipped=" << build_progress.skipped_patterns_infeasible
             << ", executed_sta_samples=" << build_progress.executed_sta_samples
             << ", skipped_sta_samples=" << build_progress.skipped_sta_samples << ")";
  }
  ++_next_pattern_id;
}

}  // namespace icts
