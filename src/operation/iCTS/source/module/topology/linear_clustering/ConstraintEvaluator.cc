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
 * @file ConstraintEvaluator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Cluster legality evaluation for linear clustering.
 */

#include "ConstraintEvaluator.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Geometry.hh"
#include "Inst.hh"
#include "LinearClusteringTypes.hh"
#include "Logger.hh"
#include "Pin.hh"
#include "PinLocationHelper.hh"
#include "Point.hh"
#include "RCTree.hh"
#include "Router.hh"
#include "SteinerTree.hh"
#include "TimingEngine.hh"
#include "TopologyConfig.hh"
#include "adapter/sta/STAAdapter.hh"
#include "bound_skew_tree/BSTTypes.hh"
#include "config/Config.hh"
#include "io/Wrapper.hh"
#include "local_legalization/LocalLegalization.hh"

namespace icts {
namespace {

constexpr double kMilliOhmPerOhm = 1000.0;

auto ResolveSyntheticRoot(const ClusterSpanMetrics& span_metrics, const LinearClusteringConfig& config) -> Point<int>
{
  return config.root_policy == LinearRootPolicy::kCenter ? span_metrics.center_root : span_metrics.median_root;
}

auto BuildRcOptions(const LinearClusteringConfig& config) -> Router::RCTreeBuildOptions
{
  Router::RCTreeBuildOptions options;
  if (config.routing_layer > 0) {
    options.routing_layer = config.routing_layer;
  } else {
    const auto& routing_layers = CONFIG_INST.get_routing_layers();
    if (!routing_layers.empty()) {
      options.routing_layer = static_cast<int>(routing_layers.front());
    }
  }

  if (config.wire_width > 0.0) {
    options.wire_width = config.wire_width;
  } else if (CONFIG_INST.get_wire_width() > 0.0) {
    options.wire_width = CONFIG_INST.get_wire_width();
  }

  return options;
}

auto BuildBstParameters(const LinearClusteringConfig& config, const Point<int>& routing_root) -> BSTParameters
{
  BSTParameters parameters;
  parameters.db_unit = std::max<int>(WRAPPER_INST.queryDbUnit(), 1);
  parameters.skew_bound = 0.0;
  parameters.pattern = RCPattern::kHV;
  parameters.topo_type = TopoType::kGreedyDist;
  parameters.root_guide = routing_root;

  auto routing_layer = config.routing_layer;
  if (routing_layer <= 0) {
    const auto& routing_layers = CONFIG_INST.get_routing_layers();
    routing_layer = routing_layers.empty() ? 1 : static_cast<int>(routing_layers.front());
  }

  std::optional<double> wire_width = std::nullopt;
  if (config.wire_width > 0.0) {
    wire_width = config.wire_width;
  } else if (CONFIG_INST.get_wire_width() > 0.0) {
    wire_width = CONFIG_INST.get_wire_width();
  }

  parameters.unit_h_cap = STA_ADAPTER_INST.queryWireCapacitance(routing_layer, 1.0, wire_width);
  parameters.unit_v_cap = parameters.unit_h_cap;
  parameters.unit_h_res = STA_ADAPTER_INST.queryWireResistance(routing_layer, 1.0, wire_width) / kMilliOhmPerOhm;
  parameters.unit_v_res = parameters.unit_h_res;
  return parameters;
}

auto IsPointOverlappingAnyLoad(const Point<int>& point, const std::vector<Point<int>>& load_locations) -> bool
{
  return std::ranges::any_of(load_locations, [&](const auto& location) -> bool { return location == point; });
}

auto LegalizeRoutingRoot(const Point<int>& raw_synthetic_root, const std::vector<Point<int>>& load_locations, Point<int>& legalized_root)
    -> bool
{
  legalized_root = raw_synthetic_root;
  if (!IsPointOverlappingAnyLoad(raw_synthetic_root, load_locations)) {
    return true;
  }

  std::vector<Point<int>> movable_points{raw_synthetic_root};
  LocalLegalization::Options options;
  options.failure_policy = LocalLegalization::FailurePolicy::kKeepOriginal;

  const auto result = LocalLegalization::legalize(movable_points, load_locations, LocalLegalization::RegionType{},
                                                  LocalLegalization::RegionType{}, options);

  if (result.legalized_points.empty()) {
    CTS_LOG_WARNING << "Linear clustering exact-cap root legalization failed: legalization returned empty points, synthetic root "
                    << raw_synthetic_root << ".";
    return false;
  }

  legalized_root = result.legalized_points.front();
  if (IsPointOverlappingAnyLoad(legalized_root, load_locations)) {
    CTS_LOG_WARNING << "Linear clustering exact-cap root legalization failed: legalized root still overlaps load location, synthetic root "
                    << raw_synthetic_root << ", legalized root " << legalized_root << ".";
    return false;
  }

  return true;
}

auto CalcTreeWirelength(const Router::ClockSteinerTreeType& tree) -> double
{
  double total_wirelength = 0.0;
  for (const auto& edge : tree.get_edges()) {
    total_wirelength += static_cast<double>(std::max(edge.distance, edge.routed_distance));
  }
  return total_wirelength;
}

auto UpdateEstimateFromRcTree(ElectricalEstimate& estimate, RCTree& rc_tree) -> bool
{
  if (!rc_tree.validate()) {
    CTS_LOG_WARNING << "Linear clustering electrical estimate got invalid RCTree.";
    return false;
  }
  const auto metrics = TimingEngine::update(rc_tree);
  estimate.total_cap = std::max(metrics.total_cap, estimate.pin_cap);
  estimate.wire_cap = std::max(0.0, estimate.total_cap - estimate.pin_cap);
  estimate.route_success = true;
  return true;
}

}  // namespace

auto ConstraintEvaluator::evaluate(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment,
                                   const LinearClusteringConfig& config, bool need_exact_cap) -> ConstraintEvaluation
{
  ConstraintEvaluation evaluation;
  evaluation.legal = false;
  evaluation.violation = ConstraintViolation::kEmptyCluster;

  if (segment.isEmpty() || segment.end > ordered_loads.size()) {
    return evaluation;
  }

  evaluation.metrics.fanout = segment.size();
  if (evaluation.metrics.fanout == 0) {
    return evaluation;
  }

  const auto span_metrics = calcSpan(ordered_loads, segment);
  evaluation.metrics.diameter = span_metrics.diameter;
  evaluation.metrics.electrical.synthetic_root = ResolveSyntheticRoot(span_metrics, config);
  evaluation.metrics.electrical.legalized_root = evaluation.metrics.electrical.synthetic_root;
  evaluation.metrics.electrical.routed_root = evaluation.metrics.electrical.synthetic_root;

  const auto has_cap_limit = IsFiniteCapLimit(config.max_cap);
  const auto need_exact_eval = need_exact_cap && (has_cap_limit || config.always_build_exact_cap);
  std::vector<Pin*> cluster_pins;

  if (has_cap_limit || need_exact_eval) {
    cluster_pins = collectPins(ordered_loads, segment);
    evaluation.metrics.cap_lower_bound = estimatePinCap(cluster_pins);
    evaluation.metrics.total_cap = evaluation.metrics.cap_lower_bound;
    evaluation.metrics.electrical.pin_cap = evaluation.metrics.cap_lower_bound;
    evaluation.metrics.electrical.total_cap = evaluation.metrics.cap_lower_bound;
  } else {
    evaluation.metrics.cap_lower_bound = 0.0;
    evaluation.metrics.total_cap = 0.0;
    evaluation.metrics.electrical.pin_cap = 0.0;
    evaluation.metrics.electrical.total_cap = 0.0;
  }

  if (config.max_fanout > 0 && evaluation.metrics.fanout > config.max_fanout) {
    evaluation.violation = ConstraintViolation::kFanout;
    return evaluation;
  }

  if (config.max_diameter > 0 && evaluation.metrics.diameter > config.max_diameter) {
    evaluation.violation = ConstraintViolation::kDiameter;
    return evaluation;
  }

  if (has_cap_limit && evaluation.metrics.cap_lower_bound > config.max_cap) {
    evaluation.violation = ConstraintViolation::kCapacitance;
    return evaluation;
  }

  if (need_exact_eval) {
    auto exact_electrical = estimateExactCap(cluster_pins, evaluation.metrics.electrical.synthetic_root, config);
    evaluation.metrics.electrical = exact_electrical;
    evaluation.metrics.total_cap = exact_electrical.total_cap;
    evaluation.metrics.wirelength = exact_electrical.wirelength;
    if (!exact_electrical.route_success) {
      evaluation.violation = ConstraintViolation::kRoutingFailed;
      return evaluation;
    }
    if (has_cap_limit && exact_electrical.total_cap > config.max_cap) {
      evaluation.violation = ConstraintViolation::kCapacitance;
      return evaluation;
    }
  }

  evaluation.violation = ConstraintViolation::kNone;
  evaluation.legal = true;
  return evaluation;
}

auto ConstraintEvaluator::calcSpan(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment) -> ClusterSpanMetrics
{
  ClusterSpanMetrics metrics;
  if (segment.isEmpty() || segment.end > ordered_loads.size()) {
    return metrics;
  }

  bool initialized = false;
  std::vector<Point<int>> points;
  points.reserve(segment.size());
  for (std::size_t i = segment.begin; i < segment.end; ++i) {
    const auto& location = ordered_loads.at(i).location;
    points.push_back(location);
    if (!initialized) {
      metrics.min_x = location.get_x();
      metrics.min_y = location.get_y();
      metrics.max_x = location.get_x();
      metrics.max_y = location.get_y();
      initialized = true;
      continue;
    }
    metrics.min_x = std::min(metrics.min_x, location.get_x());
    metrics.min_y = std::min(metrics.min_y, location.get_y());
    metrics.max_x = std::max(metrics.max_x, location.get_x());
    metrics.max_y = std::max(metrics.max_y, location.get_y());
  }

  if (!initialized) {
    return metrics;
  }

  metrics.diameter = (metrics.max_x - metrics.min_x) + (metrics.max_y - metrics.min_y);
  metrics.median_root = geometry::CalcMedian(points, [](const Point<int>& point) -> auto { return point; });
  const auto center = geometry::CalcCenter(points, [](const Point<int>& point) -> auto { return point; });
  metrics.center_root = Point<int>(static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y())));
  return metrics;
}

auto ConstraintEvaluator::collectPins(const std::vector<OrderedLoad>& ordered_loads, const SegmentRange& segment) -> std::vector<Pin*>
{
  std::vector<Pin*> pins;
  if (segment.isEmpty() || segment.end > ordered_loads.size()) {
    return pins;
  }

  pins.reserve(segment.size());
  for (std::size_t i = segment.begin; i < segment.end; ++i) {
    if (ordered_loads.at(i).pin != nullptr) {
      pins.push_back(ordered_loads.at(i).pin);
    }
  }
  return pins;
}

auto ConstraintEvaluator::estimatePinCap(const std::vector<Pin*>& loads) -> double
{
  double total_pin_cap = 0.0;
  for (const auto* pin : loads) {
    if (pin == nullptr) {
      continue;
    }
    total_pin_cap += queryPinCap(pin);
  }
  return total_pin_cap;
}

auto ConstraintEvaluator::estimateExactCap(const std::vector<Pin*>& loads, const Point<int>& synthetic_root,
                                           const LinearClusteringConfig& config) -> ElectricalEstimate
{
  ElectricalEstimate estimate;
  estimate.exact = true;
  estimate.synthetic_root = synthetic_root;
  estimate.legalized_root = synthetic_root;
  estimate.routed_root = synthetic_root;

  std::vector<Pin*> active_loads;
  active_loads.reserve(loads.size());
  for (auto* pin : loads) {
    if (pin != nullptr) {
      active_loads.push_back(pin);
    }
  }

  estimate.pin_cap = estimatePinCap(active_loads);
  estimate.total_cap = estimate.pin_cap;

  if (active_loads.empty()) {
    estimate.route_success = true;
    return estimate;
  }

  const auto load_locations = collectPinLocations(active_loads);
  if (!LegalizeRoutingRoot(estimate.synthetic_root, load_locations, estimate.legalized_root)) {
    estimate.route_success = false;
    estimate.routed_root = estimate.legalized_root;
    return estimate;
  }

  estimate.routed_root = estimate.legalized_root;
  const auto rc_options = BuildRcOptions(config);
  std::vector<Router::ClockTerminal> clock_terminals;
  clock_terminals.reserve(active_loads.size());
  for (std::size_t i = 0; i < active_loads.size(); ++i) {
    auto* pin = active_loads.at(i);
    if (pin == nullptr) {
      continue;
    }
    Router::ClockTerminal terminal;
    terminal.name = std::string("sink_") + std::to_string(i);
    terminal.location = pin->get_location();
    terminal.pin_cap = queryPinCap(pin);
    terminal.insertion_delay = 0.0;
    clock_terminals.push_back(std::move(terminal));
  }

  Router::ClockSteinerTreeType clock_tree;
  if (!clock_terminals.empty()) {
    Router::ClockTerminal driver_terminal;
    driver_terminal.name = "legalized_root";
    driver_terminal.location = estimate.legalized_root;
    driver_terminal.pin_cap = 0.0;
    driver_terminal.insertion_delay = 0.0;

    if (config.router_kind == LinearRouterKind::kFlute) {
      clock_tree = Router::buildFluteTree(driver_terminal, clock_terminals);
    } else if (config.router_kind == LinearRouterKind::kSalt) {
      clock_tree = Router::buildSaltTree(driver_terminal, clock_terminals);
    } else {
      const auto bst_parameters = BuildBstParameters(config, estimate.legalized_root);
      clock_tree = (config.router_kind == LinearRouterKind::kBst) ? Router::buildBstTree(clock_terminals, bst_parameters)
                                                                  : Router::buildCbsTree(clock_terminals, bst_parameters);
    }
  }

  if (!clock_terminals.empty() && (clock_tree.node_count() == 0 || !clock_tree.validate())) {
    CTS_LOG_WARNING << "Linear clustering clock-aware routing returned an empty or invalid tree.";
    return estimate;
  }

  const auto* routed_root = clock_tree.get_node(clock_tree.get_root());
  if (routed_root != nullptr) {
    estimate.routed_root = routed_root->location;
  }
  estimate.wirelength = CalcTreeWirelength(clock_tree);
  auto rc_tree = Router::buildRCTree(clock_tree, rc_options);
  UpdateEstimateFromRcTree(estimate, rc_tree);
  return estimate;
}

auto ConstraintEvaluator::queryPinCap(const Pin* pin) -> double
{
  if (pin == nullptr) {
    return 0.0;
  }

  auto iter = _pin_cap_cache.find(pin);
  if (iter != _pin_cap_cache.end()) {
    return iter->second;
  }

  if (pin->get_inst() == nullptr || pin->get_net() == nullptr || pin->get_name().empty()) {
    _pin_cap_cache[pin] = 0.0;
    return 0.0;
  }
  if (pin->get_inst()->get_name().empty()) {
    _pin_cap_cache[pin] = 0.0;
    return 0.0;
  }

  if (WRAPPER_INST.get_idb() == nullptr || WRAPPER_INST.get_idb_design() == nullptr) {
    CTS_LOG_WARNING << "Linear clustering pin-cap query degraded: iDB/STA context is not ready, use 0.0 cap for pin " << pin->get_name()
                    << ".";
    _pin_cap_cache[pin] = 0.0;
    return 0.0;
  }

  const auto pin_cap = std::max(0.0, STA_ADAPTER_INST.queryPinCapacitance(pin));
  _pin_cap_cache[pin] = pin_cap;
  return pin_cap;
}

}  // namespace icts
