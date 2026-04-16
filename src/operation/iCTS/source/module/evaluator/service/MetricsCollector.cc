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
 * @file MetricsCollector.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "service/MetricsCollector.hh"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "CtsDesign.hh"
#include "Net.hh"
#include "Pin.hh"
#include "context/EvaluatorRuntimeContext.hh"
#include "log/Log.hh"

namespace icts {

namespace {

struct TreeNode
{
  std::string name;
  int depth = -1;
  TreeNode* parent = nullptr;
  std::vector<TreeNode*> children;
};

}  // namespace

CtsNet* MetricsCollector::resolveDrivenNet(CtsInstance* inst)
{
  if (inst == nullptr || inst->get_type() == CtsInstanceType::kSink) {
    return nullptr;
  }

  auto* driver_pin = inst->get_out_pin();
  return driver_pin != nullptr ? driver_pin->get_net() : nullptr;
}

void MetricsCollector::transferEvalNets(const EvaluatorRuntimeContextInterface& context, std::vector<EvalNet>& eval_nets) const
{
  eval_nets.clear();
  auto* design = context.getDesign();
  auto& clk_nets = design->get_nets();
  for (auto* clk_net : clk_nets) {
    eval_nets.emplace_back(EvalNet(clk_net));
  }
}

void MetricsCollector::initLevel(const EvaluatorRuntimeContextInterface& context) const
{
  auto* design = context.getDesign();
  auto& clk_nets = design->get_nets();
  for (auto* clk_net : clk_nets) {
    recursiveSetLevel(clk_net);
  }
}

void MetricsCollector::recursiveSetLevel(CtsNet* net) const
{
  if (net == nullptr) {
    return;
  }
  auto* driver = net->get_driver_inst();
  if (driver != nullptr && driver->get_level() > 0) {
    return;
  }

  auto loads = net->get_load_insts();
  int max_level = 0;
  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    if (load->get_type() == CtsInstanceType::kSink) {
      load->set_level(1);
      max_level = std::max(1, max_level);
      continue;
    }

    auto* sub_net = resolveDrivenNet(load);
    if (sub_net == nullptr) {
      LOG_WARNING << "Driven net is null for load instance " << load->get_name() << " while setting levels for parent net "
                  << net->get_net_name();
      max_level = std::max(load->get_level(), max_level);
      continue;
    }
    recursiveSetLevel(sub_net);
    max_level = std::max(load->get_level(), max_level);
  }

  if (driver != nullptr) {
    driver->set_level(max_level + 1);
  } else if (!loads.empty()) {
    LOG_WARNING << "Driver instance is null for net: " << net->get_net_name() << ", propagate child levels only.";
  }
}

void MetricsCollector::calcInfo(const EvaluatorRuntimeContextInterface& context, const std::vector<EvalNet>& eval_nets,
                                EvaluatorMetrics& metrics) const
{
  metrics = EvaluatorMetrics{};

  calcWL(context, eval_nets, metrics);
  calcCellDist(context, eval_nets, metrics);
  calcCellStats(context, metrics);
  calcNetLevel(eval_nets, metrics);
  calcPathBufStats(context, metrics);
}

void MetricsCollector::calcWL(const EvaluatorRuntimeContextInterface& context, const std::vector<EvalNet>& eval_nets,
                              EvaluatorMetrics& metrics) const
{
  for (const auto& eval_net : eval_nets) {
    auto* net = context.findSynthesisNet(eval_net.get_name());
    if (net == nullptr) {
      continue;
    }
    auto* driver_pin = net->get_driver_pin();
    if (driver_pin == nullptr) {
      continue;
    }
    double net_len = driver_pin->get_sub_len();
    double hpwl_net_len = eval_net.getHPWL(context.getDbUnit());

    auto type = eval_net.netType([&](const std::string& net_name) { return context.isTop(net_name); });
    switch (type) {
      case NetType::kTop:
        metrics.top_wire_len += net_len;
        metrics.hpwl_top_wire_len += hpwl_net_len;
        break;
      case NetType::kTrunk:
        metrics.trunk_wire_len += net_len;
        metrics.hpwl_trunk_wire_len += hpwl_net_len;
        break;
      case NetType::kLeaf:
        metrics.leaf_wire_len += net_len;
        metrics.hpwl_leaf_wire_len += hpwl_net_len;
        break;
      default:
        break;
    }

    metrics.total_wire_len += net_len;
    metrics.hpwl_total_wire_len += hpwl_net_len;
    metrics.max_net_len = std::max(metrics.max_net_len, net_len);
    metrics.hpwl_max_net_len = std::max(metrics.hpwl_max_net_len, hpwl_net_len);
  }
}

void MetricsCollector::calcCellDist(const EvaluatorRuntimeContextInterface& context, const std::vector<EvalNet>& eval_nets,
                                    EvaluatorMetrics& metrics) const
{
  for (const auto& eval_net : eval_nets) {
    auto* net = context.findSynthesisNet(eval_net.get_name());
    if (net == nullptr) {
      continue;
    }
    if (!eval_net.is_newly()) {
      continue;
    }

    auto* driver = eval_net.get_driver();
    if (driver == nullptr) {
      continue;
    }

    auto cell_master = driver->get_cell_master();
    if (cell_master.empty() || !context.cellLibExist(cell_master)) {
      continue;
    }
    if (metrics.cell_dist_map.count(cell_master) == 0) {
      metrics.cell_dist_map[cell_master] = 1;
    } else {
      metrics.cell_dist_map[cell_master]++;
    }
  }
}

void MetricsCollector::calcCellStats(const EvaluatorRuntimeContextInterface& context, EvaluatorMetrics& metrics) const
{
  for (const auto& [cell_master, count] : metrics.cell_dist_map) {
    auto cell_type = context.getCellType(cell_master);
    auto cell_area = context.getCellArea(cell_master);
    auto cell_cap = context.getCellCap(cell_master);

    if (metrics.cell_stats_map.count(cell_type) == 0) {
      metrics.cell_stats_map[cell_type] = {count, cell_area * count, cell_cap * count};
    } else {
      metrics.cell_stats_map[cell_type].total_num += count;
      metrics.cell_stats_map[cell_type].total_area += cell_area * count;
      metrics.cell_stats_map[cell_type].total_cap += cell_cap * count;
    }
  }
}

void MetricsCollector::calcNetLevel(const std::vector<EvalNet>& eval_nets, EvaluatorMetrics& metrics) const
{
  for (const auto& eval_net : eval_nets) {
    if (!eval_net.is_newly()) {
      continue;
    }
    auto* driver = eval_net.get_driver();
    if (driver == nullptr) {
      continue;
    }
    if (metrics.net_level_map.count(driver->get_level()) == 0) {
      metrics.net_level_map[driver->get_level()] = 1;
    } else {
      metrics.net_level_map[driver->get_level()]++;
    }
  }
}

void MetricsCollector::calcPathBufStats(const EvaluatorRuntimeContextInterface& context, EvaluatorMetrics& metrics) const
{
  metrics.path_infos.clear();

  std::unordered_map<std::string, std::unique_ptr<TreeNode>> owned_nodes;
  std::unordered_map<std::string, TreeNode*> name_to_node;
  auto gen_node = [&](const std::string& node_name) -> TreeNode* {
    if (name_to_node.count(node_name) == 0) {
      auto node = std::make_unique<TreeNode>();
      node->name = node_name;
      auto* node_ptr = node.get();
      owned_nodes[node_name] = std::move(node);
      name_to_node[node_name] = node_ptr;
    }
    return name_to_node[node_name];
  };

  auto* design = context.getDesign();
  auto& clk_nets = design->get_nets();
  for (auto* clk_net : clk_nets) {
    auto& pins = clk_net->get_pins();
    auto* driver_pin = clk_net->get_driver_pin();
    if (driver_pin == nullptr) {
      LOG_WARNING << "Cannot resolve driver pin while collecting path statistics for net " << clk_net->get_net_name();
      continue;
    }

    auto driver_name = driver_pin->get_instance() != nullptr ? driver_pin->get_instance()->get_name() : clk_net->get_net_name();
    auto* driver_node = gen_node(driver_name);

    std::unordered_set<std::string> load_names;
    for (auto* pin : pins) {
      if (pin == nullptr || pin == driver_pin) {
        continue;
      }
      auto* load_inst = pin->get_instance();
      if (load_inst == nullptr) {
        continue;
      }
      if (!load_names.emplace(load_inst->get_name()).second) {
        continue;
      }
      auto* load_node = gen_node(load_inst->get_name());
      driver_node->children.emplace_back(load_node);
      load_node->parent = driver_node;
    }
  }

  std::vector<TreeNode*> roots;
  for (const auto& [_, node] : name_to_node) {
    if (node->parent == nullptr) {
      node->depth = 0;
      roots.emplace_back(node);
    }
  }

  std::function<void(TreeNode*)> set_depth = [&](TreeNode* node) {
    if (node->children.empty()) {
      return;
    }
    for (auto* child : node->children) {
      if (child->depth == -1) {
        child->depth = node->depth + 1;
        set_depth(child);
      }
    }
  };
  for (auto* root : roots) {
    set_depth(root);
  }

  for (auto* root : roots) {
    int min_depth = std::numeric_limits<int>::max();
    int max_depth = 0;
    std::unordered_set<TreeNode*> visited;

    std::function<void(TreeNode*)> find_depth = [&](TreeNode* node) {
      if (node->children.empty()) {
        min_depth = std::min(min_depth, node->depth);
        max_depth = std::max(max_depth, node->depth);
        return;
      }

      for (auto* child : node->children) {
        if (visited.count(child) > 0) {
          continue;
        }
        visited.insert(child);
        find_depth(child);
      }
    };

    find_depth(root);
    metrics.path_infos.push_back(PathInfo{root->name, max_depth == 0 ? 0 : min_depth, max_depth});
  }
}

}  // namespace icts
