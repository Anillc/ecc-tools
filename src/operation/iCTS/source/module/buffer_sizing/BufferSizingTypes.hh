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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file BufferSizingTypes.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Explicit tree sizing problem and result data types.
 */

#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace icts::buffer_sizing {

constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();

enum class TreeNodeKind
{
  kSource,
  kBuffer,
  kSink
};

struct BufferCandidate
{
  std::string cell_master;
  double input_cap_pf = 0.0;
  double output_cap_limit_pf = 0.0;
  double area_um2 = 0.0;
  unsigned drive_rank = 0U;
};

struct TreeBuffer
{
  std::size_t node_id = kInvalidIndex;
  std::string inst_name;
  std::string current_master;
  std::vector<BufferCandidate> candidates;
  std::size_t current_candidate_index = 0U;
};

struct TreeNode
{
  TreeNodeKind kind = TreeNodeKind::kSink;
  std::string name;
  std::size_t parent_id = kInvalidIndex;
  std::size_t incoming_net_id = kInvalidIndex;
  std::size_t output_net_id = kInvalidIndex;
  std::size_t buffer_id = kInvalidIndex;
  double sink_pin_cap_pf = 0.0;
};

struct TreeArc
{
  std::size_t child_node_id = kInvalidIndex;
  double length_um = 0.0;
};

struct TreeNet
{
  std::string name;
  std::size_t driver_node_id = kInvalidIndex;
  std::vector<TreeArc> arcs;
  double wire_cap_pf = 0.0;
  double fixed_load_cap_pf = 0.0;
  double baseline_load_cap_pf = 0.0;
  double max_cap_pf = 0.0;
};

struct TreeSizingProblem
{
  std::string clock_name;
  std::vector<TreeNode> nodes;
  std::vector<TreeNet> nets;
  std::vector<TreeBuffer> buffers;
  std::size_t root_node_id = kInvalidIndex;
  double source_input_slew_ns = 0.0;
  double target_skew_ns = 0.0;
  double improvement_epsilon_ns = 1e-6;
  unsigned max_iterations = 64U;
  unsigned max_trial_count = 4096U;
};

struct BufferMutation
{
  std::size_t buffer_id = kInvalidIndex;
  std::size_t node_id = kInvalidIndex;
  std::string inst_name;
  std::string from_master;
  std::string to_master;
  double area_delta_um2 = 0.0;
};

struct TreeTimingPoint
{
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  bool valid = false;
};

struct TreeEvaluation
{
  bool valid = false;
  std::string failure_reason;
  std::vector<TreeTimingPoint> node_timing;
  std::vector<double> net_loads_pf;
  std::vector<std::size_t> cap_violated_net_ids;
  std::size_t min_sink_node_id = kInvalidIndex;
  std::size_t max_sink_node_id = kInvalidIndex;
  double min_sink_arrival_ns = 0.0;
  double max_sink_arrival_ns = 0.0;
  double skew_ns = 0.0;
  double total_area_um2 = 0.0;

  auto hasCapViolation() const -> bool { return !cap_violated_net_ids.empty(); }
};

struct TreeSizingSummary
{
  bool valid = false;
  bool target_met = false;
  bool changed = false;
  std::string stop_reason;
  TreeEvaluation before;
  TreeEvaluation after;
  unsigned iteration_count = 0U;
  unsigned accepted_mutation_count = 0U;
  unsigned rejected_candidate_count = 0U;
  unsigned cap_rejected_count = 0U;
  unsigned trial_count = 0U;
  double total_area_delta_um2 = 0.0;
  std::vector<std::size_t> selected_candidate_by_node;
  std::vector<BufferMutation> mutations;
};

}  // namespace icts::buffer_sizing
