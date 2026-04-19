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
 * @file LinearClusteringSyntheticRuntime.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Runtime and reporting helpers for synthetic linear clustering tests.
 */

#include <algorithm>
#include <compare>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "TopologyConfig.hh"
#include "common/io/TestArtifactIO.hh"
#include "module/topology/linear_clustering/SequenceSplitter.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts_test::linear_clustering::synthetic::detail {
namespace {

auto UsesDiscreteHilbertOrder(icts::LinearOrderStrategy strategy) -> bool
{
  return strategy == icts::LinearOrderStrategy::kDiscreteHilbert || strategy == icts::LinearOrderStrategy::kDensityScaledDiscreteHilbert;
}

auto CsvEscape(const std::string& value) -> std::string
{
  bool needs_quotes = false;
  std::string escaped;
  escaped.reserve(value.size() + 2U);
  for (const char character : value) {
    if (character == ',' || character == '"' || character == '\n') {
      needs_quotes = true;
    }
    escaped.push_back(character);
    if (character == '"') {
      escaped.push_back('"');
    }
  }
  if (!needs_quotes) {
    return escaped;
  }
  return "\"" + escaped + "\"";
}

auto CsvBool(bool value) -> const char*
{
  return value ? "true" : "false";
}

auto AppendCsvCell(std::ostringstream& output_stream, const std::string& value, bool* first_cell) -> void
{
  if (!*first_cell) {
    output_stream << ",";
  }
  output_stream << CsvEscape(value);
  *first_cell = false;
}

auto AppendCsvCell(std::ostringstream& output_stream, const char* value, bool* first_cell) -> void
{
  AppendCsvCell(output_stream, std::string(value), first_cell);
}

auto AppendCsvCell(std::ostringstream& output_stream, std::size_t value, bool* first_cell) -> void
{
  if (!*first_cell) {
    output_stream << ",";
  }
  output_stream << value;
  *first_cell = false;
}

auto AppendCsvCell(std::ostringstream& output_stream, int value, bool* first_cell) -> void
{
  if (!*first_cell) {
    output_stream << ",";
  }
  output_stream << value;
  *first_cell = false;
}

auto AppendCsvCell(std::ostringstream& output_stream, double value, bool* first_cell) -> void
{
  if (!*first_cell) {
    output_stream << ",";
  }
  output_stream << value;
  *first_cell = false;
}

}  // namespace

auto PrepareSyntheticOutputDir(const std::string& case_name) -> std::filesystem::path
{
  static const bool cleaned_legacy_artifacts = []() -> bool {
    const auto root_dir = common::io::ResolveLinearClusteringOutputDir();
    std::error_code error_code;
    if (!std::filesystem::exists(root_dir, error_code)) {
      return true;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root_dir, error_code)) {
      if (error_code) {
        return false;
      }
      if (entry.is_regular_file()) {
        std::filesystem::remove(entry.path(), error_code);
        if (error_code) {
          return false;
        }
      }
    }
    return true;
  }();
  (void) cleaned_legacy_artifacts;

  return common::io::PrepareCleanOutputDir(common::io::ResolveLinearClusteringOutputDir() / "synthetic"
                                           / common::io::SanitizeOutputName(case_name));
}

auto FormatMetricsLine(const ClusterMetrics& metrics) -> std::string
{
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream << std::setprecision(2);
  stream << "clusters=" << metrics.cluster_count << ", singleton_clusters=" << metrics.singleton_cluster_count
         << ", cluster_size[min/max/avg]=" << metrics.min_cluster_size << "/" << metrics.max_cluster_size << "/" << metrics.avg_cluster_size
         << ", cluster_diameter[min/max]=" << metrics.min_cluster_diameter << "/" << metrics.max_cluster_diameter;
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

auto BuildConstraintObservationReport(const std::string& test_name, const std::string& input_summary,
                                      const std::vector<LadderObservation>& observations) -> std::string
{
  std::ostringstream report;
  report.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report << std::setprecision(2);
  report << "Test: " << test_name << "\n";
  report << "Mode: synthetic constraint regression\n";
  report << "Input: " << input_summary << "\n";
  report << "Observation count: " << observations.size() << "\n\n";
  report << "Observed ladder response\n";
  for (const auto& observation : observations) {
    report << "- " << observation.label << ": fanout_limit=" << observation.max_fanout << ", max_diameter=" << observation.max_diameter
           << ", empty_result=" << (observation.empty_result ? "true" : "false");
    if (!observation.empty_result) {
      report << ", " << FormatMetricsLine(observation.metrics);
    }
    report << "\n";
  }
  report << "\nArtifacts: report.log\n";
  return report.str();
}

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

auto DiscreteHilbertEncodingName(icts::DiscreteHilbertEncoding encoding) -> const char*
{
  switch (encoding) {
    case icts::DiscreteHilbertEncoding::kSinkThetaCell:
      return "sink_theta_cell";
    case icts::DiscreteHilbertEncoding::kSinkThetaCellTangent:
      return "sink_theta_cell_tangent";
    case icts::DiscreteHilbertEncoding::kClassicIndex:
      return "classic_index";
    case icts::DiscreteHilbertEncoding::kClassicIndexTangent:
      return "classic_index_tangent";
  }
  return "unknown";
}

auto HilbertTransformName(icts::HilbertTransform transform) -> const char*
{
  switch (transform) {
    case icts::HilbertTransform::kIdentity:
      return "identity";
    case icts::HilbertTransform::kMirrorX:
      return "mirror_x";
    case icts::HilbertTransform::kMirrorY:
      return "mirror_y";
    case icts::HilbertTransform::kMirrorXY:
      return "mirror_xy";
    case icts::HilbertTransform::kSwapXY:
      return "swap_xy";
    case icts::HilbertTransform::kSwapMirrorX:
      return "swap_mirror_x";
    case icts::HilbertTransform::kSwapMirrorY:
      return "swap_mirror_y";
    case icts::HilbertTransform::kSwapMirrorXY:
      return "swap_mirror_xy";
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

auto MakeStrategyLabel(icts::LinearOrderStrategy order_strategy, icts::DiscreteHilbertEncoding discrete_hilbert_encoding,
                       icts::HilbertTransform hilbert_transform, int order_bits, icts::LinearSplitStrategy split_strategy,
                       icts::LinearSweepMode sweep_mode, std::size_t strided_sweep_count) -> std::string
{
  auto label = std::string(OrderStrategyName(order_strategy));
  if (UsesDiscreteHilbertOrder(order_strategy)) {
    label += "__";
    label += DiscreteHilbertEncodingName(discrete_hilbert_encoding);
    label += "__";
    label += HilbertTransformName(hilbert_transform);
    label += "__bits_";
    label += std::to_string(order_bits);
  }
  label += "__";
  label += SplitStrategyName(split_strategy);
  label += "__";
  label += MakeSweepLabel(sweep_mode, strided_sweep_count);
  return label;
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

auto PickBestStrategy(const std::vector<StrategySweepObservation>& observations) -> std::optional<std::size_t>
{
  std::optional<std::size_t> best_index = std::nullopt;
  for (std::size_t index = 0; index < observations.size(); ++index) {
    const auto& candidate = observations.at(index);
    if (!candidate.legal || candidate.empty_result) {
      continue;
    }
    if (!best_index.has_value()) {
      best_index = index;
      continue;
    }
    const auto& current_best = observations.at(best_index.value());
    if ((candidate.selection_score <=> current_best.selection_score) == std::partial_ordering::less) {
      best_index = index;
      continue;
    }
    if (candidate.selection_score == current_best.selection_score) {
      const auto candidate_label
          = MakeStrategyLabel(candidate.order_strategy, candidate.discrete_hilbert_encoding, candidate.hilbert_transform,
                              candidate.order_bits, candidate.split_strategy, candidate.sweep_mode, candidate.strided_sweep_count);
      const auto best_label = MakeStrategyLabel(current_best.order_strategy, current_best.discrete_hilbert_encoding,
                                                current_best.hilbert_transform, current_best.order_bits, current_best.split_strategy,
                                                current_best.sweep_mode, current_best.strided_sweep_count);
      if (candidate_label < best_label) {
        best_index = index;
      }
    }
  }
  return best_index;
}

auto BuildStrategySweepReport(const std::string& test_name, const std::string& input_summary,
                              const std::vector<StrategySweepObservation>& observations, std::optional<std::size_t> selected_index,
                              const std::vector<std::string>& artifact_names) -> std::string
{
  std::ostringstream output_stream;
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(2);
  output_stream << "Test: " << test_name << "\n";
  output_stream << "Mode: synthetic strategy-space regression\n";
  output_stream << "Input: " << input_summary << "\n";
  output_stream << "Selection rule: choose the legal, non-empty candidate with the smallest actual partition score.\n";
  output_stream << "Selection score: partition.total_score computed by LinearOrderGenerator + SequenceSplitter under the active config.\n";
  output_stream << "Tie break: lexicographic(strategy_label).\n";
  output_stream << "Sweep semantics: prefix_sweep uses Sink-style offsets [0..prefix_count-1]; strided_sweep samples the full ring; "
                   "prefix_and_strided_sweep expands each strided anchor into a prefix-length sequential window and normalizes to the "
                   "full ring when those anchor windows cover every rotation.\n";
  output_stream << "Strategy space count: " << observations.size() << "\n\n";
  output_stream << "Strategy space\n";
  for (std::size_t index = 0; index < observations.size(); ++index) {
    const auto& candidate = observations.at(index);
    output_stream << "- candidate[" << index << "] order=" << OrderStrategyName(candidate.order_strategy) << ", discrete_encoding="
                  << (UsesDiscreteHilbertOrder(candidate.order_strategy) ? DiscreteHilbertEncodingName(candidate.discrete_hilbert_encoding)
                                                                         : "-")
                  << ", hilbert_transform="
                  << (UsesDiscreteHilbertOrder(candidate.order_strategy) ? HilbertTransformName(candidate.hilbert_transform) : "-")
                  << ", order_bits=" << (UsesDiscreteHilbertOrder(candidate.order_strategy) ? candidate.order_bits : 0)
                  << ", split=" << SplitStrategyName(candidate.split_strategy) << ", sweep_mode=" << SweepModeName(candidate.sweep_mode)
                  << ", strided_sweep_count=" << candidate.strided_sweep_count
                  << ", effective_sweep_mode=" << SweepModeName(candidate.effective_sweep_mode)
                  << ", prefix_count=" << candidate.prefix_count << ", resolved_strided_count=" << candidate.resolved_strided_count
                  << ", degraded_to_prefix=" << CsvBool(candidate.degraded_to_prefix)
                  << ", selected_rotation_offset=" << candidate.selected_rotation_offset
                  << ", empty=" << (candidate.empty_result ? "true" : "false") << ", legal=" << (candidate.legal ? "true" : "false")
                  << ", selection_score=" << candidate.selection_score << ", partition_score=" << candidate.partition_score
                  << ", cluster_score[min/max/avg]=" << candidate.min_cluster_score << "/" << candidate.max_cluster_score << "/"
                  << candidate.avg_cluster_score << ", " << FormatMetricsLine(candidate.metrics)
                  << ", note=" << (candidate.note.empty() ? "-" : candidate.note) << "\n";
  }

  if (selected_index.has_value() && selected_index.value() < observations.size()) {
    const auto& selected = observations.at(selected_index.value());
    output_stream << "\nSelected strategy\n";
    output_stream << "- candidate_index=" << selected_index.value() << "\n";
    output_stream << "- strategy="
                  << MakeStrategyLabel(selected.order_strategy, selected.discrete_hilbert_encoding, selected.hilbert_transform,
                                       selected.order_bits, selected.split_strategy, selected.sweep_mode, selected.strided_sweep_count)
                  << "\n";
    output_stream << "- selection_score=" << selected.selection_score << "\n";
    output_stream << "- selected_rotation_offset=" << selected.selected_rotation_offset << "\n";
    output_stream << "- result=" << FormatMetricsLine(selected.metrics) << "\n";
  } else {
    output_stream << "\nSelected strategy\n";
    output_stream << "- none\n";
  }
  output_stream << "\nArtifacts: " << BuildArtifactSummary(artifact_names) << ", report.log\n";
  return output_stream.str();
}

auto BuildStrategySweepCsv(const std::vector<StrategySweepObservation>& observations, std::optional<std::size_t> selected_index)
    -> std::string
{
  std::ostringstream output_stream;
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(2);
  output_stream
      << "candidate_index,is_selected,strategy_label,order_strategy,split_strategy,sweep_mode,effective_sweep_mode,prefix_count,"
         "discrete_hilbert_encoding,hilbert_transform,order_bits,configured_strided_sweep_count,resolved_strided_count,"
         "degraded_to_prefix,resolved_offsets_count,resolved_offsets,"
         "selected_rotation_offset,empty,legal,cluster_count,singleton_cluster_count,min_cluster_size,max_cluster_size,avg_cluster_size,"
         "min_cluster_diameter,max_cluster_diameter,selection_score,partition_score,min_cluster_score,max_cluster_score,"
         "avg_cluster_score,note\n";

  for (std::size_t index = 0; index < observations.size(); ++index) {
    const auto& candidate = observations.at(index);
    bool first_cell = true;
    AppendCsvCell(output_stream, index, &first_cell);
    AppendCsvCell(output_stream, CsvBool(selected_index.has_value() && selected_index.value() == index), &first_cell);
    AppendCsvCell(output_stream,
                  MakeStrategyLabel(candidate.order_strategy, candidate.discrete_hilbert_encoding, candidate.hilbert_transform,
                                    candidate.order_bits, candidate.split_strategy, candidate.sweep_mode, candidate.strided_sweep_count),
                  &first_cell);
    AppendCsvCell(output_stream, OrderStrategyName(candidate.order_strategy), &first_cell);
    AppendCsvCell(output_stream, SplitStrategyName(candidate.split_strategy), &first_cell);
    AppendCsvCell(output_stream, SweepModeName(candidate.sweep_mode), &first_cell);
    AppendCsvCell(output_stream, SweepModeName(candidate.effective_sweep_mode), &first_cell);
    AppendCsvCell(output_stream, candidate.prefix_count, &first_cell);
    AppendCsvCell(
        output_stream,
        UsesDiscreteHilbertOrder(candidate.order_strategy) ? DiscreteHilbertEncodingName(candidate.discrete_hilbert_encoding) : "-",
        &first_cell);
    AppendCsvCell(output_stream,
                  UsesDiscreteHilbertOrder(candidate.order_strategy) ? HilbertTransformName(candidate.hilbert_transform) : "-",
                  &first_cell);
    AppendCsvCell(output_stream, UsesDiscreteHilbertOrder(candidate.order_strategy) ? candidate.order_bits : 0, &first_cell);
    AppendCsvCell(output_stream, candidate.strided_sweep_count, &first_cell);
    AppendCsvCell(output_stream, candidate.resolved_strided_count, &first_cell);
    AppendCsvCell(output_stream, CsvBool(candidate.degraded_to_prefix), &first_cell);
    AppendCsvCell(output_stream, candidate.resolved_offsets.size(), &first_cell);
    AppendCsvCell(output_stream, FormatResolvedOffsets(candidate.resolved_offsets), &first_cell);
    AppendCsvCell(output_stream, candidate.selected_rotation_offset, &first_cell);
    AppendCsvCell(output_stream, CsvBool(candidate.empty_result), &first_cell);
    AppendCsvCell(output_stream, CsvBool(candidate.legal), &first_cell);
    AppendCsvCell(output_stream, candidate.metrics.cluster_count, &first_cell);
    AppendCsvCell(output_stream, candidate.metrics.singleton_cluster_count, &first_cell);
    AppendCsvCell(output_stream, candidate.metrics.min_cluster_size, &first_cell);
    AppendCsvCell(output_stream, candidate.metrics.max_cluster_size, &first_cell);
    AppendCsvCell(output_stream, candidate.metrics.avg_cluster_size, &first_cell);
    AppendCsvCell(output_stream, candidate.metrics.min_cluster_diameter, &first_cell);
    AppendCsvCell(output_stream, candidate.metrics.max_cluster_diameter, &first_cell);
    AppendCsvCell(output_stream, candidate.selection_score, &first_cell);
    AppendCsvCell(output_stream, candidate.partition_score, &first_cell);
    AppendCsvCell(output_stream, candidate.min_cluster_score, &first_cell);
    AppendCsvCell(output_stream, candidate.max_cluster_score, &first_cell);
    AppendCsvCell(output_stream, candidate.avg_cluster_score, &first_cell);
    AppendCsvCell(output_stream, candidate.note, &first_cell);
    output_stream << "\n";
  }
  return output_stream.str();
}

}  // namespace icts_test::linear_clustering::synthetic::detail
