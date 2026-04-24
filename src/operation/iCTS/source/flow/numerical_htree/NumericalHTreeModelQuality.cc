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
 * @file NumericalHTreeModelQuality.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Numerical H-tree fitted model quality aggregation helpers.
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "PatternId.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"

namespace icts {
namespace {

auto IsMetricUsable(const NumericalHTreeFitMetrics& metrics) -> bool
{
  return metrics.sample_count > 0U && numerical_htree::IsFinite(metrics.rmse) && numerical_htree::IsFinite(metrics.r2)
         && numerical_htree::IsFinite(metrics.max_abs_error);
}

auto AccumulateMetric(const NumericalHTreeFitMetrics& metrics, NumericalHTreeModelQualitySummary& summary) -> void
{
  if (!IsMetricUsable(metrics)) {
    return;
  }

  if (summary.metric_count == 0U) {
    summary.min_sample_count = metrics.sample_count;
    summary.min_rank = metrics.rank;
    summary.min_r2 = metrics.r2;
    summary.max_rmse = metrics.rmse;
    summary.max_abs_error = metrics.max_abs_error;
  } else {
    summary.min_sample_count = std::min(summary.min_sample_count, metrics.sample_count);
    summary.min_rank = std::min(summary.min_rank, metrics.rank);
    summary.min_r2 = std::min(summary.min_r2, metrics.r2);
    summary.max_rmse = std::max(summary.max_rmse, metrics.rmse);
    summary.max_abs_error = std::max(summary.max_abs_error, metrics.max_abs_error);
  }
  ++summary.metric_count;
}

auto AppendMetric(std::vector<NumericalHTreeModelMetric>& model_metrics, const std::string& label,
                  const std::optional<NumericalHTreeFitMetrics>& metrics) -> void
{
  if (!metrics.has_value() || !IsMetricUsable(*metrics)) {
    return;
  }

  model_metrics.push_back(NumericalHTreeModelMetric{
      .label = label,
      .sample_count = metrics->sample_count,
      .rank = metrics->rank,
      .r2 = metrics->r2,
      .rmse = metrics->rmse,
      .max_abs_error = metrics->max_abs_error,
  });
}

}  // namespace

auto CollectModelMetrics(const NumericalHTreeBuildInput& input) -> std::vector<NumericalHTreeModelMetric>
{
  std::vector<NumericalHTreeModelMetric> model_metrics;
  for (std::size_t level_index = 0U; level_index < input.levels.size(); ++level_index) {
    const auto& level = input.levels[level_index];
    for (const auto& model : level.pattern_models) {
      const std::string prefix = model.model_name.empty()
                                     ? "level_" + std::to_string(level_index) + "_pattern_" + std::to_string(model.pattern_id.local_id)
                                     : model.model_name;
      AppendMetric(model_metrics, prefix + ".delay", model.delay_metrics);
      AppendMetric(model_metrics, prefix + ".output_slew", model.output_slew_metrics);
      AppendMetric(model_metrics, prefix + ".driven_cap", model.driven_cap_metrics);
      AppendMetric(model_metrics, prefix + ".power", model.power_metrics);
      AppendMetric(model_metrics, prefix + ".source_boundary_switch_power", model.source_boundary_switch_power_metrics);
    }
  }
  return model_metrics;
}

auto SummarizeModelQuality(const NumericalHTreeBuildInput& input) -> NumericalHTreeModelQualitySummary
{
  if (input.model_quality_summary.has_value()) {
    return *input.model_quality_summary;
  }

  NumericalHTreeModelQualitySummary summary;
  for (const auto& level : input.levels) {
    summary.model_count += level.pattern_models.size();
    for (const auto& model : level.pattern_models) {
      if (model.delay_metrics.has_value()) {
        AccumulateMetric(*model.delay_metrics, summary);
      }
      if (model.output_slew_metrics.has_value()) {
        AccumulateMetric(*model.output_slew_metrics, summary);
      }
      if (model.driven_cap_metrics.has_value()) {
        AccumulateMetric(*model.driven_cap_metrics, summary);
      }
      if (model.power_metrics.has_value()) {
        AccumulateMetric(*model.power_metrics, summary);
      }
      if (model.source_boundary_switch_power_metrics.has_value()) {
        AccumulateMetric(*model.source_boundary_switch_power_metrics, summary);
      }
    }
  }

  summary.available = summary.metric_count > 0U;
  summary.note = summary.available ? "aggregated_from_pattern_model_metrics" : "model_quality_metrics_unavailable";
  return summary;
}

}  // namespace icts
