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
 * @file RootDriverCompensation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-04
 * @brief H-tree root driver compensation pass implementation.
 */

#include "synthesis/htree/compensation/RootDriverCompensation.hh"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "Log.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "STAAdapter.hh"
#include "Tree.hh"
#include "config/Config.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "routing/router/Router.hh"
#include "synthesis/htree/segment_pruning/SegmentLibrary.hh"

namespace icts::htree {
namespace {

constexpr const char* kRootDriverCompensationMethod = "direct";
constexpr const char* kRootDriverCompensationLoadSource = "root_closure_physical_estimate";
constexpr const char* kRootClosureFluteEstimator = "flute_clock_steiner_tree";
constexpr const char* kRootClosureSingleLoadEstimator = "single_load_manhattan";
constexpr const char* kRootClosureHpwlFallbackEstimator = "hpwl_bbox_fallback";

struct RootClosureTerminal
{
  std::string name;
  Point<int> location;
  double pin_cap_pf = 0.0;
};

struct RootClosureWireEstimate
{
  std::string route_estimator;
  double wire_cap_pf = 0.0;
  double routed_wirelength_um = 0.0;
};

struct RootDriverCompensationCacheKey
{
  std::string cell_master;
  double input_slew_ns = 0.0;
  unsigned load_bucket_idx = 0U;
  double load_cap_pf = 0.0;
  double clock_period_ns = 0.0;

  auto operator==(const RootDriverCompensationCacheKey& rhs) const -> bool = default;
};

struct RootDriverCompensationCacheKeyHash
{
  auto operator()(const RootDriverCompensationCacheKey& key) const noexcept -> std::size_t;
};

struct RootClosureLoadEstimate
{
  bool valid = false;
  std::string source;
  std::string route_estimator;
  unsigned bucket_idx = 0U;
  double total_load_cap_pf = 0.0;
  double terminal_pin_cap_pf = 0.0;
  double wire_cap_pf = 0.0;
  double routed_wirelength_um = 0.0;
  std::size_t terminal_count = 0U;
};

struct RootDriverCompensationState
{
  RootDriverCompensationOptions options;
  std::unordered_map<RootDriverCompensationCacheKey, HTreeRootDriverCompensation, RootDriverCompensationCacheKeyHash> cost_by_key;
  std::unordered_map<PatternId, RootClosureLoadEstimate> root_load_by_pattern;
  RootDriverCompensationStats stats;
};

auto HashCombine(std::size_t seed, std::size_t value) -> std::size_t
{
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

auto ResolveRoutingLayer() -> int
{
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (routing_layers.empty()) {
    return 1;
  }
  return static_cast<int>(routing_layers.front());
}

auto ResolveWireWidth() -> std::optional<double>
{
  const double wire_width_um = CONFIG_INST.get_wire_width();
  return wire_width_um > 0.0 ? std::optional<double>(wire_width_um) : std::nullopt;
}

auto DbuToUm(int64_t dbu) -> double
{
  const auto dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  return static_cast<double>(std::max<int64_t>(dbu, 0)) / static_cast<double>(dbu_per_um);
}

auto QueryWireCapForDbuLength(int64_t length_dbu) -> double
{
  const double length_um = DbuToUm(length_dbu);
  if (length_um <= 0.0) {
    return 0.0;
  }

  const double wire_cap_pf = STA_ADAPTER_INST.queryWireCapacitance(ResolveRoutingLayer(), length_um, ResolveWireWidth());
  if (!std::isfinite(wire_cap_pf) || wire_cap_pf < 0.0) {
    LOG_WARNING << "HTree: root-closure wire-cap query returned an invalid value for length " << length_um << " um.";
    return 0.0;
  }
  return wire_cap_pf;
}

auto CalcManhattanDbu(const Point<int>& lhs, const Point<int>& rhs) -> int64_t
{
  return static_cast<int64_t>(std::abs(lhs.get_x() - rhs.get_x())) + static_cast<int64_t>(std::abs(lhs.get_y() - rhs.get_y()));
}

auto InterpolateRootClosurePoint(const Point<int>& source, const Point<int>& sink, double normalized_position) -> Point<int>
{
  const double clamped_position = std::clamp(normalized_position, 0.0, 1.0);
  const int dx = sink.get_x() - source.get_x();
  const int dy = sink.get_y() - source.get_y();
  const int total_distance = std::abs(dx) + std::abs(dy);
  if (total_distance == 0) {
    return source;
  }

  const int target_distance = static_cast<int>(std::lround(clamped_position * static_cast<double>(total_distance)));
  const int x_step = std::min(std::abs(dx), target_distance);
  const int y_step = std::max(0, target_distance - x_step);
  const int x = source.get_x() + ((dx >= 0) ? x_step : -x_step);
  const int y = source.get_y() + ((dy >= 0) ? y_step : -y_step);
  return Point<int>(x, y);
}

auto EstimateHpwlFallbackWire(const Point<int>& root_location, const std::vector<RootClosureTerminal>& terminals,
                              RootDriverCompensationStats& stats) -> RootClosureWireEstimate
{
  int min_x = root_location.get_x();
  int max_x = root_location.get_x();
  int min_y = root_location.get_y();
  int max_y = root_location.get_y();
  for (const auto& terminal : terminals) {
    min_x = std::min(min_x, terminal.location.get_x());
    max_x = std::max(max_x, terminal.location.get_x());
    min_y = std::min(min_y, terminal.location.get_y());
    max_y = std::max(max_y, terminal.location.get_y());
  }

  const auto hpwl_dbu = static_cast<int64_t>(max_x - min_x) + static_cast<int64_t>(max_y - min_y);
  ++stats.fallback_route_estimate_count;
  return RootClosureWireEstimate{
      .route_estimator = kRootClosureHpwlFallbackEstimator,
      .wire_cap_pf = QueryWireCapForDbuLength(hpwl_dbu),
      .routed_wirelength_um = DbuToUm(hpwl_dbu),
  };
}

auto EstimateRootClosureWire(const Point<int>& root_location, const std::vector<RootClosureTerminal>& terminals,
                             RootDriverCompensationStats& stats) -> RootClosureWireEstimate
{
  if (terminals.empty()) {
    return RootClosureWireEstimate{.route_estimator = "none"};
  }

  if (terminals.size() == 1U) {
    const auto length_dbu = CalcManhattanDbu(root_location, terminals.front().location);
    ++stats.fallback_route_estimate_count;
    return RootClosureWireEstimate{
        .route_estimator = kRootClosureSingleLoadEstimator,
        .wire_cap_pf = QueryWireCapForDbuLength(length_dbu),
        .routed_wirelength_um = DbuToUm(length_dbu),
    };
  }

  Router::ClockTerminal driver_terminal;
  driver_terminal.name = "root_driver_output";
  driver_terminal.location = root_location;

  std::vector<Router::ClockTerminal> load_terminals;
  load_terminals.reserve(terminals.size());
  for (std::size_t terminal_index = 0; terminal_index < terminals.size(); ++terminal_index) {
    const auto& terminal = terminals.at(terminal_index);
    Router::ClockTerminal load_terminal;
    load_terminal.name = terminal.name.empty() ? "root_closure_terminal_" + std::to_string(terminal_index) : terminal.name;
    load_terminal.location = terminal.location;
    load_terminal.pin_cap = terminal.pin_cap_pf;
    load_terminals.push_back(std::move(load_terminal));
  }

  auto route_tree = Router::buildFluteTree(driver_terminal, load_terminals);
  if (route_tree.validate() && route_tree.edge_count() > 0U) {
    double wire_cap_pf = 0.0;
    double routed_wirelength_um = 0.0;
    for (const auto& edge : route_tree.get_edges()) {
      const auto length_dbu = static_cast<int64_t>(std::max(edge.distance, edge.routed_distance));
      wire_cap_pf += QueryWireCapForDbuLength(length_dbu);
      routed_wirelength_um += DbuToUm(length_dbu);
    }
    ++stats.flute_route_estimate_count;
    return RootClosureWireEstimate{
        .route_estimator = kRootClosureFluteEstimator,
        .wire_cap_pf = wire_cap_pf,
        .routed_wirelength_um = routed_wirelength_um,
    };
  }

  LOG_WARNING << "HTree: root-closure FLUTE estimate was unavailable; using HPWL fallback.";
  return EstimateHpwlFallbackWire(root_location, terminals, stats);
}

auto MakeBufferRootClosureTerminal(const TreeNode& parent_node, const TreeNode& child_node, const BufferingPattern& segment_pattern,
                                   std::size_t terminal_index) -> RootClosureTerminal
{
  const auto& positions = segment_pattern.get_buffer_positions();
  const auto& cell_masters = segment_pattern.get_cell_masters();
  if (positions.empty() || cell_masters.empty()) {
    return {};
  }

  const std::string& first_buffer_master = cell_masters.front();
  return RootClosureTerminal{
      .name = "root_closure_buffer_input_" + std::to_string(terminal_index),
      .location = InterpolateRootClosurePoint(parent_node.get_position(), child_node.get_position(), positions.front()),
      .pin_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(first_buffer_master),
  };
}

auto CollectExternalLoadTerminals(const std::vector<std::size_t>& boundary_node_ids, const Tree& topology)
    -> std::vector<RootClosureTerminal>
{
  std::vector<RootClosureTerminal> terminals;
  std::unordered_set<const Pin*> seen_pins;
  for (const auto node_id : boundary_node_ids) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr) {
      continue;
    }
    for (auto* load : node->get_loads()) {
      if (load == nullptr || !seen_pins.insert(load).second) {
        continue;
      }
      terminals.push_back(RootClosureTerminal{
          .name = Design::getPinFullName(load),
          .location = load->get_location(),
          .pin_cap_pf = STA_ADAPTER_INST.queryPinCapacitance(load),
      });
    }
  }
  return terminals;
}

auto MakeRootClosureLoadEstimate(const Point<int>& root_location, const std::vector<RootClosureTerminal>& terminals,
                                 const UniformValueLattice& cap_lattice, RootDriverCompensationStats& stats) -> RootClosureLoadEstimate
{
  RootClosureLoadEstimate estimate;
  estimate.source = kRootDriverCompensationLoadSource;
  estimate.terminal_count = terminals.size();
  if (terminals.empty()) {
    estimate.route_estimator = "none";
    return estimate;
  }

  for (const auto& terminal : terminals) {
    estimate.terminal_pin_cap_pf += std::max(0.0, terminal.pin_cap_pf);
  }
  const auto wire_estimate = EstimateRootClosureWire(root_location, terminals, stats);
  estimate.route_estimator = wire_estimate.route_estimator;
  estimate.wire_cap_pf = wire_estimate.wire_cap_pf;
  estimate.routed_wirelength_um = wire_estimate.routed_wirelength_um;
  estimate.total_load_cap_pf = estimate.terminal_pin_cap_pf + estimate.wire_cap_pf;
  estimate.bucket_idx = cap_lattice.isValid() ? cap_lattice.coveringIndex(estimate.total_load_cap_pf) : 0U;
  estimate.valid = estimate.total_load_cap_pf > 0.0;
  return estimate;
}

auto ResolveRootClosureLoadEstimate(PatternId topology_pattern_id, const TopologyPatternLibrary& topology_library,
                                    const BufferPatternLibrary& segment_pattern_library, const Tree& topology,
                                    const UniformValueLattice& cap_lattice, RootDriverCompensationStats& stats) -> RootClosureLoadEstimate
{
  ++stats.load_resolution_count;
  const auto* root_node = topology.get_node(topology.get_root());
  if (root_node == nullptr) {
    LOG_WARNING << "HTree: root-driver compensation load resolution failed because topology root is missing.";
    ++stats.load_resolution_failure_count;
    return {};
  }

  const auto topology_pattern = topology_library.materialize(topology_pattern_id);
  const auto& level_segment_pattern_ids = topology_pattern.get_level_segment_pattern_ids();
  std::vector<std::size_t> active_node_ids{topology.get_root()};

  for (const auto segment_pattern_id : level_segment_pattern_ids) {
    const auto* segment_pattern = segment_pattern_library.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTree: candidate segment pattern metadata is missing during root-load resolution.";

    const bool segment_has_real_buffer = !segment_pattern->get_buffer_positions().empty() && !segment_pattern->get_cell_masters().empty();
    std::vector<RootClosureTerminal> buffer_input_terminals;
    std::vector<std::size_t> next_active_node_ids;

    for (const auto parent_id : active_node_ids) {
      const auto* parent_node = topology.get_node(parent_id);
      if (parent_node == nullptr) {
        continue;
      }
      for (const auto child_id : parent_node->get_children()) {
        if (child_id == std::numeric_limits<std::size_t>::max()) {
          continue;
        }
        const auto* child_node = topology.get_node(child_id);
        if (child_node == nullptr || child_node->get_loads().empty()) {
          continue;
        }
        if (segment_has_real_buffer) {
          buffer_input_terminals.push_back(
              MakeBufferRootClosureTerminal(*parent_node, *child_node, *segment_pattern, buffer_input_terminals.size()));
        } else {
          next_active_node_ids.push_back(child_id);
        }
      }
    }

    if (segment_has_real_buffer) {
      auto estimate = MakeRootClosureLoadEstimate(root_node->get_position(), buffer_input_terminals, cap_lattice, stats);
      if (!estimate.valid) {
        ++stats.load_resolution_failure_count;
      }
      return estimate;
    }

    active_node_ids = std::move(next_active_node_ids);
    if (active_node_ids.empty()) {
      break;
    }
  }

  auto estimate
      = MakeRootClosureLoadEstimate(root_node->get_position(), CollectExternalLoadTerminals(active_node_ids, topology), cap_lattice, stats);
  if (!estimate.valid) {
    ++stats.load_resolution_failure_count;
  }
  return estimate;
}

auto ResolveRootDriverCellMaster(PatternId topology_pattern_id, const TopologyPatternLibrary& topology_library,
                                 const BufferPatternLibrary& segment_pattern_library, const std::string& fallback_cell_master)
    -> std::string
{
  const auto topology_pattern = topology_library.materialize(topology_pattern_id);
  for (const auto segment_pattern_id : topology_pattern.get_level_segment_pattern_ids()) {
    const auto* segment_pattern = segment_pattern_library.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTree: candidate segment pattern metadata is missing.";
    const auto& cell_masters = segment_pattern->get_cell_masters();
    if (!cell_masters.empty()) {
      return cell_masters.back();
    }
  }
  return fallback_cell_master;
}

auto MakeRootDriverCompensation(const STAAdapter::RootDriverCost& cost, double input_slew_ns, const RootClosureLoadEstimate& load_estimate,
                                double clock_period_ns) -> HTreeRootDriverCompensation
{
  HTreeRootDriverCompensation compensation;
  compensation.enabled = true;
  compensation.valid = cost.valid;
  compensation.method = kRootDriverCompensationMethod;
  compensation.cell_master = cost.cell_master;
  compensation.load_source = load_estimate.source;
  compensation.route_estimator = load_estimate.route_estimator;
  compensation.input_slew_ns = input_slew_ns;
  compensation.load_bucket_idx = load_estimate.bucket_idx;
  compensation.load_cap_pf = load_estimate.total_load_cap_pf;
  compensation.terminal_pin_cap_pf = load_estimate.terminal_pin_cap_pf;
  compensation.wire_cap_pf = load_estimate.wire_cap_pf;
  compensation.routed_wirelength_um = load_estimate.routed_wirelength_um;
  compensation.terminal_count = load_estimate.terminal_count;
  compensation.clock_period_ns = clock_period_ns;
  compensation.cell_delay_ns = cost.cell_delay_ns;
  compensation.internal_power_w = cost.internal_power_w;
  compensation.leakage_power_w = cost.leakage_power_w;
  compensation.cell_power_w = cost.cell_power_w;
  return compensation;
}

auto QueryRootDriverCompensation(const RootDriverCompensationCacheKey& key, const RootClosureLoadEstimate& load_estimate,
                                 RootDriverCompensationState& state) -> HTreeRootDriverCompensation
{
  auto& stats = state.stats;
  const auto cache_it = state.cost_by_key.find(key);
  if (cache_it != state.cost_by_key.end()) {
    ++stats.cache_hit_count;
    auto result = cache_it->second;
    result.load_source = load_estimate.source;
    result.route_estimator = load_estimate.route_estimator;
    result.load_bucket_idx = load_estimate.bucket_idx;
    result.load_cap_pf = load_estimate.total_load_cap_pf;
    result.terminal_pin_cap_pf = load_estimate.terminal_pin_cap_pf;
    result.wire_cap_pf = load_estimate.wire_cap_pf;
    result.routed_wirelength_um = load_estimate.routed_wirelength_um;
    result.terminal_count = load_estimate.terminal_count;
    return result;
  }

  const auto lookup_start = std::chrono::steady_clock::now();
  const auto cost = STA_ADAPTER_INST.queryRootDriverCostDirect(key.cell_master, key.input_slew_ns, key.load_cap_pf, key.clock_period_ns);
  stats.total_runtime_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lookup_start).count();
  ++stats.unique_direct_lookup_count;
  auto compensation = MakeRootDriverCompensation(cost, key.input_slew_ns, load_estimate, key.clock_period_ns);
  return state.cost_by_key.emplace(key, std::move(compensation)).first->second;
}

auto QueryRootClosureLoadEstimate(PatternId pattern_id, const TopologyPatternLibrary& topology_library,
                                  const BufferPatternLibrary& segment_pattern_library, const Tree& topology,
                                  RootDriverCompensationState& state) -> RootClosureLoadEstimate
{
  auto cache_it = state.root_load_by_pattern.find(pattern_id);
  if (cache_it != state.root_load_by_pattern.end()) {
    ++state.stats.load_resolution_cache_hit_count;
    return cache_it->second;
  }

  const auto resolution_start = std::chrono::steady_clock::now();
  auto estimate = ResolveRootClosureLoadEstimate(pattern_id, topology_library, segment_pattern_library, topology, state.options.cap_lattice,
                                                 state.stats);
  state.stats.load_resolution_runtime_ms
      += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - resolution_start).count();
  return state.root_load_by_pattern.emplace(pattern_id, std::move(estimate)).first->second;
}

auto RootDriverCompensationCacheKeyHash::operator()(const RootDriverCompensationCacheKey& key) const noexcept -> std::size_t
{
  std::size_t seed = std::hash<std::string>{}(key.cell_master);
  seed = HashCombine(seed, std::hash<double>{}(key.input_slew_ns));
  seed = HashCombine(seed, std::hash<unsigned>{}(key.load_bucket_idx));
  seed = HashCombine(seed, std::hash<double>{}(key.load_cap_pf));
  seed = HashCombine(seed, std::hash<double>{}(key.clock_period_ns));
  return seed;
}

}  // namespace

struct RootDriverCompensationPass::Impl
{
  explicit Impl(RootDriverCompensationOptions input_options)
  {
    state.options = std::move(input_options);
    state.stats.enabled = state.options.enabled;
    state.stats.method = state.options.enabled ? kRootDriverCompensationMethod : "disabled";
    state.stats.input_slew_ns = state.options.input_slew_ns;
    state.stats.clock_period_ns = state.options.clock_period_ns;
    state.stats.load_source = state.options.enabled ? kRootDriverCompensationLoadSource : "none";
  }

  RootDriverCompensationState state;
};

RootDriverCompensationPass::RootDriverCompensationPass(RootDriverCompensationOptions options)
    : _impl(std::make_unique<Impl>(std::move(options)))
{
}

RootDriverCompensationPass::~RootDriverCompensationPass() = default;

RootDriverCompensationPass::RootDriverCompensationPass(RootDriverCompensationPass&&) noexcept = default;

auto RootDriverCompensationPass::operator=(RootDriverCompensationPass&&) noexcept -> RootDriverCompensationPass& = default;

auto RootDriverCompensationPass::beginCandidateBuild() -> void
{
  // Topology pattern ids are local to one candidate build's pattern library.
  _impl->state.root_load_by_pattern.clear();
}

auto RootDriverCompensationPass::apply(std::vector<HTreeTopologyChar>& entries, const TopologyPatternLibrary& topology_library,
                                       const BufferPatternLibrary& segment_pattern_library, const Tree& topology) -> void
{
  auto& compensation_state = _impl->state;
  auto& stats = compensation_state.stats;
  stats.enabled = compensation_state.options.enabled;
  stats.method = compensation_state.options.enabled ? kRootDriverCompensationMethod : "disabled";
  stats.input_slew_ns = compensation_state.options.input_slew_ns;
  stats.clock_period_ns = compensation_state.options.clock_period_ns;
  stats.load_source = compensation_state.options.enabled ? kRootDriverCompensationLoadSource : "none";
  if (!compensation_state.options.enabled || entries.empty()) {
    return;
  }
  if (compensation_state.options.input_slew_ns <= 0.0 || compensation_state.options.clock_period_ns <= 0.0
      || !compensation_state.options.cap_lattice.isValid()) {
    LOG_WARNING << "HTree: root-driver direct compensation skipped because slew/period/cap lattice is invalid.";
    return;
  }

  std::unordered_map<PatternId, std::string> cell_master_by_pattern;
  cell_master_by_pattern.reserve(entries.size());
  for (auto& entry : entries) {
    const auto pattern_id = entry.get_pattern_id();
    const auto cell_cache_it = cell_master_by_pattern.find(pattern_id);
    const std::string cell_master = cell_cache_it == cell_master_by_pattern.end()
                                        ? ResolveRootDriverCellMaster(pattern_id, topology_library, segment_pattern_library,
                                                                      compensation_state.options.fallback_cell_master)
                                        : cell_cache_it->second;
    if (cell_cache_it == cell_master_by_pattern.end()) {
      cell_master_by_pattern.emplace(pattern_id, cell_master);
    }
    if (cell_master.empty()) {
      continue;
    }

    const auto load_estimate
        = QueryRootClosureLoadEstimate(pattern_id, topology_library, segment_pattern_library, topology, compensation_state);
    if (!load_estimate.valid) {
      continue;
    }

    RootDriverCompensationCacheKey key{
        .cell_master = cell_master,
        .input_slew_ns = compensation_state.options.input_slew_ns,
        .load_bucket_idx = load_estimate.bucket_idx,
        .load_cap_pf = load_estimate.total_load_cap_pf,
        .clock_period_ns = compensation_state.options.clock_period_ns,
    };
    auto compensation = QueryRootDriverCompensation(key, load_estimate, compensation_state);
    entry.set_root_driver_compensation(std::move(compensation));
    ++stats.compensated_candidate_count;
  }
}

auto RootDriverCompensationPass::get_stats() const -> const RootDriverCompensationStats&
{
  return _impl->state.stats;
}

}  // namespace icts::htree
