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
 * @file NumericalHTreeBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Numerical H-tree pattern selection flow built from fitted response models.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "characterization/PatternId.hh"

namespace icts {

class NumericalCharLibrary;
class Pin;

enum class NumericalHTreeResponseBasis
{
  kConstant,
  kAffine,
  kQuadratic
};

class NumericalHTreeResponseSurface
{
 public:
  NumericalHTreeResponseSurface() = default;

  static auto constant(double value) -> NumericalHTreeResponseSurface;
  static auto affine(double intercept, double slew_coeff, double cap_coeff) -> NumericalHTreeResponseSurface;
  static auto quadratic(double intercept, double slew_coeff, double cap_coeff, double slew_cap_coeff, double slew_sq_coeff,
                        double cap_sq_coeff) -> NumericalHTreeResponseSurface;
  static auto normalized(NumericalHTreeResponseBasis basis, std::vector<double> coefficients, double slew_center_ns, double slew_scale_ns,
                         double cap_center_pf, double cap_scale_pf) -> NumericalHTreeResponseSurface;

  auto evaluate(double input_slew_ns, double load_cap_pf) const -> std::optional<double>;
  auto isValid() const -> bool;
  auto get_basis() const -> NumericalHTreeResponseBasis { return _basis; }
  auto get_coefficients() const -> const std::vector<double>& { return _coefficients; }

 private:
  NumericalHTreeResponseSurface(NumericalHTreeResponseBasis basis, std::vector<double> coefficients);
  NumericalHTreeResponseSurface(NumericalHTreeResponseBasis basis, std::vector<double> coefficients, double slew_center_ns,
                                double slew_scale_ns, double cap_center_pf, double cap_scale_pf, bool normalize_inputs);

  NumericalHTreeResponseBasis _basis = NumericalHTreeResponseBasis::kConstant;
  std::vector<double> _coefficients;
  double _slew_center_ns = 0.0;
  double _slew_scale_ns = 1.0;
  double _cap_center_pf = 0.0;
  double _cap_scale_pf = 1.0;
  bool _normalize_inputs = false;
};

struct NumericalHTreeFitMetrics
{
  std::size_t sample_count = 0U;
  unsigned rank = 0U;
  double rmse = 0.0;
  double r2 = 0.0;
  double max_abs_error = 0.0;
};

struct NumericalHTreePatternModel
{
  PatternId pattern_id = PatternId::segment(0U);
  std::string model_name;
  NumericalHTreeResponseSurface delay_ns;
  NumericalHTreeResponseSurface output_slew_ns;
  NumericalHTreeResponseSurface driven_cap_pf;
  NumericalHTreeResponseSurface power_w;
  NumericalHTreeResponseSurface source_boundary_switch_power_w;
  std::optional<NumericalHTreeFitMetrics> delay_metrics = std::nullopt;
  std::optional<NumericalHTreeFitMetrics> output_slew_metrics = std::nullopt;
  std::optional<NumericalHTreeFitMetrics> driven_cap_metrics = std::nullopt;
  std::optional<NumericalHTreeFitMetrics> power_metrics = std::nullopt;
  std::optional<NumericalHTreeFitMetrics> source_boundary_switch_power_metrics = std::nullopt;
};

struct NumericalHTreeLevelInput
{
  double representative_load_cap_pf = 0.0;
  std::vector<NumericalHTreePatternModel> pattern_models;
};

struct NumericalHTreeOptions
{
  double top_input_slew_ns = 0.0;
  double leaf_load_cap_pf = 0.0;
  std::size_t top_k_per_level = 8U;
  double delay_weight = 1.0;
  double power_weight = 1.0;
  double output_slew_weight = 0.0;
  double driven_cap_weight = 0.0;
  bool require_non_negative_qor = true;
  std::optional<unsigned> target_depth = std::nullopt;
  std::optional<unsigned> depth_explore_window = std::nullopt;
  std::optional<double> max_model_slew_ns = std::nullopt;
  std::optional<double> max_model_load_cap_pf = std::nullopt;
  bool require_positive_leaf_power = false;
};

struct NumericalHTreeModelQualitySummary
{
  bool available = false;
  std::size_t model_count = 0U;
  std::size_t metric_count = 0U;
  std::size_t min_sample_count = 0U;
  unsigned min_rank = 0U;
  double min_r2 = 0.0;
  double max_rmse = 0.0;
  double max_abs_error = 0.0;
  std::string note;
};

struct NumericalHTreeModelMetric
{
  std::string label;
  std::size_t sample_count = 0U;
  unsigned rank = 0U;
  double r2 = 0.0;
  double rmse = 0.0;
  double max_abs_error = 0.0;
};

struct NumericalHTreeBuildInput
{
  NumericalHTreeOptions options;
  std::vector<NumericalHTreeLevelInput> levels;
  std::optional<NumericalHTreeModelQualitySummary> model_quality_summary = std::nullopt;
};

struct NumericalHTreeLevelResult
{
  unsigned level_index = 0U;
  PatternId segment_pattern_id = PatternId::segment(0U);
  std::string model_name;
  double input_slew_ns = 0.0;
  double load_cap_pf = 0.0;
  double output_slew_ns = 0.0;
  double driven_cap_pf = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_switch_power_w = 0.0;
  double composed_power_contribution_w = 0.0;
};

struct NumericalHTreeSelectedDepth
{
  unsigned value = 0U;
  bool valid = false;

  auto operator=(unsigned selected_depth) -> NumericalHTreeSelectedDepth&
  {
    value = selected_depth;
    valid = selected_depth > 0U;
    return *this;
  }

  auto has_value() const -> bool { return valid; }
  auto value_or(unsigned fallback) const -> unsigned { return valid ? value : fallback; }
  operator unsigned() const { return value; }
};

struct NumericalHTreeResult
{
  bool success = false;
  std::string failure_reason;
  std::optional<unsigned> failure_level = std::nullopt;
  NumericalHTreeSelectedDepth selected_depth;
  std::size_t selected_levels = 0U;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double selected_delay_ns = 0.0;
  double selected_power_w = 0.0;
  double selected_score = 0.0;
  double runtime_ms = 0.0;
  std::vector<PatternId> selected_segment_pattern_ids;
  std::vector<NumericalHTreeLevelResult> levels;
  std::vector<NumericalHTreeLevelResult> level_results;
  std::vector<NumericalHTreeModelMetric> model_metrics;
  NumericalHTreeModelQualitySummary model_quality_summary;
  std::size_t evaluated_candidate_count = 0U;
  std::size_t pruned_candidate_count = 0U;
  std::vector<std::size_t> level_candidate_state_counts;
  std::vector<std::size_t> level_surviving_state_counts;
};

struct NumericalHTreeNativeReference
{
  bool available = false;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double runtime_ms = 0.0;
  std::vector<PatternId> segment_pattern_ids;
};

struct NumericalHTreeComparison
{
  bool available = false;
  bool numerical_success = false;
  bool native_available = false;
  bool runtime_faster = false;
  bool delay_within_tolerance = false;
  bool power_within_tolerance = false;
  double numerical_delay_ns = 0.0;
  double numerical_power_w = 0.0;
  double numerical_runtime_ms = 0.0;
  double native_delay_ns = 0.0;
  double native_power_w = 0.0;
  double native_runtime_ms = 0.0;
  double relative_delay_delta = 0.0;
  double relative_power_delta = 0.0;
  std::vector<PatternId> numerical_segment_pattern_ids;
  std::vector<PatternId> native_segment_pattern_ids;
};

class NumericalHTreeBuilder
{
 public:
  NumericalHTreeBuilder() = default;
  ~NumericalHTreeBuilder() = default;

  static auto build(const NumericalHTreeBuildInput& input) -> NumericalHTreeResult;
  static auto build(const NumericalCharLibrary& library, const std::vector<unsigned>& level_length_indices,
                    const NumericalHTreeOptions& options) -> NumericalHTreeResult;
  static auto build(const std::vector<Pin*>& loads, const NumericalHTreeOptions& options) -> NumericalHTreeResult;
  static auto compareWithNative(const NumericalHTreeResult& numerical_result, const NumericalHTreeNativeReference& native_reference,
                                double delay_relative_tolerance = 0.20, double power_relative_tolerance = 0.25) -> NumericalHTreeComparison;
};

}  // namespace icts
