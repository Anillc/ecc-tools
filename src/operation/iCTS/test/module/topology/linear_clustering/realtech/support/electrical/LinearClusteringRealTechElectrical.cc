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
 * @file LinearClusteringRealTechElectrical.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Electrical helpers for real-tech linear clustering tests.
 */

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Inst.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/ConstraintEvaluator.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"

namespace icts_test::linear_clustering::realtech::detail {

namespace {

auto FindDirectionalMedianNeighbor(const std::vector<icts::Pin*>& loads, const icts::Pin* candidate,
                                   const icts::Point<int>& candidate_location, bool lower_side) -> icts::Pin*
{
  for (auto* other : loads) {
    if (other == nullptr || other == candidate) {
      continue;
    }

    const auto& location = other->get_location();
    const bool matches_side = lower_side
                                  ? (location.get_x() <= candidate_location.get_x() && location.get_y() <= candidate_location.get_y())
                                  : (location.get_x() >= candidate_location.get_x() && location.get_y() >= candidate_location.get_y());
    if (matches_side) {
      return other;
    }
  }
  return nullptr;
}

auto BuildMedianCollisionSubset(const std::vector<icts::Pin*>& loads, icts::Pin* candidate) -> std::vector<icts::Pin*>
{
  if (candidate == nullptr) {
    return {};
  }

  const auto& candidate_location = candidate->get_location();
  auto* lower = FindDirectionalMedianNeighbor(loads, candidate, candidate_location, true);
  if (lower == nullptr) {
    return {};
  }

  auto* upper = FindDirectionalMedianNeighbor(loads, candidate, candidate_location, false);
  if (upper == nullptr || upper == lower) {
    return {};
  }
  return {lower, candidate, upper};
}

auto IsMedianCollisionSubset(const std::vector<icts::Pin*>& subset, const icts::Point<int>& candidate_location) -> bool
{
  return !subset.empty() && CalcClusterDiameter(subset) > 0 && IsSamePoint(CalcClusterMedian(subset), candidate_location);
}

}  // namespace

auto FindNonDegenerateMedianCollisionSubset(const std::vector<icts::Pin*>& loads) -> std::vector<icts::Pin*>
{
  if (loads.size() < 3U) {
    return {};
  }

  for (auto* candidate : loads) {
    if (candidate == nullptr) {
      continue;
    }
    const auto& candidate_location = candidate->get_location();
    auto subset = BuildMedianCollisionSubset(loads, candidate);
    if (IsMedianCollisionSubset(subset, candidate_location)) {
      return subset;
    }
  }

  return {};
}

auto CountPinsWithExactCapContext(const std::vector<icts::Pin*>& loads) -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(loads, [](const icts::Pin* pin) -> bool {
    return pin != nullptr && pin->get_inst() != nullptr && pin->get_net() != nullptr && !pin->get_name().empty()
           && !pin->get_inst()->get_name().empty();
  }));
}

auto EstimateExactElectrical(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config) -> icts::ElectricalEstimate
{
  std::vector<icts::OrderedLoad> ordered_loads;
  ordered_loads.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    ordered_loads.push_back(icts::OrderedLoad{.pin = pin, .location = pin->get_location(), .original_index = index});
  }

  icts::LinearClusteringConfig exact_config = config;
  exact_config.max_fanout = 0;
  exact_config.max_diameter = 0;
  exact_config.max_cap = std::numeric_limits<double>::infinity();
  exact_config.always_build_exact_cap = true;

  icts::ConstraintEvaluator evaluator;
  const auto evaluation
      = evaluator.evaluate(ordered_loads, icts::SegmentRange{.begin = 0U, .end = ordered_loads.size()}, exact_config, true);
  return evaluation.metrics.electrical;
}

auto ResolveEffectiveRoutingLayer(const icts::LinearClusteringConfig& config) -> int
{
  if (config.routing_layer > 0) {
    return config.routing_layer;
  }

  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (routing_layers.empty()) {
    return 1;
  }
  return static_cast<int>(routing_layers.front());
}

auto ResolveEffectiveWireWidth(const icts::LinearClusteringConfig& config) -> std::optional<double>
{
  if (config.wire_width > 0.0) {
    return config.wire_width;
  }

  const auto configured_wire_width = CONFIG_INST.get_wire_width();
  return configured_wire_width > 0.0 ? std::optional<double>{configured_wire_width} : std::nullopt;
}

auto QueryEffectiveWireRcPerUm(const icts::LinearClusteringConfig& config) -> WireRcPerUm
{
  const auto routing_layer = ResolveEffectiveRoutingLayer(config);
  const auto wire_width_um = ResolveEffectiveWireWidth(config);
  const auto resistance_per_um_ohm = STA_ADAPTER_INST.queryWireResistance(routing_layer, 1.0, wire_width_um) / kMilliOhmPerOhm;
  const auto capacitance_per_um = STA_ADAPTER_INST.queryWireCapacitance(routing_layer, 1.0, wire_width_um);
  return {
      .routing_layer = routing_layer,
      .wire_width_um = wire_width_um,
      .resistance_per_um_ohm = resistance_per_um_ohm,
      .capacitance_per_um = capacitance_per_um,
  };
}

}  // namespace icts_test::linear_clustering::realtech::detail
