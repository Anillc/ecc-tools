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
 * @file Solver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "CtsPin.hh"
#include "Inst.hh"
#include "Net.hh"

namespace icts {

class Solver
{
 public:
  Solver() = delete;
  Solver(const std::string& net_name, CtsPin* cts_driver, const std::vector<CtsPin*>& cts_pins)
      : _net_name(net_name), _cts_driver(cts_driver), _cts_pins(cts_pins)
  {
    auto* config = CTSAPIInst.get_config();
    _min_buffering_length = config->get_min_buffering_length();
  }

  ~Solver() = default;

  void run();
  std::vector<Net*> get_solver_nets() const { return _nets; }

 private:
  struct NetRecord
  {
    Net* net = nullptr;
    int evaluation_order = -1;
  };

  struct FeasibilityResult
  {
    bool feasible = true;
    bool skew = false;
    bool buffer_slew = false;
    bool sink_slew = false;
    bool cap = false;
    bool length = false;
    bool fanout = false;
    double skew_over = 0.0;
    double buffer_slew_over = 0.0;
    double sink_slew_over = 0.0;
    double cap_over = 0.0;
    double length_over = 0.0;
    double fanout_over = 0.0;

    size_t violationCount() const
    {
      return static_cast<size_t>(skew) + static_cast<size_t>(buffer_slew) + static_cast<size_t>(sink_slew) + static_cast<size_t>(cap)
             + static_cast<size_t>(length) + static_cast<size_t>(fanout);
    }

    double totalViolation() const
    {
      return skew_over + buffer_slew_over + sink_slew_over + cap_over + length_over + fanout_over;
    }
  };

  struct SizingCandidate
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
    FeasibilityResult feasibility;
  };

  struct SizingSearchStats
  {
    size_t evaluated = 0;
    size_t feasible = 0;
    size_t rejected_skew = 0;
    size_t rejected_buffer_slew = 0;
    size_t rejected_sink_slew = 0;
    size_t rejected_cap = 0;
    size_t rejected_length = 0;
    size_t rejected_fanout = 0;

    void accumulate(const FeasibilityResult& result)
    {
      rejected_skew += result.skew ? 1 : 0;
      rejected_buffer_slew += result.buffer_slew ? 1 : 0;
      rejected_sink_slew += result.sink_slew ? 1 : 0;
      rejected_cap += result.cap ? 1 : 0;
      rejected_length += result.length ? 1 : 0;
      rejected_fanout += result.fanout ? 1 : 0;
    }
  };

  void init();
  void buildLeafBuffers();
  void buildTopology();
  void buildSubTree(Pin* parent_driver, const std::vector<Pin*>& subtree_loads, int depth);
  void breakLongWire();
  Net* connectNet(Pin* driver, const std::vector<Pin*>& loads, const std::string& stage_tag);
  Net* createNetRecord(Pin* driver, const std::vector<Pin*>& loads, const std::string& stage_tag);
  void registerBuffer(Inst* buffer, int depth);
  void finalizeLeafDepth(Pin* leaf_load, int depth);
  int childDepth(Pin* child) const;
  void refreshNetEvaluationOrder();
  int computeNetEvaluationOrder(Net* net, std::unordered_map<Net*, int>& cache) const;

  void optimizeLevelSizing();
  void enumerateLevelSizing(size_t depth, size_t max_lib_index, std::vector<size_t>& current_assignment,
                            std::vector<SizingCandidate>& feasible_candidates, std::vector<SizingCandidate>& all_candidates,
                            SizingSearchStats& stats);
  SizingCandidate evaluateSizing(const std::vector<size_t>& level_lib_indices, SizingSearchStats& stats);
  FeasibilityResult checkSizingFeasibility() const;
  void applyLevelSizing(const std::vector<size_t>& level_lib_indices);
  void reevaluateTree();
  void normalizeCandidates(std::vector<SizingCandidate>& candidates) const;
  static bool dominates(const SizingCandidate& lhs, const SizingCandidate& rhs);
  size_t selectBalancedCandidate(const std::vector<SizingCandidate>& candidates) const;
  size_t countParetoCandidates(const std::vector<SizingCandidate>& candidates) const;
  std::string formatFeasibilitySummary(const FeasibilityResult& result) const;

  double totalBufferArea() const;
  double totalBufferPower() const;
  void logTopologySummary() const;
  void logSizingSummary(const SizingCandidate& candidate) const;

  std::string _net_name;
  CtsPin* _cts_driver = nullptr;
  std::vector<CtsPin*> _cts_pins;
  std::vector<Pin*> _sink_pins;
  std::vector<Pin*> _leaf_load_pins;
  Pin* _driver = nullptr;
  std::vector<Net*> _nets;
  std::vector<NetRecord> _net_records;
  std::unordered_map<Inst*, int> _buffer_depths;
  std::vector<std::vector<Inst*>> _buffers_by_depth;
  int _max_depth = -1;
  double _min_buffering_length = 150.0;
};

}  // namespace icts
