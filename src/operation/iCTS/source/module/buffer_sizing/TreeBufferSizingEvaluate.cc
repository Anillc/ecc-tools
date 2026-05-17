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
 * @file TreeBufferSizingEvaluate.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Tree timing evaluation for critical-branch buffer sizing.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "ValueLattice.hh"
#include "buffer_sizing/BufferSizingTypes.hh"
#include "buffer_sizing/CharTimingLookup.hh"
#include "buffer_sizing/TreeBufferSizing.hh"

namespace icts::buffer_sizing {
namespace {

constexpr double kEpsilon = 1e-12;

auto invalidEvaluation(const std::string& reason) -> TreeEvaluation
{
  TreeEvaluation evaluation;
  evaluation.valid = false;
  evaluation.failure_reason = reason;
  return evaluation;
}

auto finite(double value) -> bool
{
  return std::isfinite(value);
}

auto validateProblem(const TreeSizingProblem& problem) -> std::optional<std::string>
{
  if (problem.nodes.empty()) {
    return "empty_nodes";
  }
  if (problem.root_node_id >= problem.nodes.size() || problem.nodes.at(problem.root_node_id).kind != TreeNodeKind::kSource) {
    return "invalid_root_node";
  }

  bool has_sink = false;
  for (std::size_t node_id = 0U; node_id < problem.nodes.size(); ++node_id) {
    const auto& node = problem.nodes.at(node_id);
    if (node.kind == TreeNodeKind::kSink) {
      has_sink = true;
    }
    if (node.kind == TreeNodeKind::kBuffer) {
      if (node.buffer_id >= problem.buffers.size()) {
        return "invalid_buffer_node";
      }
      if (problem.buffers.at(node.buffer_id).node_id != node_id) {
        return "buffer_node_mismatch";
      }
    }
    if (node_id != problem.root_node_id && node.parent_id >= problem.nodes.size()) {
      return "invalid_parent_node";
    }
  }
  if (!has_sink) {
    return "no_sink_nodes";
  }

  for (const auto& buffer : problem.buffers) {
    if (buffer.node_id >= problem.nodes.size() || buffer.candidates.empty() || buffer.current_candidate_index >= buffer.candidates.size()) {
      return "invalid_buffer";
    }
  }
  for (const auto& net : problem.nets) {
    if (net.driver_node_id >= problem.nodes.size()) {
      return "invalid_net_driver";
    }
    for (const auto& arc : net.arcs) {
      if (arc.child_node_id >= problem.nodes.size()) {
        return "invalid_net_arc";
      }
    }
  }
  return std::nullopt;
}

auto selectedCandidate(const TreeSizingProblem& problem, const TreeNode& node, const std::vector<std::size_t>& selected_candidate_by_node)
    -> const BufferCandidate*
{
  if (node.kind != TreeNodeKind::kBuffer || node.buffer_id == kInvalidIndex || node.buffer_id >= problem.buffers.size()) {
    return nullptr;
  }
  const auto& buffer = problem.buffers.at(node.buffer_id);
  if (buffer.node_id >= selected_candidate_by_node.size()) {
    return nullptr;
  }
  const auto selected_index = selected_candidate_by_node.at(buffer.node_id);
  return selected_index < buffer.candidates.size() ? &buffer.candidates.at(selected_index) : nullptr;
}

auto netLoadCap(const TreeSizingProblem& problem, const TreeNet& net, const std::vector<std::size_t>& selected_candidate_by_node)
    -> std::optional<double>
{
  double load_cap = std::max(0.0, net.fixed_load_cap_pf) + std::max(0.0, net.wire_cap_pf);
  for (const auto& arc : net.arcs) {
    const auto& child = problem.nodes.at(arc.child_node_id);
    if (child.kind == TreeNodeKind::kBuffer) {
      const auto* candidate = selectedCandidate(problem, child, selected_candidate_by_node);
      if (candidate == nullptr) {
        return std::nullopt;
      }
      load_cap += std::max(0.0, candidate->input_cap_pf);
    } else if (child.kind == TreeNodeKind::kSink) {
      load_cap += std::max(0.0, child.sink_pin_cap_pf);
    }
  }
  return load_cap;
}

auto capLegal(const TreeNet& net, double load_cap_pf) -> bool
{
  if (net.max_cap_pf <= 0.0) {
    return true;
  }
  if (net.baseline_load_cap_pf <= net.max_cap_pf + kEpsilon) {
    return load_cap_pf <= net.max_cap_pf + kEpsilon;
  }
  return load_cap_pf <= net.baseline_load_cap_pf + kEpsilon;
}

auto outputLoadCap(const TreeNode& node, const std::vector<double>& net_loads_pf) -> std::optional<double>
{
  if (node.output_net_id == kInvalidIndex) {
    return 0.0;
  }
  if (node.output_net_id >= net_loads_pf.size()) {
    return std::nullopt;
  }
  return net_loads_pf.at(node.output_net_id);
}

auto selectedArea(const TreeSizingProblem& problem, const std::vector<std::size_t>& selected_candidate_by_node) -> std::optional<double>
{
  double area = 0.0;
  for (const auto& buffer : problem.buffers) {
    if (buffer.node_id >= selected_candidate_by_node.size()) {
      return std::nullopt;
    }
    const auto selected = selected_candidate_by_node.at(buffer.node_id);
    if (selected >= buffer.candidates.size()) {
      return std::nullopt;
    }
    area += std::max(0.0, buffer.candidates.at(selected).area_um2);
  }
  return area;
}

auto sourceSlew(const TreeSizingProblem& problem, const CharTimingLookup& timing_lookup) -> double
{
  if (problem.source_input_slew_ns > 0.0) {
    return problem.source_input_slew_ns;
  }
  return timing_lookup.get_slew_lattice().isValid() ? timing_lookup.get_slew_lattice().stepValue() : 0.0;
}

auto queryArcTiming(const TreeSizingProblem& problem, const CharTimingLookup& timing_lookup,
                    const std::vector<std::size_t>& selected_candidate_by_node, const std::vector<double>& net_loads_pf,
                    const TreeTimingPoint& parent_timing, const TreeArc& arc) -> CharTimingResult
{
  const auto& child = problem.nodes.at(arc.child_node_id);
  if (child.kind == TreeNodeKind::kBuffer) {
    const auto* candidate = selectedCandidate(problem, child, selected_candidate_by_node);
    const auto child_output_load = outputLoadCap(child, net_loads_pf);
    if (candidate == nullptr || !child_output_load.has_value()) {
      return CharTimingResult{.success = false, .failure_reason = "invalid_buffer_arc"};
    }
    return timing_lookup.lookup(CharTimingQuery{
        .arc_kind = CharArcKind::kTerminalBuffer,
        .terminal_buffer_master = candidate->cell_master,
        .length_um = arc.length_um,
        .input_slew_ns = parent_timing.slew_ns,
        .load_cap_pf = std::max(timing_lookup.get_cap_lattice().stepValue(), *child_output_load),
    });
  }

  return timing_lookup.lookup(CharTimingQuery{
      .arc_kind = CharArcKind::kWire,
      .terminal_buffer_master = "",
      .length_um = arc.length_um,
      .input_slew_ns = parent_timing.slew_ns,
      .load_cap_pf = std::max(timing_lookup.get_cap_lattice().stepValue(), child.sink_pin_cap_pf),
  });
}

auto fillLoadCaps(const TreeSizingProblem& problem, const std::vector<std::size_t>& selected_candidate_by_node, TreeEvaluation& evaluation)
    -> std::optional<std::string>
{
  for (std::size_t net_id = 0U; net_id < problem.nets.size(); ++net_id) {
    const auto load_cap = netLoadCap(problem, problem.nets.at(net_id), selected_candidate_by_node);
    if (!load_cap.has_value()) {
      return "invalid_net_load";
    }
    evaluation.net_loads_pf.at(net_id) = *load_cap;
    if (!capLegal(problem.nets.at(net_id), *load_cap)) {
      evaluation.cap_violated_net_ids.push_back(net_id);
    }
  }

  return std::nullopt;
}

auto propagateTiming(const TreeSizingProblem& problem, const CharTimingLookup& timing_lookup,
                     const std::vector<std::size_t>& selected_candidate_by_node, TreeEvaluation& evaluation) -> std::optional<std::string>
{
  std::vector<std::size_t> pending{problem.root_node_id};
  while (!pending.empty()) {
    const auto node_id = pending.back();
    pending.pop_back();
    const auto parent_timing = evaluation.node_timing.at(node_id);
    if (!parent_timing.valid) {
      continue;
    }
    for (const auto& net : problem.nets) {
      if (net.driver_node_id != node_id) {
        continue;
      }
      for (const auto& arc : net.arcs) {
        const auto timing = queryArcTiming(problem, timing_lookup, selected_candidate_by_node, evaluation.net_loads_pf, parent_timing, arc);
        if (!timing.success || !finite(timing.delay_ns) || !finite(timing.output_slew_ns)) {
          return timing.failure_reason.empty() ? std::string{"char_timing_lookup_failed"} : timing.failure_reason;
        }
        evaluation.node_timing.at(arc.child_node_id) = TreeTimingPoint{
            .arrival_ns = parent_timing.arrival_ns + std::max(0.0, timing.delay_ns),
            .slew_ns = std::max(0.0, timing.output_slew_ns),
            .valid = true,
        };
        pending.push_back(arc.child_node_id);
      }
    }
  }
  return std::nullopt;
}

auto fillSinkSkew(const TreeSizingProblem& problem, TreeEvaluation& evaluation) -> std::optional<std::string>
{
  double min_arrival = std::numeric_limits<double>::infinity();
  double max_arrival = -std::numeric_limits<double>::infinity();
  for (std::size_t node_id = 0U; node_id < problem.nodes.size(); ++node_id) {
    if (problem.nodes.at(node_id).kind != TreeNodeKind::kSink) {
      continue;
    }
    const auto timing = evaluation.node_timing.at(node_id);
    if (!timing.valid || !finite(timing.arrival_ns)) {
      return "unreached_sink";
    }
    if (timing.arrival_ns < min_arrival) {
      min_arrival = timing.arrival_ns;
      evaluation.min_sink_node_id = node_id;
    }
    if (timing.arrival_ns > max_arrival) {
      max_arrival = timing.arrival_ns;
      evaluation.max_sink_node_id = node_id;
    }
  }
  if (!finite(min_arrival) || !finite(max_arrival)) {
    return "invalid_sink_arrival";
  }

  evaluation.min_sink_arrival_ns = min_arrival;
  evaluation.max_sink_arrival_ns = max_arrival;
  evaluation.skew_ns = max_arrival - min_arrival;
  evaluation.valid = true;
  return std::nullopt;
}

}  // namespace

auto TreeBufferSizing::evaluate(const TreeSizingProblem& problem, const CharTimingLookup& timing_lookup,
                                const std::vector<std::size_t>& selected_candidate_by_node) -> TreeEvaluation
{
  if (const auto invalid = validateProblem(problem); invalid.has_value()) {
    return invalidEvaluation(*invalid);
  }
  if (!timing_lookup.isReady()) {
    return invalidEvaluation("timing_lookup_not_ready");
  }
  if (selected_candidate_by_node.size() < problem.nodes.size()) {
    return invalidEvaluation("selection_size_mismatch");
  }

  TreeEvaluation evaluation;
  evaluation.node_timing.assign(problem.nodes.size(), TreeTimingPoint{});
  evaluation.net_loads_pf.assign(problem.nets.size(), 0.0);

  const auto area = selectedArea(problem, selected_candidate_by_node);
  if (!area.has_value()) {
    return invalidEvaluation("invalid_selection_area");
  }
  evaluation.total_area_um2 = *area;

  if (const auto load_error = fillLoadCaps(problem, selected_candidate_by_node, evaluation); load_error.has_value()) {
    return invalidEvaluation(*load_error);
  }
  evaluation.node_timing.at(problem.root_node_id) = TreeTimingPoint{
      .arrival_ns = 0.0,
      .slew_ns = sourceSlew(problem, timing_lookup),
      .valid = true,
  };
  if (const auto timing_error = propagateTiming(problem, timing_lookup, selected_candidate_by_node, evaluation); timing_error.has_value()) {
    return invalidEvaluation(*timing_error);
  }
  if (const auto sink_error = fillSinkSkew(problem, evaluation); sink_error.has_value()) {
    return invalidEvaluation(*sink_error);
  }
  return evaluation;
}

}  // namespace icts::buffer_sizing
