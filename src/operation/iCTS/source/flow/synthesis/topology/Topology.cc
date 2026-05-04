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
 * @file Topology.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-01
 * @brief CTS topology formation entry for sink branches and source trunk.
 */

#include "synthesis/topology/Topology.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "Log.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "geometry/Geometry.hh"
#include "instantiation/design_conversion/DesignConversion.hh"
#include "io/Wrapper.hh"
#include "synthesis/distribution/ClockDistribution.hh"
#include "synthesis/topology/sink/SinkBranch.hh"
#include "synthesis/topology/trunk/SourceTrunk.hh"
#include "synthesis/trace/SynthesisTrace.hh"
#include "synthesis/trace/domain_status/DomainStatus.hh"
#include "synthesis/trace/layout/ClockLayoutAdapter.hh"
#include "synthesis/trace/layout/ClockLayoutBuilder.hh"

namespace icts {
namespace {

constexpr std::size_t kMinSynthesisSinkCount = 2U;

auto recordSynthesisResult(SynthesisTraceSummary& summary, const Topology::BuildResult& result) -> void
{
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, result.selected_htree_level_count);
  if (result.selected_htree_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *result.selected_htree_depth);
  }
  summary.htree_inserted_buffer_count += result.htree_inserted_buffer_count;
  summary.htree_inserted_net_count += result.htree_inserted_net_count;
}

auto recordSourceTrunkResult(SynthesisTraceSummary& summary, const Topology::SourceTrunkBuildResult& result) -> void
{
  if (result.htree_result.selected_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *result.htree_result.selected_depth);
  }
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, result.htree_result.levels.size());
  summary.htree_inserted_buffer_count += result.inserted_buffer_count;
  summary.htree_inserted_net_count += result.inserted_net_count;
}

auto sourceTrunkSynthesisPhase(Topology::SourceTrunkStage stage) -> ClockLayoutPhase
{
  switch (stage) {
    case Topology::SourceTrunkStage::kSegment:
      return ClockLayoutPhase::kSourceToRootSegment;
    case Topology::SourceTrunkStage::kHTree:
      return ClockLayoutPhase::kSourceToRootHTree;
    case Topology::SourceTrunkStage::kUnknown:
      return ClockLayoutPhase::kUnknown;
  }
  return ClockLayoutPhase::kUnknown;
}

auto makeLogContext(const Clock& clock, const std::string& sink_domain, const std::string& stage, const std::string& object_name_prefix)
    -> HTree::LogContext
{
  return HTree::LogContext{
      .clock_name = clock.get_clock_name(),
      .clock_net_name = clock.get_clock_net_name(),
      .sink_domain = sink_domain,
      .stage = stage,
      .object_name_prefix = object_name_prefix,
  };
}

auto clearClockCtsMembership(Clock& clock) -> void
{
  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();
}

auto collectRootInputs(const std::vector<ClockDistributionContext>& sink_domains) -> std::vector<Pin*>
{
  std::vector<Pin*> root_inputs;
  root_inputs.reserve(sink_domains.size());
  for (const auto& context : sink_domains) {
    if (context.root_input != nullptr) {
      root_inputs.push_back(context.root_input);
    }
  }
  return root_inputs;
}

auto collectSourceTrunkLengthsUm(Pin* clock_source, const std::vector<Pin*>& root_inputs) -> std::vector<double>
{
  std::vector<double> lengths_um;
  if (clock_source == nullptr) {
    return lengths_um;
  }

  const double dbu_per_um = static_cast<double>(std::max(WRAPPER_INST.queryDbUnit(), int32_t{1}));
  lengths_um.reserve(root_inputs.size());
  for (const auto* root_input : root_inputs) {
    if (root_input == nullptr) {
      continue;
    }
    const int distance_dbu = geometry::Manhattan(clock_source->get_location(), root_input->get_location());
    const double length_um = static_cast<double>(std::max(distance_dbu, 0)) / dbu_per_um;
    if (length_um > 0.0) {
      lengths_um.push_back(length_um);
    }
  }
  return lengths_um;
}

class ClockTopologyFormation
{
 public:
  ClockTopologyFormation(Clock& clock, std::size_t clock_index, ClockLayout& clock_layout, SynthesisTraceSummary& summary,
                         DomainStatusTable& status_table, CharacterizationLibrary& characterization_library, std::size_t valid_sinks)
      : _clock(&clock),
        _clock_index(clock_index),
        _clock_layout(&clock_layout),
        _summary(&summary),
        _status_table(&status_table),
        _characterization_library(&characterization_library),
        _valid_sinks(valid_sinks)
  {
  }

  auto form(const std::vector<ClockDistributionContext>& sink_domains) -> bool
  {
    auto root_inputs = collectRootInputs(sink_domains);
    const auto source_trunk_lengths_um = collectSourceTrunkLengthsUm(_clock->get_clock_source(), root_inputs);
    for (const auto& context : sink_domains) {
      if (!synthesizeSinkDomain(context, source_trunk_lengths_um)) {
        return false;
      }
    }
    return synthesizeSourceTrunk(root_inputs);
  }

 private:
  auto commitSinkDomain(const ClockDistributionContext& context, Topology::BuildResult& synthesis_result, std::string& failure_reason)
      -> bool
  {
    auto pending_clock_layout = ClockLayoutBuilder::makeSinkDomainLayout(*_clock, _clock_index, context.makeLayoutTopology(),
                                                                         ClockLayoutAdapter::makeSinkDomainLayoutInput(synthesis_result));
    if (!DesignConversion::commitInsertedObjects(*_clock, synthesis_result.inserted_insts, synthesis_result.inserted_pins,
                                                 synthesis_result.inserted_nets)) {
      DesignConversion::reconnectNet(*context.downstream_net, context.downstream_net->get_driver(), context.sinks);
      failure_reason = "failed to commit inserted synthesis objects";
      Topology::resetClockTopology(*_clock);
      return false;
    }

    ClockLayoutBuilder::merge(*_clock_layout, pending_clock_layout);
    recordSynthesisResult(*_summary, synthesis_result);
    return true;
  }

  auto synthesizeSinkDomain(const ClockDistributionContext& context, const std::vector<double>& source_trunk_lengths_um) -> bool
  {
    const auto* const sink_domain_label = ToString(context.sink_domain);
    if (context.sinks.size() < kMinSynthesisSinkCount) {
      ClockLayoutBuilder::appendDirectSinkDomain(*_clock_layout, *_clock, _clock_index, context.makeLayoutTopology());
      _status_table->append(*_clock, DomainStatus::kFinished, context.sink_domain, _valid_sinks, context.sinks.size(), "direct");
      return true;
    }

    Topology::BuildOptions synthesis_options;
    synthesis_options.object_name_prefix = context.domain_prefix;
    synthesis_options.enable_sink_clustering = CONFIG_INST.is_enable_sink_clustering();
    synthesis_options.characterization_library = _characterization_library;
    synthesis_options.additional_characterization_lengths_um = source_trunk_lengths_um;
    synthesis_options.log_context = makeLogContext(*_clock, sink_domain_label, "downstream_htree", context.domain_prefix);

    std::string failure_reason;
    auto synthesis_result = Topology::build(*context.downstream_net, synthesis_options);
    if (!synthesis_result.success) {
      failure_reason = synthesis_result.failure_reason.empty() ? "sink-domain synthesis failed" : synthesis_result.failure_reason;
    } else {
      (void) commitSinkDomain(context, synthesis_result, failure_reason);
    }

    if (!failure_reason.empty()) {
      _status_table->append(*_clock, DomainStatus::kFailed, context.sink_domain, _valid_sinks, context.sinks.size(), failure_reason);
      LOG_ERROR << "Topology: clock \"" << _clock->get_clock_name() << "\" sink domain " << sink_domain_label
                << " failed: " << failure_reason;
      Topology::resetClockTopology(*_clock);
      return false;
    }

    _status_table->append(*_clock, DomainStatus::kFinished, context.sink_domain, _valid_sinks, context.sinks.size(), "synthesis");
    return true;
  }

  auto synthesizeSourceTrunk(const std::vector<Pin*>& root_inputs) -> bool
  {
    const auto source_trunk_domain = SinkDomainKind::kSourceToRoot;
    const auto* const source_trunk_label = ToString(source_trunk_domain);
    auto* clock_source = _clock->get_clock_source();
    auto* clock_source_net = _clock->get_clock_source_net();
    if (clock_source_net == nullptr && clock_source != nullptr) {
      clock_source_net = clock_source->get_net();
      _clock->set_clock_source_net(clock_source_net);
    }
    if (clock_source == nullptr || clock_source_net == nullptr) {
      _status_table->append(*_clock, DomainStatus::kFailed, source_trunk_domain, _valid_sinks, root_inputs.size(),
                            "missing clock source or source net");
      LOG_ERROR << "Topology: clock \"" << _clock->get_clock_name()
                << "\" source trunk formation failed because the source pin or net is missing.";
      Topology::resetClockTopology(*_clock);
      return false;
    }

    const auto source_trunk_prefix = DesignConversion::makeSinkDomainPrefix(*_clock, _clock_index, source_trunk_domain);
    Topology::SourceTrunkBuildOptions options{
        .object_name_prefix = source_trunk_prefix,
        .characterization_library = _characterization_library,
        .log_context = makeLogContext(*_clock, source_trunk_label, "source_to_root", source_trunk_prefix),
    };
    auto source_trunk_result = Topology::buildSourceTrunk(*clock_source_net, clock_source, root_inputs, options);
    const auto source_trunk_phase = sourceTrunkSynthesisPhase(source_trunk_result.stage);
    if (!source_trunk_result.success) {
      const auto failure_reason
          = source_trunk_result.failure_reason.empty() ? "source trunk formation failed" : source_trunk_result.failure_reason;
      _status_table->append(*_clock, DomainStatus::kFailed, source_trunk_domain, _valid_sinks, root_inputs.size(), failure_reason);
      LOG_ERROR << "Topology: clock \"" << _clock->get_clock_name() << "\" source trunk formation failed: " << failure_reason;
      Topology::resetClockTopology(*_clock);
      return false;
    }

    auto pending_clock_layout = ClockLayoutBuilder::makeSourceToRootLayout(
        *_clock, _clock_index, *clock_source_net, ClockLayoutAdapter::makeSourceTrunkLayoutInput(source_trunk_result, source_trunk_phase),
        source_trunk_phase);
    if (!DesignConversion::commitInsertedObjects(*_clock, source_trunk_result.inserted_insts, source_trunk_result.inserted_pins,
                                                 source_trunk_result.inserted_nets)) {
      _status_table->append(*_clock, DomainStatus::kFailed, source_trunk_domain, _valid_sinks, root_inputs.size(),
                            "failed to commit source trunk objects");
      LOG_ERROR << "Topology: clock \"" << _clock->get_clock_name()
                << "\" source trunk formation failed while committing inserted objects.";
      Topology::resetClockTopology(*_clock);
      return false;
    }

    ClockLayoutBuilder::merge(*_clock_layout, pending_clock_layout);
    recordSourceTrunkResult(*_summary, source_trunk_result);
    _status_table->append(*_clock, DomainStatus::kFinished, source_trunk_domain, _valid_sinks, root_inputs.size(),
                          ToString(source_trunk_result.stage));
    return true;
  }

  Clock* _clock = nullptr;
  std::size_t _clock_index = 0U;
  ClockLayout* _clock_layout = nullptr;
  SynthesisTraceSummary* _summary = nullptr;
  DomainStatusTable* _status_table = nullptr;
  CharacterizationLibrary* _characterization_library = nullptr;
  std::size_t _valid_sinks = 0U;
};

}  // namespace

auto Topology::build(Net& root_net) -> BuildResult
{
  return build(root_net, BuildOptions{});
}

auto Topology::build(Net& root_net, const BuildOptions& options) -> BuildResult
{
  return topology::BuildSinkTree(root_net, options);
}

auto Topology::buildSourceTrunk(Net& source_net, Pin* clock_source, const std::vector<Pin*>& root_inputs,
                                const SourceTrunkBuildOptions& options) -> SourceTrunkBuildResult
{
  return topology::BuildSourceTrunkTree(source_net, clock_source, root_inputs, options);
}

auto Topology::resetClockTopology(Clock& clock) -> void
{
  DesignConversion::restoreClockSourceNetToClockLoads(clock);
  clearClockCtsMembership(clock);
}

auto Topology::formClock(Clock& clock, std::size_t clock_index, ClockLayout& clock_layout, SynthesisTraceSummary& summary,
                         DomainStatusTable& status_table, CharacterizationLibrary& characterization_library, std::size_t valid_sinks,
                         const std::vector<ClockDistributionContext>& sink_domains) -> bool
{
  ClockTopologyFormation formation(clock, clock_index, clock_layout, summary, status_table, characterization_library, valid_sinks);
  return formation.form(sink_domains);
}

auto ToString(Topology::SourceTrunkStage stage) -> const char*
{
  switch (stage) {
    case Topology::SourceTrunkStage::kSegment:
      return "top_segment";
    case Topology::SourceTrunkStage::kHTree:
      return "top_htree";
    case Topology::SourceTrunkStage::kUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace icts
