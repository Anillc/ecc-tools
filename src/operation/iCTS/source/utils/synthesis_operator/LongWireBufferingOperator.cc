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
 * @file LongWireBufferingOperator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "LongWireBufferingOperator.hh"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <ranges>
#include <sstream>

#include "TimingPropagator.hh"
#include "TreeBuilder.hh"
#include "log/Log.hh"

namespace icts {

namespace {
constexpr double kMetricEpsilon = 1e-12;

struct LevelBufferingPlan
{
  double max_wire_length = 0.0;
  size_t edge_count = 0;
  int inserted_count = 0;
};

double calcWireLength(Pin* parent_driver, Pin* child_load)
{
  const double wire_distance = static_cast<double>(TimingPropagator::calcDist(parent_driver, child_load));
  const int db_unit = TimingPropagator::getDbUnit();
  return db_unit > 0 ? wire_distance / db_unit : wire_distance;
}

int computeInsertedCount(double max_wire_length, double min_buffering_length)
{
  LOG_FATAL_IF(!std::isfinite(max_wire_length) || max_wire_length < 0.0)
      << "Long-wire buffering got invalid max wire length: " << max_wire_length;
  if (max_wire_length <= min_buffering_length + kMetricEpsilon) {
    return 0;
  }

  const double required_segment_count = std::ceil(max_wire_length / min_buffering_length);
  LOG_FATAL_IF(required_segment_count >= static_cast<double>(std::numeric_limits<int>::max()))
      << "Long-wire buffering segment count overflow: " << required_segment_count;

  return std::max(0, static_cast<int>(required_segment_count) - 1);
}

std::map<int, LevelBufferingPlan> buildLevelBufferingPlans(const SolverPipelineState& state, size_t original_record_count,
                                                           const SolverNetBuilder& net_builder)
{
  std::map<int, LevelBufferingPlan> level_plans;
  for (size_t record_idx = 0; record_idx < original_record_count; ++record_idx) {
    const auto& record = state.net_records[record_idx];
    if (!record.allow_long_wire_buffering) {
      continue;
    }

    auto* net = record.net;
    auto* parent_driver = net == nullptr ? nullptr : net->get_driver_pin();
    if (net == nullptr || parent_driver == nullptr) {
      continue;
    }

    for (Pin* child_load : net->get_load_pins()) {
      if (child_load == nullptr) {
        continue;
      }

      auto* child_inst = child_load->get_inst();
      if (child_inst == nullptr || !child_inst->isBuffer()) {
        continue;
      }
      if (child_load->get_parent() != parent_driver) {
        continue;
      }

      const int child_depth = net_builder.childDepth(state, child_load);
      LOG_FATAL_IF(child_depth < 0) << "Net [" << state.net_name << "] long-wire buffering missing sizing depth for child buffer "
                                    << child_inst->get_name();

      auto& plan = level_plans[child_depth];
      plan.max_wire_length = std::max(plan.max_wire_length, calcWireLength(parent_driver, child_load));
      ++plan.edge_count;
    }
  }

  for (auto& [depth, plan] : level_plans) {
    (void) depth;
    plan.inserted_count = computeInsertedCount(plan.max_wire_length, state.min_buffering_length);
  }
  return level_plans;
}

std::string formatLevelPlanSummary(const std::map<int, LevelBufferingPlan>& level_plans)
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  bool first = true;
  for (const auto& [depth, plan] : level_plans) {
    if (plan.inserted_count <= 0) {
      continue;
    }
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << "d" << depth << ":max=" << plan.max_wire_length << ",edges=" << plan.edge_count << ",insert=" << plan.inserted_count;
  }
  return first ? "none" : oss.str();
}
}  // namespace

void LongWireBufferingOperator::run(SolverPipelineState& state) const
{
  if (state.min_buffering_length <= 0.0) {
    LOG_WARNING << "Net [" << state.net_name
                << "] skip long-wire buffering because min_buffering_length <= 0: " << state.min_buffering_length;
    return;
  }

  const size_t original_record_count = state.net_records.size();
  const auto level_plans = buildLevelBufferingPlans(state, original_record_count, _net_builder);
  size_t affected_edges = 0;
  size_t affected_levels = 0;
  size_t inserted_buffers = 0;
  std::ranges::for_each(level_plans, [&](const auto& plan_entry) { affected_levels += plan_entry.second.inserted_count > 0 ? 1 : 0; });

  for (size_t record_idx = 0; record_idx < original_record_count; ++record_idx) {
    const auto& record = state.net_records[record_idx];
    if (!record.allow_long_wire_buffering) {
      continue;
    }

    auto* net = record.net;
    if (net == nullptr) {
      continue;
    }
    auto* parent_driver = net->get_driver_pin();
    if (parent_driver == nullptr) {
      continue;
    }
    auto original_loads = net->get_load_pins();
    auto updated_loads = original_loads;
    bool net_changed = false;

    for (Pin* child_load : original_loads) {
      if (child_load == nullptr) {
        continue;
      }

      auto* child_inst = child_load->get_inst();
      if (child_inst == nullptr || !child_inst->isBuffer()) {
        continue;
      }
      if (child_load->get_parent() != parent_driver) {
        continue;
      }

      const int sizing_depth = _net_builder.childDepth(state, child_load);
      LOG_FATAL_IF(sizing_depth < 0) << "Net [" << state.net_name << "] long-wire buffering missing sizing depth for child buffer "
                                     << child_inst->get_name();

      const auto plan_it = level_plans.find(sizing_depth);
      if (plan_it == level_plans.end() || plan_it->second.inserted_count <= 0) {
        continue;
      }

      const int inserted_count = plan_it->second.inserted_count;
      const int segment_count = inserted_count + 1;
      const auto parent_loc = parent_driver->get_location();
      const auto child_loc = child_load->get_location();

      TreeBuilder::disconnect(parent_driver, child_load);
      updated_loads.erase(std::remove(updated_loads.begin(), updated_loads.end(), child_load), updated_loads.end());

      Pin* upstream_driver = parent_driver;
      Pin* first_inserted_load = nullptr;
      const auto child_master = child_inst->get_cell_master();

      for (int segment_idx = 1; segment_idx <= inserted_count; ++segment_idx) {
        const double ratio = 1.0 * segment_idx / segment_count;
        const int x = static_cast<int>(std::lround(parent_loc.x() + (child_loc.x() - parent_loc.x()) * ratio));
        const int y = static_cast<int>(std::lround(parent_loc.y() + (child_loc.y() - parent_loc.y()) * ratio));
        auto break_name = ComposeSolverName(state.net_name, "_break_", sizing_depth, "_", segment_idx, "_", _runtime.genId());
        auto* break_buf = TreeBuilder::genBufInst(break_name, Point(x, y));
        break_buf->set_cell_master(child_master);
        _net_builder.registerBuffer(state, break_buf, sizing_depth);

        auto* break_load = break_buf->get_load_pin();
        auto* break_driver = break_buf->get_driver_pin();
        TreeBuilder::directConnectTree(upstream_driver, break_load);

        if (segment_idx == 1) {
          first_inserted_load = break_load;
        } else {
          _net_builder.createNetRecord(state, upstream_driver, {break_load}, "break");
        }

        upstream_driver = break_driver;
      }

      LOG_FATAL_IF(first_inserted_load == nullptr)
          << "Net [" << state.net_name << "] breakLongWire failed to create inserted buffers for edge " << parent_driver->get_name()
          << " -> " << child_load->get_name();

      updated_loads.push_back(first_inserted_load);
      TreeBuilder::directConnectTree(upstream_driver, child_load);
      _net_builder.createNetRecord(state, upstream_driver, {child_load}, "break");

      net_changed = true;
      ++affected_edges;
      inserted_buffers += inserted_count;
    }

    if (net_changed) {
      net->set_load_pins(updated_loads);
      std::ranges::for_each(updated_loads, [net](Pin* load) { load->set_net(net); });
    }
  }

  std::ostringstream break_summary;
  break_summary << std::fixed << std::setprecision(3);
  break_summary << "Net [" << state.net_name << "] level-uniform long-wire buffering summary => threshold: " << state.min_buffering_length
                << ", affected levels: " << affected_levels << ", affected edges: " << affected_edges
                << ", inserted buffers: " << inserted_buffers << ", levels: [" << formatLevelPlanSummary(level_plans) << "]";
  LOG_INFO << break_summary.str();
  _runtime.saveToLog(break_summary.str());
}

}  // namespace icts
