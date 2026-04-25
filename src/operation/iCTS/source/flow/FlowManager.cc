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
 * @file FlowManager.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-25
 * @brief CTS flow orchestration manager implementation
 */

#include "FlowManager.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "logger/Schema.hh"
#include "spatial/Point.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts {
namespace {

constexpr std::size_t kMinSynthesisSinkCount = 2U;

struct BranchPins
{
  Inst* inst = nullptr;
  Pin* input_pin = nullptr;
  Pin* output_pin = nullptr;
};

struct OwnedCtsObjects
{
  std::vector<std::unique_ptr<Inst>> inst_storage;
  std::vector<std::unique_ptr<Pin>> pin_storage;
  std::vector<std::unique_ptr<Net>> net_storage;
  std::vector<Inst*> inserted_insts;
  std::vector<Net*> inserted_nets;
};

struct BranchWork
{
  FlowManager::BranchSummary summary;
  std::vector<Pin*> sinks;
  BranchPins root_buffer;
};

struct SinkPartition
{
  std::vector<Pin*> hard_macro_sinks;
  std::vector<Pin*> regular_sinks;
  std::vector<std::pair<Pin*, Net*>> original_sink_nets;
};

auto branchKindName(FlowManager::BranchKind kind) -> const char*
{
  return kind == FlowManager::BranchKind::kHardMacro ? "hard_macro" : "regular";
}

auto makeSafeNameToken(const std::string& value, const std::string& fallback) -> std::string
{
  std::string token;
  token.reserve(value.size());
  for (const auto character : value) {
    const auto uch = static_cast<unsigned char>(character);
    if (std::isalnum(uch) != 0) {
      token.push_back(static_cast<char>(character));
    } else {
      token.push_back('_');
    }
  }
  const auto duplicate_underscores = std::ranges::unique(token, [](char lhs, char rhs) -> bool { return lhs == '_' && rhs == '_'; });
  token.erase(duplicate_underscores.begin(), duplicate_underscores.end());
  while (!token.empty() && token.front() == '_') {
    token.erase(token.begin());
  }
  while (!token.empty() && token.back() == '_') {
    token.pop_back();
  }
  return token.empty() ? fallback : token;
}

auto makeClockPrefix(const Clock& clock, std::size_t clock_index) -> std::string
{
  return "cts_flow_clk_" + std::to_string(clock_index) + "_" + makeSafeNameToken(clock.get_clock_name(), "clock");
}

auto makeBranchPrefix(const Clock& clock, std::size_t clock_index, FlowManager::BranchKind kind) -> std::string
{
  return makeClockPrefix(clock, clock_index) + "_" + branchKindName(kind);
}

auto makePinName(const std::string& inst_name, const std::string& pin_name) -> std::string
{
  return inst_name + "/" + pin_name;
}

auto resolveBufferDriveCap(const FlowManager::BranchRootBufferSpec& spec) -> double
{
  if (spec.output_drive_cap_pf.has_value()) {
    return *spec.output_drive_cap_pf;
  }

  double drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(spec.cell_master);
  if (drive_cap_pf <= 0.0) {
    drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(spec.cell_master);
  }
  return drive_cap_pf;
}

auto resolveRootBufferSpec(const FlowManager::BranchRootBufferSpec& input_spec, bool require_output_drive)
    -> std::optional<FlowManager::BranchRootBufferSpec>
{
  if (input_spec.cell_master.empty()) {
    return std::nullopt;
  }

  auto resolved_spec = input_spec;
  if (resolved_spec.input_pin.empty() || resolved_spec.output_pin.empty()) {
    auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(resolved_spec.cell_master);
    resolved_spec.input_pin = std::move(input_pin);
    resolved_spec.output_pin = std::move(output_pin);
  }

  if (resolved_spec.input_pin.empty() || resolved_spec.output_pin.empty()) {
    LOG_WARNING << "FlowManager: skip branch root buffer master \"" << resolved_spec.cell_master
                << "\" because buffer ports are unresolved.";
    return std::nullopt;
  }

  if (!require_output_drive && !resolved_spec.output_drive_cap_pf.has_value()) {
    return resolved_spec;
  }

  const double drive_cap_pf = resolveBufferDriveCap(resolved_spec);
  if (require_output_drive && drive_cap_pf <= 0.0) {
    LOG_WARNING << "FlowManager: skip branch root buffer master \"" << resolved_spec.cell_master
                << "\" because output drive cap is unresolved.";
    return std::nullopt;
  }
  if (drive_cap_pf > 0.0) {
    resolved_spec.output_drive_cap_pf = drive_cap_pf;
  }

  return resolved_spec;
}

auto selectMinimumDriveRootBufferSpec(const std::vector<FlowManager::BranchRootBufferSpec>& candidates)
    -> std::optional<FlowManager::BranchRootBufferSpec>
{
  std::optional<FlowManager::BranchRootBufferSpec> best_spec = std::nullopt;
  double best_drive_cap_pf = std::numeric_limits<double>::infinity();

  for (const auto& candidate : candidates) {
    auto resolved_candidate = resolveRootBufferSpec(candidate, true);
    if (!resolved_candidate.has_value()) {
      continue;
    }

    const double drive_cap_pf = resolved_candidate->output_drive_cap_pf.value_or(std::numeric_limits<double>::infinity());
    if (!best_spec.has_value() || drive_cap_pf < best_drive_cap_pf
        || (drive_cap_pf == best_drive_cap_pf && resolved_candidate->cell_master < best_spec->cell_master)) {
      best_spec = *resolved_candidate;
      best_drive_cap_pf = drive_cap_pf;
    }
  }

  return best_spec;
}

auto buildConfiguredRootBufferCandidates() -> std::vector<FlowManager::BranchRootBufferSpec>
{
  std::vector<FlowManager::BranchRootBufferSpec> candidates;
  const auto& buffer_types = CONFIG_INST.get_buffer_types();
  candidates.reserve(buffer_types.size());
  for (const auto& cell_master : buffer_types) {
    if (cell_master.empty()) {
      continue;
    }
    candidates.push_back(FlowManager::BranchRootBufferSpec{
        .cell_master = cell_master,
        .input_pin = "",
        .output_pin = "",
        .output_drive_cap_pf = std::nullopt,
    });
  }
  return candidates;
}

auto resolveMinimumDriveRootBufferSpec(const FlowManager::RunOptions& options) -> std::optional<FlowManager::BranchRootBufferSpec>
{
  if (!options.branch_root_buffer_candidates.empty()) {
    auto candidate_spec = selectMinimumDriveRootBufferSpec(options.branch_root_buffer_candidates);
    if (candidate_spec.has_value()) {
      return candidate_spec;
    }
    LOG_ERROR << "FlowManager: failed to resolve a minimum-drive branch root buffer from option candidates.";
    return std::nullopt;
  }

  auto configured_candidates = buildConfiguredRootBufferCandidates();
  if (!configured_candidates.empty()) {
    auto configured_spec = selectMinimumDriveRootBufferSpec(configured_candidates);
    if (configured_spec.has_value()) {
      return configured_spec;
    }
    LOG_ERROR << "FlowManager: failed to resolve a minimum-drive branch root buffer from configured buffer types.";
    return std::nullopt;
  }

  if (options.branch_root_buffer.has_value()) {
    auto explicit_spec = resolveRootBufferSpec(*options.branch_root_buffer, true);
    if (explicit_spec.has_value()) {
      return explicit_spec;
    }
    LOG_ERROR << "FlowManager: configured branch root buffer spec cannot be used as a minimum-drive fallback.";
    return std::nullopt;
  }

  LOG_ERROR << "FlowManager: no branch root buffer candidates are configured.";
  return std::nullopt;
}

auto resolveInitialRootBufferSpec(const FlowManager::RunOptions& options, const FlowManager::BranchRootBufferSpec& minimum_drive_fallback)
    -> std::optional<FlowManager::BranchRootBufferSpec>
{
  if (!options.branch_root_buffer.has_value()) {
    return minimum_drive_fallback;
  }

  auto explicit_spec = resolveRootBufferSpec(*options.branch_root_buffer, false);
  if (!explicit_spec.has_value()) {
    LOG_ERROR << "FlowManager: configured initial branch root buffer spec is incomplete.";
    return std::nullopt;
  }
  return explicit_spec;
}

auto resolveRecommendedRootBufferSpec(const FlowManager::BranchRootBufferSpec& recommendation)
    -> std::optional<FlowManager::BranchRootBufferSpec>
{
  return resolveRootBufferSpec(recommendation, true);
}

auto resolveRecommendedRootBufferSpec(const ClockSynthesis::BuildResult& synthesis_result)
    -> std::optional<FlowManager::BranchRootBufferSpec>
{
  if (synthesis_result.recommended_root_driver_cell_master.empty()) {
    return std::nullopt;
  }

  return resolveRecommendedRootBufferSpec(FlowManager::BranchRootBufferSpec{
      .cell_master = synthesis_result.recommended_root_driver_cell_master,
      .input_pin = "",
      .output_pin = "",
      .output_drive_cap_pf = std::nullopt,
  });
}

auto resolveBranchLocation(Pin* clock_source, const std::vector<Pin*>& sinks) -> Point<int>
{
  long long sum_x = 0;
  long long sum_y = 0;
  std::size_t count = 0U;
  for (const auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }
    const auto location = sink->get_location();
    if (location.get_x() < 0 || location.get_y() < 0) {
      continue;
    }
    sum_x += location.get_x();
    sum_y += location.get_y();
    ++count;
  }

  if (count > 0U) {
    return Point<int>(static_cast<int>(sum_x / static_cast<long long>(count)), static_cast<int>(sum_y / static_cast<long long>(count)));
  }
  if (clock_source != nullptr) {
    return clock_source->get_location();
  }
  return Point<int>(0, 0);
}

auto createBuffer(OwnedCtsObjects& owned_objects, const std::string& inst_name, const FlowManager::BranchRootBufferSpec& buffer_spec,
                  const Point<int>& location) -> BranchPins
{
  auto inst = std::make_unique<Inst>(inst_name, buffer_spec.cell_master, InstType::kBuffer, location);
  auto* inst_ptr = inst.get();

  auto input_pin = std::make_unique<Pin>(makePinName(inst_name, buffer_spec.input_pin), PinType::kIn, location, inst_ptr, nullptr, false);
  auto output_pin
      = std::make_unique<Pin>(makePinName(inst_name, buffer_spec.output_pin), PinType::kOut, location, inst_ptr, nullptr, false);
  auto* input_pin_ptr = input_pin.get();
  auto* output_pin_ptr = output_pin.get();

  inst_ptr->insertDriverPin(output_pin_ptr);
  inst_ptr->add_pin(input_pin_ptr);

  owned_objects.inserted_insts.push_back(inst_ptr);
  owned_objects.inst_storage.push_back(std::move(inst));
  owned_objects.pin_storage.push_back(std::move(output_pin));
  owned_objects.pin_storage.push_back(std::move(input_pin));

  return BranchPins{
      .inst = inst_ptr,
      .input_pin = input_pin_ptr,
      .output_pin = output_pin_ptr,
  };
}

auto createStandalonePin(OwnedCtsObjects& owned_objects, const std::string& pin_name, PinType pin_type, const Point<int>& location) -> Pin*
{
  auto pin = std::make_unique<Pin>(pin_name, pin_type, location, nullptr, nullptr, true);
  auto* pin_ptr = pin.get();
  owned_objects.pin_storage.push_back(std::move(pin));
  return pin_ptr;
}

auto applyRootBufferSpec(BranchWork& branch, const FlowManager::BranchRootBufferSpec& buffer_spec, bool used_recommendation) -> void
{
  if (branch.root_buffer.inst == nullptr || branch.root_buffer.input_pin == nullptr || branch.root_buffer.output_pin == nullptr) {
    return;
  }

  branch.root_buffer.inst->set_cell_master(buffer_spec.cell_master);
  branch.root_buffer.input_pin->set_name(makePinName(branch.root_buffer.inst->get_name(), buffer_spec.input_pin));
  branch.root_buffer.output_pin->set_name(makePinName(branch.root_buffer.inst->get_name(), buffer_spec.output_pin));
  branch.summary.root_buffer_cell_master = buffer_spec.cell_master;
  branch.summary.used_recommended_root_driver = used_recommendation;
  branch.summary.used_minimum_drive_root_driver = !used_recommendation;
}

auto createNet(OwnedCtsObjects& owned_objects, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads) -> Net*
{
  auto net = std::make_unique<Net>(net_name);
  auto* net_ptr = net.get();
  net_ptr->set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(net_ptr);
  }

  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net_ptr->add_load(load);
    load->set_net(net_ptr);
  }

  owned_objects.inserted_nets.push_back(net_ptr);
  owned_objects.net_storage.push_back(std::move(net));
  return net_ptr;
}

auto absorbSynthesisResult(OwnedCtsObjects& owned_objects, ClockSynthesis::BuildResult& synthesis_result) -> void
{
  owned_objects.inserted_insts.insert(owned_objects.inserted_insts.end(), synthesis_result.inserted_insts.begin(),
                                      synthesis_result.inserted_insts.end());
  owned_objects.inserted_nets.insert(owned_objects.inserted_nets.end(), synthesis_result.inserted_nets.begin(),
                                     synthesis_result.inserted_nets.end());

  owned_objects.inst_storage.reserve(owned_objects.inst_storage.size() + synthesis_result.inst_storage.size());
  for (auto& inst : synthesis_result.inst_storage) {
    owned_objects.inst_storage.push_back(std::move(inst));
  }
  synthesis_result.inst_storage.clear();

  owned_objects.pin_storage.reserve(owned_objects.pin_storage.size() + synthesis_result.pin_storage.size());
  for (auto& pin : synthesis_result.pin_storage) {
    owned_objects.pin_storage.push_back(std::move(pin));
  }
  synthesis_result.pin_storage.clear();

  owned_objects.net_storage.reserve(owned_objects.net_storage.size() + synthesis_result.net_storage.size());
  for (auto& net : synthesis_result.net_storage) {
    owned_objects.net_storage.push_back(std::move(net));
  }
  synthesis_result.net_storage.clear();
}

auto adoptClockObjects(Clock& clock, OwnedCtsObjects& owned_objects) -> void
{
  clock.adoptInsertedCtsOwnership(std::move(owned_objects.inst_storage), std::move(owned_objects.pin_storage),
                                  std::move(owned_objects.net_storage));

  for (auto* inst : owned_objects.inserted_insts) {
    if (inst != nullptr) {
      clock.add_inserted_inst(inst);
    }
  }
  for (auto* net : owned_objects.inserted_nets) {
    if (net != nullptr) {
      clock.add_inserted_net(net);
    }
  }
}

auto partitionSinks(const std::vector<Pin*>& sinks) -> SinkPartition
{
  SinkPartition partition;
  partition.original_sink_nets.reserve(sinks.size());
  for (auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }
    partition.original_sink_nets.emplace_back(sink, sink->get_net());
    const auto* inst = sink->get_inst();
    if (inst != nullptr && inst->is_macro_block()) {
      partition.hard_macro_sinks.push_back(sink);
    } else {
      partition.regular_sinks.push_back(sink);
    }
  }
  return partition;
}

auto restoreOriginalNets(Pin* clock_source, Net* original_source_net, const std::vector<std::pair<Pin*, Net*>>& original_sink_nets) -> void
{
  if (clock_source != nullptr) {
    clock_source->set_net(original_source_net);
  }
  for (const auto& [sink, net] : original_sink_nets) {
    if (sink != nullptr) {
      sink->set_net(net);
    }
  }
}

auto detachOriginalClockNetPins(Pin* clock_source, Net* original_source_net, const std::vector<std::pair<Pin*, Net*>>& original_sink_nets)
    -> void
{
  if (original_source_net != nullptr && original_source_net->get_driver() == clock_source) {
    original_source_net->set_driver(nullptr);
  }

  for (const auto& [sink, net] : original_sink_nets) {
    if (sink == nullptr || net == nullptr) {
      continue;
    }

    auto updated_loads = net->get_loads();
    std::erase(updated_loads, sink);
    net->set_loads(updated_loads);
  }
}

auto appendBranchIfPresent(std::vector<BranchWork>& branches, FlowManager::BranchKind kind, const std::vector<Pin*>& sinks) -> void
{
  if (sinks.empty()) {
    return;
  }

  BranchWork branch;
  branch.summary.kind = kind;
  branch.summary.sink_count = sinks.size();
  branch.sinks = sinks;
  branches.push_back(std::move(branch));
}

auto buildDirectBranch(OwnedCtsObjects& owned_objects, const std::string& branch_prefix, BranchWork& branch,
                       const FlowManager::BranchRootBufferSpec& minimum_drive_fallback) -> void
{
  branch.summary.direct_sink_net
      = createNet(owned_objects, branch_prefix + "_direct_sink_net", branch.root_buffer.output_pin, branch.sinks);
  applyRootBufferSpec(branch, minimum_drive_fallback, false);
  branch.summary.used_direct_connection = true;
  branch.summary.success = true;
}

auto buildSyntheticSynthesisBranch(OwnedCtsObjects& owned_objects, const std::string& branch_prefix, BranchWork& branch) -> void
{
  const auto root_location = branch.root_buffer.output_pin != nullptr ? branch.root_buffer.output_pin->get_location() : Point<int>(0, 0);
  auto* root_input_pin = createStandalonePin(owned_objects, branch_prefix + "_htree_root_in", PinType::kIn, root_location);
  auto* root_output_pin = createStandalonePin(owned_objects, branch_prefix + "_htree_root_out", PinType::kOut, root_location);
  branch.summary.synthesis_source_to_root_net = createNet(owned_objects, branch_prefix + "_clock_source_to_htree_root",
                                                          branch.root_buffer.output_pin, std::vector<Pin*>{root_input_pin});
  createNet(owned_objects, branch_prefix + "_synthetic_htree_root_to_sinks", root_output_pin, branch.sinks);
}

auto buildSynthesisBranch(OwnedCtsObjects& owned_objects, const std::string& branch_prefix, BranchWork& branch,
                          const FlowManager::RunOptions& options, const FlowManager::BranchRootBufferSpec& minimum_drive_fallback) -> bool
{
  branch.summary.used_synthesis = true;

  std::optional<FlowManager::BranchRootBufferSpec> recommended_root_driver = std::nullopt;
  if (options.branch_synthesis_override.has_value()) {
    if (!options.branch_synthesis_override->success) {
      branch.summary.failure_reason = options.branch_synthesis_override->failure_reason.empty()
                                          ? "branch synthesis failed"
                                          : options.branch_synthesis_override->failure_reason;
      return false;
    }
    buildSyntheticSynthesisBranch(owned_objects, branch_prefix, branch);
    if (options.branch_synthesis_override->recommended_root_driver.has_value()) {
      recommended_root_driver = resolveRecommendedRootBufferSpec(*options.branch_synthesis_override->recommended_root_driver);
    }
  } else {
    ClockSynthesis::BuildOptions synthesis_options;
    synthesis_options.object_name_prefix = branch_prefix;
    auto synthesis_result = ClockSynthesis::build(branch.root_buffer.output_pin, branch.sinks, synthesis_options);
    branch.summary.synthesis_source_to_root_net = synthesis_result.source_to_root_net;
    if (!synthesis_result.success) {
      branch.summary.failure_reason = synthesis_result.failure_reason.empty() ? "branch synthesis failed" : synthesis_result.failure_reason;
      return false;
    }
    recommended_root_driver = resolveRecommendedRootBufferSpec(synthesis_result);
    absorbSynthesisResult(owned_objects, synthesis_result);
  }

  if (recommended_root_driver.has_value()) {
    applyRootBufferSpec(branch, *recommended_root_driver, true);
  } else {
    applyRootBufferSpec(branch, minimum_drive_fallback, false);
  }
  branch.summary.success = true;
  return true;
}

auto clockStatusName(const FlowManager::ClockSummary& clock_summary) -> const char*
{
  if (clock_summary.success) {
    return "success";
  }
  return clock_summary.skipped ? "skipped" : "failed";
}

auto emitRunSummary(const FlowManager::RunSummary& summary) -> void
{
  schema::EmitKeyValueTable("CTS Flow Summary", {
                                                    {"success", summary.success ? "true" : "false"},
                                                    {"total_clocks", std::to_string(summary.total_clocks)},
                                                    {"successful_clocks", std::to_string(summary.successful_clocks)},
                                                    {"skipped_clocks", std::to_string(summary.skipped_clocks)},
                                                    {"failed_clocks", std::to_string(summary.failed_clocks)},
                                                    {"total_branches", std::to_string(summary.total_branches)},
                                                    {"hard_macro_sinks", std::to_string(summary.hard_macro_sinks)},
                                                    {"regular_sinks", std::to_string(summary.regular_sinks)},
                                                });

  schema::TableRows rows;
  for (const auto& clock_summary : summary.clocks) {
    if (clock_summary.branches.empty()) {
      rows.push_back({
          clock_summary.clock_name,
          clock_summary.clock_net_name,
          clockStatusName(clock_summary),
          "none",
          std::to_string(clock_summary.valid_sinks),
          "0",
          clock_summary.failure_reason,
      });
      continue;
    }
    for (const auto& branch : clock_summary.branches) {
      rows.push_back({
          clock_summary.clock_name,
          clock_summary.clock_net_name,
          branch.success ? "success" : "failed",
          branchKindName(branch.kind),
          std::to_string(clock_summary.valid_sinks),
          std::to_string(branch.sink_count),
          branch.failure_reason,
      });
    }
  }

  if (!rows.empty()) {
    schema::EmitTable("CTS Flow Branches", {"Clock", "Net", "Status", "Branch", "Valid Sinks", "Branch Sinks", "Detail"}, rows);
  }
}

}  // namespace

auto FlowManager::run() -> RunSummary
{
  return run(RunOptions{});
}

auto FlowManager::run(const RunOptions& options) -> RunSummary
{
  RunSummary summary;
  auto clocks = DESIGN_INST.get_clocks();
  summary.total_clocks = clocks.size();
  summary.clocks.reserve(clocks.size());

  for (std::size_t clock_index = 0; clock_index < clocks.size(); ++clock_index) {
    auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      ClockSummary clock_summary;
      clock_summary.skipped = true;
      clock_summary.failure_reason = "clock pointer is null";
      summary.clocks.push_back(std::move(clock_summary));
      ++summary.skipped_clocks;
      continue;
    }

    auto clock_summary = runClock(*clock, clock_index, options);
    summary.total_branches += clock_summary.branches.size();
    summary.hard_macro_sinks += clock_summary.hard_macro_sinks;
    summary.regular_sinks += clock_summary.regular_sinks;
    if (clock_summary.success) {
      ++summary.successful_clocks;
    } else if (clock_summary.skipped) {
      ++summary.skipped_clocks;
    } else {
      ++summary.failed_clocks;
    }
    summary.clocks.push_back(std::move(clock_summary));
  }

  summary.success = summary.failed_clocks == 0U;
  LOG_INFO << "CTS flow finished with " << summary.successful_clocks << " successful, " << summary.skipped_clocks << " skipped, "
           << summary.failed_clocks << " failed clocks.";
  emitRunSummary(summary);
  return summary;
}

auto FlowManager::runClock(Clock& clock, std::size_t clock_index, const RunOptions& options) -> ClockSummary
{
  ClockSummary clock_summary;
  clock_summary.clock_name = clock.get_clock_name();
  clock_summary.clock_net_name = clock.get_clock_net_name();
  clock_summary.total_sinks = clock.get_loads().size();

  clock.clearInsertedCtsObjects();

  auto* clock_source = clock.get_clock_source();
  auto* original_source_net = clock_source != nullptr ? clock_source->get_net() : nullptr;
  if (clock_source == nullptr) {
    clock_summary.skipped = true;
    clock_summary.failure_reason = "clock source is null";
    LOG_WARNING << "FlowManager: skip clock \"" << clock_summary.clock_name << "\" because clock source is null.";
    return clock_summary;
  }

  auto partition = partitionSinks(clock.get_loads());
  clock_summary.valid_sinks = partition.hard_macro_sinks.size() + partition.regular_sinks.size();
  clock_summary.hard_macro_sinks = partition.hard_macro_sinks.size();
  clock_summary.regular_sinks = partition.regular_sinks.size();
  if (clock_summary.valid_sinks == 0U) {
    clock_summary.skipped = true;
    clock_summary.failure_reason = "no valid sinks";
    LOG_WARNING << "FlowManager: skip clock \"" << clock_summary.clock_name << "\" because no valid sinks are available.";
    return clock_summary;
  }

  const auto minimum_drive_fallback = resolveMinimumDriveRootBufferSpec(options);
  if (!minimum_drive_fallback.has_value()) {
    clock_summary.failure_reason = "failed to resolve branch root buffer";
    LOG_ERROR << "FlowManager: clock \"" << clock_summary.clock_name << "\" failed because branch root buffer is unresolved.";
    return clock_summary;
  }
  const auto initial_root_buffer_spec = resolveInitialRootBufferSpec(options, *minimum_drive_fallback);
  if (!initial_root_buffer_spec.has_value()) {
    clock_summary.failure_reason = "failed to resolve initial branch root buffer";
    LOG_ERROR << "FlowManager: clock \"" << clock_summary.clock_name << "\" failed because initial branch root buffer is unresolved.";
    return clock_summary;
  }

  OwnedCtsObjects owned_objects;
  std::vector<BranchWork> branches;
  appendBranchIfPresent(branches, BranchKind::kHardMacro, partition.hard_macro_sinks);
  appendBranchIfPresent(branches, BranchKind::kRegular, partition.regular_sinks);

  for (auto& branch : branches) {
    const auto branch_prefix = makeBranchPrefix(clock, clock_index, branch.summary.kind);
    const auto location = resolveBranchLocation(clock_source, branch.sinks);
    branch.root_buffer = createBuffer(owned_objects, branch_prefix + "_root_buf", *initial_root_buffer_spec, location);
    branch.summary.root_buffer_inst = branch.root_buffer.inst;
    branch.summary.root_buffer_input_pin = branch.root_buffer.input_pin;
    branch.summary.root_buffer_output_pin = branch.root_buffer.output_pin;
    branch.summary.root_buffer_cell_master = initial_root_buffer_spec->cell_master;

    if (branch.sinks.size() < kMinSynthesisSinkCount) {
      buildDirectBranch(owned_objects, branch_prefix, branch, *minimum_drive_fallback);
      continue;
    }
    if (!buildSynthesisBranch(owned_objects, branch_prefix, branch, options, *minimum_drive_fallback)) {
      clock_summary.failure_reason = branch.summary.failure_reason;
      clock_summary.branches.push_back(branch.summary);
      restoreOriginalNets(clock_source, original_source_net, partition.original_sink_nets);
      LOG_ERROR << "FlowManager: clock \"" << clock_summary.clock_name << "\" branch " << branchKindName(branch.summary.kind)
                << " failed: " << branch.summary.failure_reason;
      return clock_summary;
    }
  }

  std::vector<Pin*> branch_root_inputs;
  branch_root_inputs.reserve(branches.size());
  for (const auto& branch : branches) {
    if (branch.root_buffer.input_pin != nullptr) {
      branch_root_inputs.push_back(branch.root_buffer.input_pin);
    }
  }
  detachOriginalClockNetPins(clock_source, original_source_net, partition.original_sink_nets);
  clock_summary.source_to_branch_roots_net
      = createNet(owned_objects, makeClockPrefix(clock, clock_index) + "_source_to_branch_roots", clock_source, branch_root_inputs);

  for (const auto& branch : branches) {
    clock_summary.branches.push_back(branch.summary);
  }
  adoptClockObjects(clock, owned_objects);
  clock_summary.success = true;
  return clock_summary;
}

}  // namespace icts
