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
 * @file BoundSkewTreeFlow.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Bound-skew tree bottom-up/top-down orchestration.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <vector>

#include "BSTTypes.hh"
#include "BoundSkewTree.hh"
#include "Components.hh"
#include "GeomCalc.hh"
#include "Log.hh"

namespace icts::bst {
namespace {

enum class TraversalStage : std::uint8_t
{
  kEnter,
  kProcessRight,
  kFinalize,
};

struct TraversalFrame
{
  Area* area = nullptr;
  TraversalStage stage = TraversalStage::kEnter;
};

}  // namespace

auto BoundSkewTree::bottomUp() -> void
{
  switch (_topo_type) {
    case TopoType::kBiCluster:
    case TopoType::kBiPartition:
    case TopoType::kInputTopo:
      bottomUpTopoBased();
      break;
    case TopoType::kGreedyDist:
    case TopoType::kGreedyMerge:
      bottomUpAllPairBased();
      break;
    default:
      LOG_FATAL << "topo type is not supported";
      break;
  }
}
auto BoundSkewTree::bottomUpAllPairBased() -> void
{
  // none input topo
  while (_unmerged_nodes.size() > 1) {
    // switch cost_func by topo_type
    CostFunc cost_func;
    switch (_topo_type) {
      case TopoType::kGreedyDist:
        cost_func = [&](Area* left, Area* right) -> double { return distanceCost(left, right); };
        break;
      case TopoType::kGreedyMerge:
        cost_func = [&](Area* left, Area* right) -> double { return mergeCost(left, right); };
        break;
      default:
        LOG_FATAL << "topo type is not supported";
        break;
    }
    auto best_match = getBestMatch(cost_func);
    auto* left = best_match.left;
    auto* right = best_match.right;
    auto* parent = makeArea(++_id);
    // random select RCpattern
    parent->set_pattern(_pattern);
    merge(parent, left, right);
    // erase left and right
    auto [erase_begin, erase_end]
        = std::ranges::remove_if(_unmerged_nodes, [&](Area* node) -> bool { return node == left || node == right; });
    _unmerged_nodes.erase(erase_begin, erase_end);
    _unmerged_nodes.push_back(parent);
  }
  _root = _unmerged_nodes.front();
}
auto BoundSkewTree::bottomUpTopoBased() -> void
{
  switch (_topo_type) {
    case TopoType::kBiCluster:
      biCluster();
      break;
    case TopoType::kBiPartition:
      biPartition();
      break;
    case TopoType::kInputTopo:
      break;
    default:
      LOG_FATAL << "topo type is not supported";
      break;
  }
  processBottomUpTopology();
}
auto BoundSkewTree::processBottomUpTopology() -> void
{
  if (_root == nullptr) {
    return;
  }

  std::vector<TraversalFrame> stack{{.area = _root, .stage = TraversalStage::kEnter}};
  while (!stack.empty()) {
    auto frame = stack.back();
    stack.pop_back();

    auto* current = frame.area;
    auto* left = current->get_left();
    auto* right = current->get_right();
    if (left == nullptr || right == nullptr) {
      continue;
    }

    if (frame.stage == TraversalStage::kEnter) {
      stack.push_back(TraversalFrame{.area = current, .stage = TraversalStage::kFinalize});
      stack.push_back(TraversalFrame{.area = right, .stage = TraversalStage::kEnter});
      stack.push_back(TraversalFrame{.area = left, .stage = TraversalStage::kEnter});
      continue;
    }

    merge(current, left, right);
  }
}
auto BoundSkewTree::topDown() -> void
{
  Point root_location;
  auto merge_region = _root->get_merge_region();
  if (_root_guide.has_value()) {
    root_location = Geom::closestPointOnRegion(_root_guide.value(), merge_region);
  } else {
    root_location = Geom::centerPoint(merge_region);
  }
  _root->set_location(root_location);
  embedTree();
}
auto BoundSkewTree::embedTree() const -> void
{
  if (_root == nullptr) {
    return;
  }

  std::vector<TraversalFrame> stack{{.area = _root, .stage = TraversalStage::kEnter}};
  while (!stack.empty()) {
    auto frame = stack.back();
    stack.pop_back();

    auto* current = frame.area;
    auto* left = current->get_left();
    auto* right = current->get_right();
    if (left == nullptr || right == nullptr) {
      continue;
    }

    switch (frame.stage) {
      case TraversalStage::kEnter:
        embedChild(EmbeddingStep{.parent = current, .child = left, .side = kLeft});
        stack.push_back(TraversalFrame{.area = current, .stage = TraversalStage::kProcessRight});
        stack.push_back(TraversalFrame{.area = left, .stage = TraversalStage::kEnter});
        break;
      case TraversalStage::kProcessRight:
        embedChild(EmbeddingStep{.parent = current, .child = right, .side = kRight});
        stack.push_back(TraversalFrame{.area = current, .stage = TraversalStage::kFinalize});
        stack.push_back(TraversalFrame{.area = right, .stage = TraversalStage::kEnter});
        break;
      case TraversalStage::kFinalize:
        updateEmbeddedNodeTiming(current);
        break;
    }
  }
}
auto BoundSkewTree::updateEmbeddedNodeTiming(Area* current) const -> void
{
  auto* left = current->get_left();
  auto* right = current->get_right();
  LOG_FATAL_IF(left == nullptr || right == nullptr) << "Embedded node children are null";

  auto parent_point = current->get_location();
  const auto left_point = left->get_location();
  const auto right_point = right->get_location();
  parent_point.min = std::numeric_limits<double>::max();
  parent_point.max = std::numeric_limits<double>::lowest();
  const auto delay_to_left = pointDelayIncrease(parent_point, left_point, current->get_edge_len(kLeft), left->get_cap_load(), _pattern);
  const auto delay_to_right = pointDelayIncrease(parent_point, right_point, current->get_edge_len(kRight), right->get_cap_load(), _pattern);
  parent_point.min = std::min(left_point.min + delay_to_left, right_point.min + delay_to_right);
  parent_point.max = std::max(left_point.max + delay_to_left, right_point.max + delay_to_right);
  LOG_FATAL_IF(pointSkew(parent_point) > _skew_bound + (100 * kEpsilon))
      << "skew is so larger than skew bound, skew: " << pointSkew(parent_point);
  if (pointSkew(parent_point) > _skew_bound + kEpsilon) {
    LOG_WARNING << current->get_name() << " max delay: " << parent_point.max << " min delay: " << parent_point.min;
    LOG_WARNING << "skew is larger than skew bound with error: " << pointSkew(parent_point) - _skew_bound;
    parent_point.min = parent_point.max - _skew_bound + kEpsilon;
  }
  current->set_location(parent_point);
}
auto BoundSkewTree::merge(Area* parent, Area* left, Area* right) -> void
{
  parent->set_left(left);
  parent->set_right(right);
  left->set_parent(parent);
  right->set_parent(parent);
  calcJoiningSegment(MergeAreas{.parent = parent, .left = left, .right = right});
  parent->set_edge_len(kLeft, -1);
  parent->set_edge_len(kRight, -1);
  auto dist = parent->get_radius();
  processJoiningSegment(parent);
  auto left_line = parent->get_line(kLeft);
  auto right_line = parent->get_line(kRight);
  constructMergeRegion(MergeAreas{.parent = parent, .left = left, .right = right});
  if (Geom::lineType(getJoiningSegmentLine(kLeft)) == LineType::kManhattan) {
    LOG_FATAL_IF(Geom::lineType(getJoiningSegmentLine(kRight)) != LineType::kManhattan) << "right joining_segment is not manhattan";
    if (Geom::isSegmentTransformedRect(mergeSegment(kLeft))) {
      auto& left_merge_segment = mergeSegment(kLeft);
      Geom::transformedRectToLine(left_merge_segment, left_line);
    }
    if (Geom::isSegmentTransformedRect(mergeSegment(kRight))) {
      auto& right_merge_segment = mergeSegment(kRight);
      Geom::transformedRectToLine(right_merge_segment, right_line);
    }
  }
  parent->set_line(kLeft, left_line);
  parent->set_line(kRight, right_line);

  parent->set_radius(dist);
  if (parent->get_edge_len(kLeft) + parent->get_edge_len(kRight) < 0) {
    parent->set_cap_load(left->get_cap_load() + right->get_cap_load() + (parent->get_radius() * _unit_horizontal_capacitance));
  } else {
    parent->set_cap_load(left->get_cap_load() + right->get_cap_load()
                         + ((parent->get_edge_len(kLeft) + parent->get_edge_len(kRight)) * _unit_horizontal_capacitance));
  }
}

}  // namespace icts::bst
