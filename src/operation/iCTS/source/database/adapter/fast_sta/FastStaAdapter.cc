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
 * @file FastStaAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS-facing facade implementation for fast timing and power calculation.
 */

#include "FastStaAdapter.hh"

#include <glog/logging.h>

#include <chrono>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "FastStaBuilder.hh"
#include "FastStaChar.hh"
#include "FastStaIncremental.hh"
#include "FastStaPower.hh"
#include "FastStaTiming.hh"
#include "Log.hh"
#include "design/Clock.hh"

namespace icts {
namespace {

auto elapsedSeconds(std::chrono::steady_clock::time_point start_time) -> double
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
}

auto logContextSize(std::string_view owner, const FastStaClockContext& context) -> void
{
  LOG_INFO << owner << ": clock=\"" << context.clock_name << "\", nodes=" << context.nodes.size() << ", nets=" << context.nets.size()
           << ", liberty_cells=" << context.liberty_cell_by_master.size() << ".";
}

}  // namespace

auto FastStaAdapter::buildClockContext(const Clock& clock) -> FastStaClockId
{
  auto& adapter = getInst();
  const auto total_start = std::chrono::steady_clock::now();
  auto stage_start = std::chrono::steady_clock::now();
  LOG_INFO << "FastStaAdapter: start build clock context for clock \"" << clock.get_clock_name() << "\".";
  auto context = FastStaBuilder::buildClockContext(clock);
  LOG_INFO << "FastStaAdapter: base context build finished in " << elapsedSeconds(stage_start) << " s.";
  logContextSize("FastStaAdapter base context", context);
  const auto clock_id = adapter._clock_contexts.size();
  adapter._clock_contexts.push_back(std::move(context));
  adapter._clock_context_valid.push_back(true);

  stage_start = std::chrono::steady_clock::now();
  LOG_INFO << "FastStaAdapter: start initial timing update for clock \"" << clock.get_clock_name() << "\".";
  (void) updateTiming(clock_id);
  LOG_INFO << "FastStaAdapter: initial timing update for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(stage_start) << " s.";

  stage_start = std::chrono::steady_clock::now();
  LOG_INFO << "FastStaAdapter: start initial power update for clock \"" << clock.get_clock_name() << "\".";
  (void) updatePower(clock_id);
  LOG_INFO << "FastStaAdapter: initial power update for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(stage_start) << " s.";
  LOG_INFO << "FastStaAdapter: build clock context for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(total_start) << " s.";
  return clock_id;
}

auto FastStaAdapter::buildClockContext(const Clock& clock, const ClockLayout& clock_layout, std::size_t clock_index) -> FastStaClockId
{
  auto& adapter = getInst();
  const auto total_start = std::chrono::steady_clock::now();
  auto stage_start = std::chrono::steady_clock::now();
  LOG_INFO << "FastStaAdapter: start build layout clock context for clock \"" << clock.get_clock_name() << "\".";
  auto context = FastStaBuilder::buildClockContext(clock, clock_layout, clock_index);
  LOG_INFO << "FastStaAdapter: layout context build finished in " << elapsedSeconds(stage_start) << " s.";
  logContextSize("FastStaAdapter layout context", context);
  const auto clock_id = adapter._clock_contexts.size();
  adapter._clock_contexts.push_back(std::move(context));
  adapter._clock_context_valid.push_back(true);

  stage_start = std::chrono::steady_clock::now();
  LOG_INFO << "FastStaAdapter: start initial timing update for clock \"" << clock.get_clock_name() << "\".";
  (void) updateTiming(clock_id);
  LOG_INFO << "FastStaAdapter: initial timing update for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(stage_start) << " s.";

  stage_start = std::chrono::steady_clock::now();
  LOG_INFO << "FastStaAdapter: start initial power update for clock \"" << clock.get_clock_name() << "\".";
  (void) updatePower(clock_id);
  LOG_INFO << "FastStaAdapter: initial power update for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(stage_start) << " s.";
  LOG_INFO << "FastStaAdapter: build layout clock context for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(total_start) << " s.";
  return clock_id;
}

auto FastStaAdapter::rebuildClockContext(FastStaClockId clock_id) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    LOG_ERROR << "FastStaAdapter: rebuild skipped because clock context id is invalid.";
    return false;
  }
  context->timing_valid = false;
  context->power_valid = false;
  return updateTiming(clock_id) && updatePower(clock_id);
}

auto FastStaAdapter::eraseClockContext(FastStaClockId clock_id) -> bool
{
  auto& adapter = getInst();
  if (clock_id >= adapter._clock_context_valid.size() || !adapter._clock_context_valid.at(clock_id)) {
    return false;
  }
  adapter._clock_context_valid.at(clock_id) = false;
  return true;
}

auto FastStaAdapter::clear() -> void
{
  auto& adapter = getInst();
  adapter._clock_contexts.clear();
  adapter._clock_context_valid.clear();
  adapter._char_contexts.clear();
  adapter._char_context_valid.clear();
}

auto FastStaAdapter::buildCharContext(const FastStaCharTopologySpec& spec) -> FastStaCharContextId
{
  auto& adapter = getInst();
  auto context = FastStaChar::buildContext(spec);
  const auto char_context_id = adapter._char_contexts.size();
  adapter._char_contexts.push_back(std::move(context));
  adapter._char_context_valid.push_back(true);
  return char_context_id;
}

auto FastStaAdapter::eraseCharContext(FastStaCharContextId char_context_id) -> bool
{
  auto& adapter = getInst();
  if (char_context_id >= adapter._char_context_valid.size() || !adapter._char_context_valid.at(char_context_id)) {
    return false;
  }
  adapter._char_context_valid.at(char_context_id) = false;
  return true;
}

auto FastStaAdapter::setCharLoad(FastStaCharContextId char_context_id, double effective_load_pf) -> bool
{
  auto& adapter = getInst();
  if (char_context_id >= adapter._char_contexts.size() || char_context_id >= adapter._char_context_valid.size()
      || !adapter._char_context_valid.at(char_context_id)) {
    LOG_ERROR << "FastStaAdapter: characterization load update skipped because char context id is invalid.";
    return false;
  }
  return FastStaChar::setLoad(adapter._char_contexts.at(char_context_id), effective_load_pf);
}

auto FastStaAdapter::runCharSample(FastStaCharContextId char_context_id, double input_slew_ns) -> FastStaCharSampleResult
{
  auto& adapter = getInst();
  if (char_context_id >= adapter._char_contexts.size() || char_context_id >= adapter._char_context_valid.size()
      || !adapter._char_context_valid.at(char_context_id)) {
    LOG_ERROR << "FastStaAdapter: characterization sample skipped because char context id is invalid.";
    return {};
  }
  return FastStaChar::runSample(adapter._char_contexts.at(char_context_id), input_slew_ns);
}

auto FastStaAdapter::changeBufferMaster(FastStaClockId clock_id, FastStaNodeId node_id, std::string_view cell_master) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    LOG_ERROR << "FastStaAdapter: buffer master change skipped because clock context id is invalid.";
    return false;
  }
  const auto dirty_region = FastStaIncremental::changeBufferMasterIncremental(*context, node_id, cell_master);
  if (!dirty_region.has_value()) {
    return false;
  }
  return FastStaTiming::updateRegion(*context, *dirty_region) && FastStaPower::updateRegion(*context, *dirty_region);
}

auto FastStaAdapter::changeBufferMasters(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    LOG_ERROR << "FastStaAdapter: buffer master batch change skipped because clock context id is invalid.";
    return false;
  }
  if (changes.empty()) {
    return context->timing_valid && context->power_valid;
  }
  if (!FastStaIncremental::changeBufferMasters(*context, changes)) {
    return false;
  }
  return FastStaTiming::update(*context) && FastStaPower::update(*context);
}

auto FastStaAdapter::changeBufferMastersTimingOnly(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    LOG_ERROR << "FastStaAdapter: timing-only buffer master batch change skipped because clock context id is invalid.";
    return false;
  }
  if (changes.empty()) {
    return context->timing_valid;
  }
  if (!FastStaIncremental::changeBufferMasters(*context, changes)) {
    return false;
  }
  const bool timing_updated = FastStaTiming::update(*context);
  context->power_valid = false;
  return timing_updated;
}

auto FastStaAdapter::updateTiming(FastStaClockId clock_id) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    LOG_ERROR << "FastStaAdapter: timing update skipped because clock context id is invalid.";
    return false;
  }
  return FastStaTiming::update(*context);
}

auto FastStaAdapter::updatePower(FastStaClockId clock_id) -> bool
{
  auto* context = mutableClockContext(clock_id);
  if (context == nullptr) {
    LOG_ERROR << "FastStaAdapter: power update skipped because clock context id is invalid.";
    return false;
  }
  return FastStaPower::update(*context);
}

auto FastStaAdapter::querySinkArrival(FastStaClockId clock_id, std::string_view sink_pin_name) -> std::optional<double>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr) {
    return std::nullopt;
  }
  const auto iter = context->node_id_by_name.find(std::string(sink_pin_name));
  if (iter == context->node_id_by_name.end() || iter->second >= context->nodes.size()) {
    return std::nullopt;
  }
  const auto& node = context->nodes.at(iter->second);
  if (node.kind != FastStaNodeKind::kSink || !node.timing.valid) {
    return std::nullopt;
  }
  return node.timing.arrival_ns;
}

auto FastStaAdapter::querySkew(FastStaClockId clock_id) -> FastStaSkewSummary
{
  const auto* context = queryClockContext(clock_id);
  return context == nullptr ? FastStaSkewSummary{} : context->skew;
}

auto FastStaAdapter::queryNodeSlew(FastStaClockId clock_id, FastStaNodeId node_id) -> std::optional<double>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr || node_id >= context->nodes.size() || !context->nodes.at(node_id).timing.valid) {
    return std::nullopt;
  }
  return context->nodes.at(node_id).timing.slew_ns;
}

auto FastStaAdapter::queryNetLoad(FastStaClockId clock_id, FastStaNetId net_id) -> std::optional<double>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr || net_id >= context->nets.size()) {
    return std::nullopt;
  }
  return context->nets.at(net_id).load_cap_pf;
}

auto FastStaAdapter::queryCapStatus(FastStaClockId clock_id, FastStaNetId net_id) -> std::optional<FastStaCapStatus>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr || net_id >= context->nets.size()) {
    return std::nullopt;
  }
  const auto& net = context->nets.at(net_id);
  return FastStaCapStatus{.net_id = net_id,
                          .net_name = net.name,
                          .load_cap_pf = net.load_cap_pf,
                          .max_cap_pf = net.max_cap_pf,
                          .violated = net.max_cap_pf > 0.0 && net.load_cap_pf > net.max_cap_pf};
}

auto FastStaAdapter::querySlewStatus(FastStaClockId clock_id, FastStaNodeId node_id) -> std::optional<FastStaSlewStatus>
{
  const auto* context = queryClockContext(clock_id);
  if (context == nullptr || node_id >= context->nodes.size()) {
    return std::nullopt;
  }
  const auto& node = context->nodes.at(node_id);
  if (!node.timing.valid) {
    return std::nullopt;
  }
  return FastStaSlewStatus{.node_id = node_id,
                           .node_name = node.name,
                           .slew_ns = node.timing.slew_ns,
                           .max_slew_ns = node.max_slew_ns,
                           .violated = node.max_slew_ns > 0.0 && node.timing.slew_ns > node.max_slew_ns};
}

auto FastStaAdapter::queryPower(FastStaClockId clock_id) -> FastStaPowerSummary
{
  const auto* context = queryClockContext(clock_id);
  return context == nullptr ? FastStaPowerSummary{} : context->power;
}

auto FastStaAdapter::queryArea(FastStaClockId clock_id) -> double
{
  return queryPower(clock_id).area_um2;
}

auto FastStaAdapter::queryClockContext(FastStaClockId clock_id) -> const FastStaClockContext*
{
  const auto& adapter = getInst();
  if (clock_id >= adapter._clock_contexts.size() || clock_id >= adapter._clock_context_valid.size()
      || !adapter._clock_context_valid.at(clock_id)) {
    return nullptr;
  }
  return &adapter._clock_contexts.at(clock_id);
}

auto FastStaAdapter::mutableClockContext(FastStaClockId clock_id) -> FastStaClockContext*
{
  auto& adapter = getInst();
  if (clock_id >= adapter._clock_contexts.size() || clock_id >= adapter._clock_context_valid.size()
      || !adapter._clock_context_valid.at(clock_id)) {
    return nullptr;
  }
  return &adapter._clock_contexts.at(clock_id);
}

auto FastStaAdapter::queryClockIds() -> std::vector<FastStaClockId>
{
  const auto& adapter = getInst();
  std::vector<FastStaClockId> ids;
  ids.reserve(adapter._clock_contexts.size());
  for (FastStaClockId id = 0U; id < adapter._clock_contexts.size(); ++id) {
    if (id < adapter._clock_context_valid.size() && adapter._clock_context_valid.at(id)) {
      ids.push_back(id);
    }
  }
  return ids;
}

}  // namespace icts
