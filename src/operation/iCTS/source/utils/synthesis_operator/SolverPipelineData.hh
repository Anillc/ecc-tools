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
 * @file SolverPipelineData.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CtsPin.hh"
#include "Inst.hh"
#include "Net.hh"

namespace icts {

struct SolverNetRecord
{
  Net* net = nullptr;
  int evaluation_order = -1;
  bool allow_long_wire_buffering = true;
};

struct SolverFeasibilityResult
{
  bool feasible = true;
  bool skew = false;
  bool buffer_slew = false;
  bool sink_slew = false;
  bool cap = false;
  bool fanout = false;
  double skew_over = 0.0;
  double buffer_slew_over = 0.0;
  double sink_slew_over = 0.0;
  double cap_over = 0.0;
  double fanout_over = 0.0;

  size_t violationCount() const
  {
    return static_cast<size_t>(skew) + static_cast<size_t>(buffer_slew) + static_cast<size_t>(sink_slew) + static_cast<size_t>(cap)
           + static_cast<size_t>(fanout);
  }

  double totalViolation() const { return skew_over + buffer_slew_over + sink_slew_over + cap_over + fanout_over; }
};

struct SolverSizingCandidate
{
  std::vector<size_t> level_lib_indices;
  double delay = 0.0;
  double area = 0.0;
  double power = 0.0;
  double skew = 0.0;
  double delay_norm = 0.0;
  double area_norm = 0.0;
  double power_norm = 0.0;
  double distance_to_ideal = 0.0;
  bool feasible = true;
  size_t violated_constraints = 0;
  double violation_score = 0.0;
  SolverFeasibilityResult feasibility;
};

struct SolverSizingSearchStats
{
  size_t evaluated = 0;
  size_t feasible = 0;
  size_t rejected_skew = 0;
  size_t rejected_buffer_slew = 0;
  size_t rejected_sink_slew = 0;
  size_t rejected_cap = 0;
  size_t rejected_fanout = 0;

  void accumulate(const SolverFeasibilityResult& result)
  {
    rejected_skew += result.skew ? 1 : 0;
    rejected_buffer_slew += result.buffer_slew ? 1 : 0;
    rejected_sink_slew += result.sink_slew ? 1 : 0;
    rejected_cap += result.cap ? 1 : 0;
    rejected_fanout += result.fanout ? 1 : 0;
  }
};

struct SolverPipelineState
{
  std::string net_name;
  CtsPin* cts_driver = nullptr;
  std::vector<CtsPin*> cts_pins;

  std::vector<Pin*> sink_pins;
  std::vector<Pin*> leaf_load_pins;
  Pin* driver = nullptr;
  std::vector<Net*> nets;
  std::vector<SolverNetRecord> net_records;
  std::unordered_map<Inst*, int> buffer_depths;
  std::vector<std::vector<Inst*>> buffers_by_depth;
  int max_depth = -1;
  double min_buffering_length = 150.0;

  void resetGeneratedData()
  {
    sink_pins.clear();
    leaf_load_pins.clear();
    driver = nullptr;
    nets.clear();
    net_records.clear();
    buffer_depths.clear();
    buffers_by_depth.clear();
    max_depth = -1;
  }
};

template <typename... Args>
inline std::string ComposeSolverName(const std::string& net_name, Args&&... args)
{
  std::ostringstream oss;
  oss << net_name;
  (oss << ... << std::forward<Args>(args));
  return oss.str();
}

}  // namespace icts
