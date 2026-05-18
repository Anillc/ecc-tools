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
 * @file AnalyticalCharacterization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Analytical characterization catalog builder implementation.
 */

#include "AnalyticalCharacterization.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AnalyticalFit.hh"
#include "BufferingPattern.hh"
#include "CharBuilder.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"
#include "adapter/sta/STAAdapter.hh"

namespace icts::analytical {
namespace {

struct GroupedSegmentChars
{
  AnalyticalModelKey key;
  std::vector<SegmentChar> chars;
};

auto FindPattern(PatternId pattern_id, const std::vector<BufferingPattern>& buffering_patterns) -> const BufferingPattern*
{
  const auto it = std::ranges::find_if(buffering_patterns,
                                       [&](const BufferingPattern& pattern) -> bool { return pattern.get_pattern_id() == pattern_id; });
  return it == buffering_patterns.end() ? nullptr : &*it;
}

auto GroupSegmentChars(const std::vector<SegmentChar>& segment_chars) -> std::vector<GroupedSegmentChars>
{
  std::unordered_map<AnalyticalModelKey, std::vector<SegmentChar>, AnalyticalModelKeyHash> grouped;
  std::vector<AnalyticalModelKey> stable_keys;
  for (const auto& segment_char : segment_chars) {
    AnalyticalModelKey key{.pattern_id = segment_char.get_pattern_id(), .length_idx = segment_char.get_length_idx()};
    if (!grouped.contains(key)) {
      stable_keys.push_back(key);
    }
    grouped[key].push_back(segment_char);
  }

  std::ranges::sort(stable_keys, [](const AnalyticalModelKey& lhs, const AnalyticalModelKey& rhs) -> bool {
    if (lhs.length_idx != rhs.length_idx) {
      return lhs.length_idx < rhs.length_idx;
    }
    return lhs.pattern_id.pack() < rhs.pattern_id.pack();
  });

  std::vector<GroupedSegmentChars> result;
  result.reserve(stable_keys.size());
  for (const auto& key : stable_keys) {
    result.push_back(GroupedSegmentChars{.key = key, .chars = std::move(grouped[key])});
  }
  return result;
}

auto BuildSamples(const std::vector<SegmentChar>& chars, const UniformValueLattice& slew_lattice, const UniformValueLattice& cap_lattice,
                  AnalyticalMetric metric) -> std::vector<AnalyticalFitSample>
{
  std::vector<AnalyticalFitSample> samples;
  samples.reserve(chars.size());
  for (const auto& segment_char : chars) {
    double value = 0.0;
    switch (metric) {
      case AnalyticalMetric::kOutputSlew:
        value = slew_lattice.valueForIndex(segment_char.get_output_slew_idx());
        break;
      case AnalyticalMetric::kDelay:
        value = segment_char.get_delay();
        break;
      case AnalyticalMetric::kPower:
        value = segment_char.get_power();
        break;
      case AnalyticalMetric::kSourceBoundaryNetSwitchPower:
        value = segment_char.get_source_boundary_net_switch_power();
        break;
    }
    samples.push_back(AnalyticalFitSample{
        .input_slew_ns = slew_lattice.valueForIndex(segment_char.get_input_slew_idx()),
        .load_cap_pf = cap_lattice.valueForIndex(segment_char.get_load_cap_idx()),
        .value = value,
    });
  }
  return samples;
}

auto MakeFitOptions(AnalyticalMetric metric, const AnalyticalCharacterizationOptions& options) -> AnalyticalFitOptions
{
  AnalyticalFitOptions fit_options;
  fit_options.metric = metric;
  fit_options.min_r2 = options.min_r2;
  switch (metric) {
    case AnalyticalMetric::kOutputSlew:
      fit_options.basis = options.output_slew_basis;
      fit_options.max_abs_residual = options.max_output_slew_abs_residual_ns;
      fit_options.max_bucket_residual = options.max_output_slew_bucket_residual;
      fit_options.require_monotonic = options.require_monotonic_output_slew;
      break;
    case AnalyticalMetric::kDelay:
      fit_options.basis = options.delay_basis;
      fit_options.max_relative_residual = options.max_delay_relative_residual;
      fit_options.require_monotonic = options.require_monotonic_delay;
      break;
    case AnalyticalMetric::kPower:
      fit_options.basis = options.power_basis;
      fit_options.max_relative_residual = options.max_power_relative_residual;
      fit_options.require_monotonic = options.require_monotonic_power;
      break;
    case AnalyticalMetric::kSourceBoundaryNetSwitchPower:
      fit_options.basis = options.source_boundary_power_basis;
      fit_options.max_relative_residual = options.max_power_relative_residual;
      fit_options.require_monotonic = options.require_monotonic_source_boundary_power;
      break;
  }
  return fit_options;
}

auto CanUseSparseConstantModel(const std::string& failure_reason) -> bool
{
  return failure_reason == "insufficient_samples" || failure_reason == "singular_normal_equation" || failure_reason == "invalid_domain";
}

auto BuildSparseConstantModel(const std::vector<AnalyticalFitSample>& samples, const AnalyticalFitOptions& fit_options,
                              const UniformValueLattice& slew_lattice, const UniformValueLattice& cap_lattice)
    -> std::optional<AnalyticalSurfaceModel>
{
  if (samples.empty() || !slew_lattice.isValid() || !cap_lattice.isValid()) {
    return std::nullopt;
  }

  double total_value = 0.0;
  for (const auto& sample : samples) {
    total_value += sample.value;
  }
  const double mean_value = total_value / static_cast<double>(samples.size());

  AnalyticalSurfaceModel model;
  model.metric = fit_options.metric;
  model.basis = fit_options.basis;
  model.domain = AnalyticalDomain{
      .slew_min_ns = slew_lattice.stepValue(),
      .slew_max_ns = slew_lattice.maxValue(),
      .cap_min_pf = cap_lattice.stepValue(),
      .cap_max_pf = cap_lattice.maxValue(),
  };
  model.coefficients.assign(AnalyticalBasisTermCount(model.basis), 0.0);
  if (model.coefficients.empty()) {
    return std::nullopt;
  }
  model.coefficients.front() = mean_value;

  auto& quality = model.quality;
  quality.sample_count = samples.size();
  double square_error = 0.0;
  for (const auto& sample : samples) {
    const double residual = mean_value - sample.value;
    const double abs_residual = std::abs(residual);
    square_error += residual * residual;
    quality.max_abs_residual = std::max(quality.max_abs_residual, abs_residual);
    quality.max_relative_residual
        = std::max(quality.max_relative_residual, abs_residual / std::max(std::abs(sample.value), fit_options.relative_floor));
    if (fit_options.bucket_size > 0.0) {
      quality.max_bucket_residual = std::max(quality.max_bucket_residual, abs_residual / fit_options.bucket_size);
    }
  }
  quality.rmse = std::sqrt(square_error / static_cast<double>(samples.size()));
  quality.r2_valid = false;
  quality.r2 = 1.0;
  quality.monotonicity_passed = true;
  quality.bucket_aware_passed = true;
  quality.accepted = true;
  quality.failure_reason = "sparse_constant_model";
  return model;
}

auto FitMetric(const GroupedSegmentChars& group, const UniformValueLattice& slew_lattice, const UniformValueLattice& cap_lattice,
               AnalyticalMetric metric, const AnalyticalCharacterizationOptions& options, AnalyticalCharacterizationResult& result)
    -> std::optional<AnalyticalSurfaceModel>
{
  auto fit_options = MakeFitOptions(metric, options);
  if (metric == AnalyticalMetric::kOutputSlew) {
    fit_options.bucket_size = slew_lattice.stepValue();
  }
  auto samples = BuildSamples(group.chars, slew_lattice, cap_lattice, metric);
  auto fit = FitAnalyticalSurface(samples, fit_options);
  if (fit.success && fit.model.has_value()) {
    return fit.model;
  }

  if (options.allow_sparse_constant_model && CanUseSparseConstantModel(fit.failure_reason)) {
    auto sparse_model = BuildSparseConstantModel(samples, fit_options, slew_lattice, cap_lattice);
    if (sparse_model.has_value()) {
      return sparse_model;
    }
  }

  ++result.rejected_fit_count;
  result.failures.push_back(AnalyticalCharacterizationFailure{
      .key = group.key,
      .metric = metric,
      .reason = fit.failure_reason.empty() ? std::string{"fit_rejected"} : fit.failure_reason,
  });
  return std::nullopt;
}

auto ValidateBucketCompatibility(const StructuralCapOperator& op, const std::vector<SegmentChar>& grouped_chars,
                                 const UniformValueLattice& cap_lattice) -> bool
{
  for (const auto& segment_char : grouped_chars) {
    const double load_cap_pf = cap_lattice.valueForIndex(segment_char.get_load_cap_idx());
    const unsigned predicted_bucket = cap_lattice.coveringIndex(op.apply(load_cap_pf));
    if (predicted_bucket != segment_char.get_driven_cap_idx()) {
      return false;
    }
  }
  return true;
}

auto MakeExactStructuralCapOperator(const BufferingPattern* pattern, const AnalyticalCharacterizationOptions& options)
    -> std::optional<StructuralCapOperator>
{
  if (pattern == nullptr || options.length_unit_um <= 0.0 || options.routing_layer <= 0) {
    return std::nullopt;
  }

  const double total_length_um = static_cast<double>(pattern->get_length_idx()) * options.length_unit_um;
  if (total_length_um <= 0.0) {
    return std::nullopt;
  }

  if (pattern->isWirePattern()) {
    auto op = StructuralCapOperator::wire(
        STA_ADAPTER_INST.queryRequiredWireCapacitance(options.routing_layer, total_length_um, options.wire_width));
    op.source = "exact_wire";
    return op;
  }

  const auto& buffer_positions = pattern->get_buffer_positions();
  const auto& cell_masters = pattern->get_cell_masters();
  if (buffer_positions.empty() || cell_masters.empty() || buffer_positions.size() != cell_masters.size()) {
    return std::nullopt;
  }

  const double first_buffer_position = buffer_positions.front();
  if (first_buffer_position <= 0.0 || first_buffer_position > 1.0) {
    return std::nullopt;
  }

  const double first_buffer_input_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(cell_masters.front());
  if (first_buffer_input_cap_pf <= 0.0) {
    return std::nullopt;
  }

  const double pre_buffer_wire_length_um = total_length_um * first_buffer_position;
  auto op = StructuralCapOperator::buffered(
      first_buffer_input_cap_pf,
      STA_ADAPTER_INST.queryRequiredWireCapacitance(options.routing_layer, pre_buffer_wire_length_um, options.wire_width));
  op.source = "exact_buffered";
  return op;
}

auto MakeBucketRepresentativeOperator(const BufferingPattern* pattern, const std::vector<SegmentChar>& grouped_chars,
                                      const UniformValueLattice& cap_lattice) -> std::optional<StructuralCapOperator>
{
  if (grouped_chars.empty()) {
    return std::nullopt;
  }

  StructuralCapOperator op;
  op.physical = false;
  op.source = "bucket_representative";
  if (pattern != nullptr && pattern->isBufferPattern()) {
    op.alpha = 0.0;
    op.eta_pf = cap_lattice.valueForIndex(grouped_chars.front().get_driven_cap_idx());
    return op;
  }

  op.alpha = 1.0;
  double min_eta_pf = std::numeric_limits<double>::infinity();
  for (const auto& segment_char : grouped_chars) {
    const double driven_cap_pf = cap_lattice.valueForIndex(segment_char.get_driven_cap_idx());
    const double load_cap_pf = cap_lattice.valueForIndex(segment_char.get_load_cap_idx());
    min_eta_pf = std::min(min_eta_pf, std::max(0.0, driven_cap_pf - load_cap_pf));
  }
  op.eta_pf = std::isfinite(min_eta_pf) ? min_eta_pf : 0.0;
  return op;
}

auto BuildStructuralCapOperator(PatternId pattern_id, const std::vector<BufferingPattern>& buffering_patterns,
                                const std::vector<SegmentChar>& grouped_chars, const UniformValueLattice& cap_lattice,
                                const AnalyticalCharacterizationOptions& options) -> std::optional<StructuralCapOperator>
{
  const auto* pattern = FindPattern(pattern_id, buffering_patterns);
  auto op = options.prefer_exact_structural_cap ? MakeExactStructuralCapOperator(pattern, options)
                                                : MakeBucketRepresentativeOperator(pattern, grouped_chars, cap_lattice);
  if (!op.has_value() && !options.prefer_exact_structural_cap) {
    return std::nullopt;
  }
  if (!op.has_value()) {
    return std::nullopt;
  }

  op->bucket_compatible = ValidateBucketCompatibility(*op, grouped_chars, cap_lattice);
  return op;
}

}  // namespace

auto BuildBucketCompatibleStructuralCapOperator(PatternId pattern_id, const std::vector<BufferingPattern>& buffering_patterns,
                                                const std::vector<SegmentChar>& grouped_chars, const UniformValueLattice& cap_lattice)
    -> std::optional<StructuralCapOperator>
{
  const auto* pattern = FindPattern(pattern_id, buffering_patterns);
  auto op = MakeBucketRepresentativeOperator(pattern, grouped_chars, cap_lattice);
  if (!op.has_value()) {
    return std::nullopt;
  }
  op->bucket_compatible = ValidateBucketCompatibility(*op, grouped_chars, cap_lattice);
  return op;
}

auto AnalyticalCharacterization::buildFromCharBuilder(const CharBuilder& char_builder, const AnalyticalCharacterizationOptions& options)
    -> AnalyticalCharacterizationResult
{
  auto builder_options = options;
  if (builder_options.length_unit_um <= 0.0) {
    builder_options.length_unit_um = char_builder.get_wirelength_unit_um();
  }
  builder_options.routing_layer = char_builder.get_routing_layer();
  builder_options.wire_width = char_builder.get_wire_width();
  return buildFromSegmentChars(char_builder.get_segment_chars(), char_builder.get_buffering_patterns(), char_builder.get_slew_lattice(),
                               char_builder.get_cap_lattice(), builder_options);
}

auto AnalyticalCharacterization::buildFromSegmentChars(const std::vector<SegmentChar>& segment_chars,
                                                       const std::vector<BufferingPattern>& buffering_patterns,
                                                       const UniformValueLattice& slew_lattice, const UniformValueLattice& cap_lattice,
                                                       const AnalyticalCharacterizationOptions& options) -> AnalyticalCharacterizationResult
{
  AnalyticalCharacterizationResult result;
  if (segment_chars.empty() || !slew_lattice.isValid() || !cap_lattice.isValid()) {
    result.failures.push_back(AnalyticalCharacterizationFailure{
        .key = {},
        .metric = AnalyticalMetric::kDelay,
        .reason = "empty_samples_or_invalid_lattice",
    });
    return result;
  }

  for (const auto& group : GroupSegmentChars(segment_chars)) {
    AnalyticalModelSet model_set;
    model_set.key = group.key;
    model_set.output_slew_model = FitMetric(group, slew_lattice, cap_lattice, AnalyticalMetric::kOutputSlew, options, result);
    model_set.delay_model = FitMetric(group, slew_lattice, cap_lattice, AnalyticalMetric::kDelay, options, result);
    model_set.power_model = FitMetric(group, slew_lattice, cap_lattice, AnalyticalMetric::kPower, options, result);
    model_set.source_boundary_power_model
        = FitMetric(group, slew_lattice, cap_lattice, AnalyticalMetric::kSourceBoundaryNetSwitchPower, options, result);
    model_set.source_cap_operator = BuildStructuralCapOperator(group.key.pattern_id, buffering_patterns, group.chars, cap_lattice, options);
    if (model_set.source_cap_operator.has_value()) {
      ++result.structural_cap_operator_count;
      if (!model_set.source_cap_operator->bucket_compatible) {
        result.failures.push_back(AnalyticalCharacterizationFailure{
            .key = group.key,
            .metric = AnalyticalMetric::kDelay,
            .reason = "structural_cap_bucket_incompatible",
        });
      }
    } else {
      result.failures.push_back(AnalyticalCharacterizationFailure{
          .key = group.key,
          .metric = AnalyticalMetric::kDelay,
          .reason = "missing_structural_cap_operator",
      });
    }

    if (model_set.isComplete()) {
      result.catalog.addModelSet(std::move(model_set));
      ++result.model_set_count;
    }
  }

  result.success = result.model_set_count > 0U && result.rejected_fit_count == 0U;
  return result;
}

}  // namespace icts::analytical
