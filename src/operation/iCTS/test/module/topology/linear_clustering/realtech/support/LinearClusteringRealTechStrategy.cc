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
 * @file LinearClusteringRealTechStrategy.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Strategy-sweep helpers for real-tech linear clustering tests.
 */

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "clustering/Clustering.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/SequenceSplitter.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts_test::linear_clustering::realtech::detail {

namespace {

struct StrategySweepSpecInput
{
  std::size_t load_count = 0;
  bool enable_exact_cap = false;
  int max_diameter = 0;
  std::size_t fanout_limit = 0;
};

auto EmitStrategySweepIntro(std::ostringstream& output_stream, icts::LinearScoringStrategy scoring_strategy, std::size_t candidate_count)
    -> void
{
  output_stream << "Selection rule: choose the legal, non-empty candidate with the smallest actual partition score.\n";
  output_stream << "Selection score: partition.total_score computed by LinearOrderGenerator + SequenceSplitter.\n";
  output_stream << "Scoring strategy: " << ScoringStrategyName(scoring_strategy) << ".\n";
  output_stream << "Tie break: lexicographic(order, split, sweep_mode).\n";
  output_stream << "Sweep semantics: prefix_sweep uses Sink-style offsets [0..prefix_count-1]; strided_sweep samples the full ring; "
                   "prefix_and_strided_sweep expands each strided anchor into a prefix-length sequential window and normalizes to the "
                   "full ring when those anchor windows cover every rotation.\n";
  output_stream << "Strategy space count: " << candidate_count << "\n";
}

auto EmitStrategySweepCandidates(std::ostringstream& output_stream, const std::vector<StrategySweepCandidate>& candidates) -> void
{
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto& candidate = candidates.at(index);
    output_stream << "- candidate[" << index << "] order=" << OrderStrategyName(candidate.order_strategy)
                  << ", split=" << SplitStrategyName(candidate.split_strategy) << ", sweep_mode=" << SweepModeName(candidate.sweep_mode)
                  << ", strided_sweep_count=" << candidate.strided_sweep_count << ", empty=" << (candidate.empty_result ? "true" : "false")
                  << ", legal=" << (candidate.legal ? "true" : "false") << ", selection_score=" << candidate.selection_score << ", "
                  << FormatMetricsLine(candidate.metrics) << ", note=" << (candidate.note.empty() ? "-" : candidate.note) << "\n";
  }
}

auto EmitSelectedStrategySweepCandidate(std::ostringstream& output_stream, const std::vector<StrategySweepCandidate>& candidates,
                                        std::optional<std::size_t> selected_index) -> void
{
  if (!selected_index.has_value() || selected_index.value() >= candidates.size()) {
    output_stream << "Selected candidate: none\n";
    return;
  }

  const auto& selected = candidates.at(selected_index.value());
  output_stream << "Selected candidate: index=" << selected_index.value() << ", strategy="
                << StrategyLabel(selected.order_strategy, selected.split_strategy, selected.sweep_mode, selected.strided_sweep_count)
                << ", selection_score=" << selected.selection_score << "\n";
}

auto BuildStrategySweepSpecs(const StrategySweepSpecInput& input) -> std::vector<FanoutConfigSpec>
{
  const auto sweep_candidates = BuildSweepCandidates(input.load_count);
  std::vector<FanoutConfigSpec> specs;
  specs.reserve(kStrategyOrderSweep.size() * kStrategySplitSweep.size() * sweep_candidates.size());

  for (const auto order_strategy : kStrategyOrderSweep) {
    for (const auto split_strategy : kStrategySplitSweep) {
      for (const auto& sweep_candidate : sweep_candidates) {
        specs.push_back(FanoutConfigSpec{
            .fanout_limit = input.fanout_limit,
            .max_diameter = input.max_diameter,
            .enable_exact_cap = input.enable_exact_cap,
            .order_strategy = order_strategy,
            .split_strategy = split_strategy,
            .sweep_mode = sweep_candidate.sweep_mode,
            .strided_sweep_count = sweep_candidate.strided_sweep_count,
        });
      }
    }
  }
  return specs;
}

}  // namespace

auto OrderStrategyName(icts::LinearOrderStrategy strategy) -> const char*
{
  switch (strategy) {
    case icts::LinearOrderStrategy::kContinuousHilbert:
      return "continuous_hilbert";
    case icts::LinearOrderStrategy::kDiscreteHilbert:
      return "discrete_hilbert";
    case icts::LinearOrderStrategy::kDensityScaledContinuousHilbert:
      return "density_scaled_continuous_hilbert";
    case icts::LinearOrderStrategy::kDensityScaledDiscreteHilbert:
      return "density_scaled_discrete_hilbert";
  }
  return "unknown";
}

auto SplitStrategyName(icts::LinearSplitStrategy strategy) -> const char*
{
  switch (strategy) {
    case icts::LinearSplitStrategy::kForwardGreedy:
      return "forward_greedy";
    case icts::LinearSplitStrategy::kReverseGreedy:
      return "reverse_greedy";
    case icts::LinearSplitStrategy::kBidirectionalGreedy:
      return "bidirectional_greedy";
  }
  return "unknown";
}

auto SweepModeName(icts::LinearSweepMode mode) -> const char*
{
  switch (mode) {
    case icts::LinearSweepMode::kPrefixSweep:
      return "prefix_sweep";
    case icts::LinearSweepMode::kStridedSweep:
      return "strided_sweep";
    case icts::LinearSweepMode::kPrefixAndStridedSweep:
      return "prefix_and_strided_sweep";
  }
  return "unknown";
}

auto MakeSweepLabel(icts::LinearSweepMode sweep_mode, std::size_t strided_sweep_count) -> std::string
{
  auto label = std::string(SweepModeName(sweep_mode));
  if (sweep_mode != icts::LinearSweepMode::kPrefixSweep) {
    label += "__strided_count_" + std::to_string(strided_sweep_count);
  }
  return label;
}

auto StrategyLabel(icts::LinearOrderStrategy order_strategy, icts::LinearSplitStrategy split_strategy, icts::LinearSweepMode sweep_mode,
                   std::size_t strided_sweep_count) -> std::string
{
  return std::string(OrderStrategyName(order_strategy)) + "__" + SplitStrategyName(split_strategy) + "__"
         + MakeSweepLabel(sweep_mode, strided_sweep_count);
}

auto ScoringStrategyName(icts::LinearScoringStrategy strategy) -> const char*
{
  switch (strategy) {
    case icts::LinearScoringStrategy::kMaxDiameter:
      return "max_diameter";
    case icts::LinearScoringStrategy::kTotalWirelength:
      return "total_wirelength";
  }
  return "unknown";
}

auto FormatResolvedOffsets(const std::vector<std::size_t>& offsets) -> std::string
{
  std::ostringstream stream;
  stream << "[";
  for (std::size_t index = 0; index < offsets.size(); ++index) {
    if (index > 0U) {
      stream << ",";
    }
    stream << offsets.at(index);
  }
  stream << "]";
  return stream.str();
}

auto BuildSweepCandidates(std::size_t load_count) -> std::vector<SweepConfigSpec>
{
  std::vector<SweepConfigSpec> candidates = {
      {.sweep_mode = icts::LinearSweepMode::kPrefixSweep, .strided_sweep_count = 0U},
  };
  if (load_count > 1U) {
    const auto strided_sweep_count = std::min(load_count, std::max<std::size_t>(std::size_t{2}, kDefaultStridedSweepCount));
    candidates.push_back({.sweep_mode = icts::LinearSweepMode::kStridedSweep, .strided_sweep_count = strided_sweep_count});
    candidates.push_back({.sweep_mode = icts::LinearSweepMode::kPrefixAndStridedSweep, .strided_sweep_count = strided_sweep_count});
  }
  return candidates;
}

auto BuildSweepNote(std::size_t load_count, const icts::LinearClusteringConfig& config) -> std::string
{
  const auto sweep_resolution = icts::SequenceSplitter::resolveSweepOffsets(load_count, config);
  std::ostringstream stream;
  stream << "sweep_mode=" << SweepModeName(sweep_resolution.requested_mode)
         << "; effective_sweep_mode=" << SweepModeName(sweep_resolution.effective_mode)
         << "; prefix_count=" << sweep_resolution.prefix_count << "; strided_count=" << sweep_resolution.strided_count
         << "; degraded_to_prefix=" << (sweep_resolution.degraded_to_prefix ? "true" : "false")
         << "; resolved_offsets=" << FormatResolvedOffsets(sweep_resolution.offsets)
         << "; resolved_count=" << sweep_resolution.offsets.size();
  return stream.str();
}

auto FormatSweepCandidates(const std::vector<SweepConfigSpec>& candidates) -> std::string
{
  std::ostringstream stream;
  stream << "[";
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (index > 0U) {
      stream << ",";
    }
    stream << MakeSweepLabel(candidates.at(index).sweep_mode, candidates.at(index).strided_sweep_count);
  }
  stream << "]";
  return stream.str();
}

auto AppendCandidateNote(const std::string& base_note, const std::string& extra_note) -> std::string
{
  if (base_note.empty()) {
    return extra_note;
  }
  if (extra_note.empty()) {
    return base_note;
  }
  return base_note + "; " + extra_note;
}

auto PickBestStrategyCandidate(const std::vector<StrategySweepCandidate>& candidates) -> std::optional<std::size_t>
{
  std::optional<std::size_t> best_index = std::nullopt;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto& candidate = candidates.at(index);
    if (!candidate.legal || candidate.empty_result) {
      continue;
    }
    if (!best_index.has_value()) {
      best_index = index;
      continue;
    }
    const auto& current_best = candidates.at(best_index.value());
    if ((candidate.selection_score <=> current_best.selection_score) == std::partial_ordering::less) {
      best_index = index;
      continue;
    }
    if (candidate.selection_score == current_best.selection_score) {
      const auto candidate_label
          = StrategyLabel(candidate.order_strategy, candidate.split_strategy, candidate.sweep_mode, candidate.strided_sweep_count);
      const auto best_label = StrategyLabel(current_best.order_strategy, current_best.split_strategy, current_best.sweep_mode,
                                            current_best.strided_sweep_count);
      if (candidate_label < best_label) {
        best_index = index;
      }
    }
  }
  return best_index;
}

auto FormatMetricsLine(const ClusterMetrics& metrics) -> std::string
{
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream << std::setprecision(2);
  stream << "clusters=" << metrics.cluster_count << ", singleton_clusters=" << metrics.singleton_cluster_count
         << ", cluster_size[min/max/avg]=" << metrics.min_cluster_size << "/" << metrics.max_cluster_size << "/" << metrics.avg_cluster_size
         << ", max_cluster_diameter=" << metrics.max_cluster_diameter;
  return stream.str();
}

auto BuildArtifactSummary(const std::vector<std::string>& artifact_names) -> std::string
{
  std::ostringstream stream;
  for (std::size_t index = 0; index < artifact_names.size(); ++index) {
    if (index > 0U) {
      stream << ", ";
    }
    stream << artifact_names.at(index);
  }
  return artifact_names.empty() ? std::string("none") : stream.str();
}

auto BuildStrategySweepSection(const std::vector<StrategySweepCandidate>& candidates, std::optional<std::size_t> selected_index,
                               icts::LinearScoringStrategy scoring_strategy) -> std::string
{
  std::ostringstream output_stream;
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(2);
  EmitStrategySweepIntro(output_stream, scoring_strategy, candidates.size());
  EmitStrategySweepCandidates(output_stream, candidates);
  EmitSelectedStrategySweepCandidate(output_stream, candidates, selected_index);
  return output_stream.str();
}

auto BuildRealTechFanoutConfig(const FanoutConfigSpec& spec) -> icts::LinearClusteringConfig
{
  icts::LinearClusteringConfig config{};
  config.router_kind = icts::LinearRouterKind::kFlute;
  config.max_fanout = spec.fanout_limit;
  config.max_diameter = std::max(1, spec.max_diameter);
  config.max_cap = std::numeric_limits<double>::infinity();
  config.enable_exact_cap = spec.enable_exact_cap;
  config.always_build_exact_cap = spec.enable_exact_cap;
  config.order_strategy = spec.order_strategy;
  config.split_strategy = spec.split_strategy;
  config.sweep_mode = spec.sweep_mode;
  config.strided_sweep_count = spec.strided_sweep_count;
  return config;
}

auto MakeFanoutCaseTag(std::size_t fanout_limit, std::size_t load_count) -> std::string
{
  return "fanout_" + std::to_string(fanout_limit) + "_loads_" + std::to_string(load_count);
}

auto EvaluateStrategySweepCandidate(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config,
                                    StrategySweepCandidate& candidate) -> std::optional<DetailedLinearClusteringRun>
{
  auto run = RunDetailedLinearClustering(loads, config);
  const auto& result = run.result;
  if (result.clusters.empty()) {
    candidate.empty_result = true;
    const auto* const empty_reason = run.partition.legal ? "status=empty_result" : "status=no_legal_partition";
    candidate.note = AppendCandidateNote(candidate.note, empty_reason);
    return std::nullopt;
  }

  std::string legality_error;
  if (!ValidateClusterLegality(result, config, legality_error)) {
    candidate.note = AppendCandidateNote(candidate.note, "status=illegal; legality_error=" + legality_error);
    return std::nullopt;
  }

  candidate.legal = true;
  candidate.metrics = GatherMetrics(result);
  candidate.selection_score = run.partition.total_score;
  candidate.note = AppendCandidateNote(candidate.note, "rotation_offset=" + std::to_string(run.partition.rotation_offset));
  return run;
}

auto BuildStrategySweepSelection(const std::vector<icts::Pin*>& loads, std::size_t fanout_limit, int max_diameter, bool enable_exact_cap)
    -> StrategySweepSelection
{
  StrategySweepSelection selection;
  const auto specs = BuildStrategySweepSpecs(StrategySweepSpecInput{
      .load_count = loads.size(),
      .enable_exact_cap = enable_exact_cap,
      .max_diameter = max_diameter,
      .fanout_limit = fanout_limit,
  });
  selection.candidates.reserve(specs.size());

  std::vector<DetailedLinearClusteringRun> legal_runs;
  std::vector<icts::LinearClusteringConfig> legal_configs;
  legal_runs.reserve(selection.candidates.capacity());
  legal_configs.reserve(selection.candidates.capacity());

  for (const auto& spec : specs) {
    const auto config = BuildRealTechFanoutConfig(spec);

    StrategySweepCandidate candidate;
    candidate.order_strategy = spec.order_strategy;
    candidate.split_strategy = spec.split_strategy;
    candidate.sweep_mode = spec.sweep_mode;
    candidate.strided_sweep_count = spec.strided_sweep_count;
    candidate.note = BuildSweepNote(loads.size(), config);

    const auto run = EvaluateStrategySweepCandidate(loads, config, candidate);
    selection.candidates.push_back(candidate);
    if (run.has_value()) {
      legal_runs.push_back(*run);
      legal_configs.push_back(config);
    }
  }

  selection.selected_index = PickBestStrategyCandidate(selection.candidates);
  if (!selection.selected_index.has_value()) {
    return selection;
  }

  std::size_t legal_cursor = 0;
  for (std::size_t index = 0; index < selection.candidates.size(); ++index) {
    const auto& candidate = selection.candidates.at(index);
    if (!candidate.legal || candidate.empty_result) {
      continue;
    }
    if (index == selection.selected_index.value()) {
      selection.selected_run = legal_runs.at(legal_cursor);
      selection.selected_config = legal_configs.at(legal_cursor);
      return selection;
    }
    ++legal_cursor;
  }
  selection.selected_index = std::nullopt;
  return selection;
}

}  // namespace icts_test::linear_clustering::realtech::detail
