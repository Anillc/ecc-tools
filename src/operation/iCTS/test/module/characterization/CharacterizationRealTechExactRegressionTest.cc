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
 * @file CharacterizationRealTechExactRegressionTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-18
 * @brief Exact compose and exact join regression coverage on real-tech assets.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/adapter/sta/STAAdapter.hh"
#include "database/characterization/BufferingPattern.hh"
#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "database/characterization/ValueLattice.hh"
#include "module/characterization/CharBuilder.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

namespace icts_test {
namespace {

namespace realtech_support = characterization::realtech;

auto MakeExactRegressionCharBuilderInitOptions() -> icts::CharBuilder::InitOptions
{
  auto options = realtech_support::MakeRuntimeCharBuilderInitOptions();
  const double length_step_um = options.wirelength_unit_um.value_or(realtech_support::kRealTechCharWirelengthUnitUm);
  if (length_step_um <= 0.0) {
    return options;
  }

  const unsigned required_iterations = realtech_support::MakeLengthIndex(realtech_support::kExactRootLevelLengthUm, length_step_um);
  options.wirelength_iterations = std::max(options.wirelength_iterations.value_or(1U), required_iterations);
  return options;
}

auto MakeIterOneExperimentCharBuilderInitOptions() -> icts::CharBuilder::InitOptions
{
  auto options = realtech_support::MakeRuntimeCharBuilderInitOptions();
  options.wirelength_iterations = std::max(options.wirelength_iterations.value_or(1U), 3U);
  return options;
}

enum class FitBasisKind
{
  kLinear,
  kQuadratic,
};

auto FitBasisName(FitBasisKind basis_kind) -> std::string
{
  return basis_kind == FitBasisKind::kLinear ? "linear" : "quadratic";
}

auto FitBasisSize(FitBasisKind basis_kind) -> std::size_t
{
  return basis_kind == FitBasisKind::kLinear ? 3U : 6U;
}

auto MakeFitBasis(FitBasisKind basis_kind, double s, double c) -> std::array<double, 6>
{
  std::array<double, 6> basis{1.0, s, c, s * c, s * s, c * c};
  if (basis_kind == FitBasisKind::kLinear) {
    basis[3] = 0.0;
    basis[4] = 0.0;
    basis[5] = 0.0;
  }
  return basis;
}

auto MakeFitBasis(FitBasisKind basis_kind, const icts::SegmentChar& entry) -> std::array<double, 6>
{
  const auto s = static_cast<double>(entry.get_input_slew_idx());
  const auto c = static_cast<double>(entry.get_load_cap_idx());
  return MakeFitBasis(basis_kind, s, c);
}

auto SolveSmallLinearSystem(std::array<std::array<double, 6>, 6> matrix, std::array<double, 6> rhs, std::size_t size,
                            std::array<double, 6>& solution) -> bool
{
  for (std::size_t pivot_col = 0U; pivot_col < size; ++pivot_col) {
    std::size_t pivot_row = pivot_col;
    double pivot_abs = std::abs(matrix[pivot_col][pivot_col]);
    for (std::size_t row = pivot_col + 1U; row < size; ++row) {
      const double candidate_abs = std::abs(matrix[row][pivot_col]);
      if (candidate_abs > pivot_abs) {
        pivot_abs = candidate_abs;
        pivot_row = row;
      }
    }

    if (pivot_abs <= 1e-18) {
      return false;
    }

    if (pivot_row != pivot_col) {
      std::swap(matrix[pivot_row], matrix[pivot_col]);
      std::swap(rhs[pivot_row], rhs[pivot_col]);
    }

    const double pivot = matrix[pivot_col][pivot_col];
    for (std::size_t row = pivot_col + 1U; row < size; ++row) {
      const double factor = matrix[row][pivot_col] / pivot;
      matrix[row][pivot_col] = 0.0;
      for (std::size_t col = pivot_col + 1U; col < size; ++col) {
        matrix[row][col] -= factor * matrix[pivot_col][col];
      }
      rhs[row] -= factor * rhs[pivot_col];
    }
  }

  solution.fill(0.0);
  for (std::size_t row_from_end = 0U; row_from_end < size; ++row_from_end) {
    const std::size_t row = size - 1U - row_from_end;
    double residual = rhs[row];
    for (std::size_t col = row + 1U; col < size; ++col) {
      residual -= matrix[row][col] * solution[col];
    }
    solution[row] = residual / matrix[row][row];
  }
  return true;
}

struct GroupFitResult
{
  std::size_t sample_count = 0U;
  double sum_abs_y = 0.0;
  double max_abs_y = 0.0;
  double sse = 0.0;
  double sst = 0.0;
  double rmse = 0.0;
  double r2 = 1.0;
};

struct FitStats
{
  std::size_t fitted_groups = 0U;
  std::size_t skipped_groups = 0U;
  std::size_t sample_count = 0U;
  double sum_abs_y = 0.0;
  double max_abs_y = 0.0;
  double sse = 0.0;
  double sst = 0.0;
  double max_group_rmse = 0.0;
  double min_group_r2 = 1.0;
};

auto TryFitGroup(const std::vector<const icts::SegmentChar*>& group, const std::function<double(const icts::SegmentChar&)>& metric_fn,
                 FitBasisKind basis_kind) -> std::optional<GroupFitResult>
{
  const std::size_t basis_size = FitBasisSize(basis_kind);
  if (group.size() < basis_size) {
    return std::nullopt;
  }

  double sum_y = 0.0;
  for (const auto* entry : group) {
    sum_y += metric_fn(*entry);
  }
  const double mean_y = sum_y / static_cast<double>(group.size());

  std::array<std::array<double, 6>, 6> normal_matrix{};
  std::array<double, 6> normal_rhs{};
  for (const auto* entry : group) {
    const auto basis = MakeFitBasis(basis_kind, *entry);
    const double y = metric_fn(*entry);
    for (std::size_t row = 0U; row < basis_size; ++row) {
      normal_rhs[row] += basis[row] * y;
      for (std::size_t col = 0U; col < basis_size; ++col) {
        normal_matrix[row][col] += basis[row] * basis[col];
      }
    }
  }

  std::array<double, 6> coefficients{};
  if (!SolveSmallLinearSystem(normal_matrix, normal_rhs, basis_size, coefficients)) {
    return std::nullopt;
  }

  GroupFitResult result;
  result.sample_count = group.size();
  for (const auto* entry : group) {
    const auto basis = MakeFitBasis(basis_kind, *entry);
    double predicted = 0.0;
    for (std::size_t term = 0U; term < basis_size; ++term) {
      predicted += coefficients[term] * basis[term];
    }

    const double y = metric_fn(*entry);
    const double error = predicted - y;
    result.sse += error * error;
    const double centered = y - mean_y;
    result.sst += centered * centered;
    const double abs_y = std::abs(y);
    result.sum_abs_y += abs_y;
    result.max_abs_y = std::max(result.max_abs_y, abs_y);
  }

  result.rmse = std::sqrt(result.sse / static_cast<double>(result.sample_count));
  if (result.sst > 1e-30) {
    result.r2 = 1.0 - result.sse / result.sst;
  } else {
    result.r2 = result.sse <= 1e-30 ? 1.0 : 0.0;
  }
  return result;
}

auto TryFitSurfaceCoefficients(const std::vector<const icts::SegmentChar*>& group,
                               const std::function<double(const icts::SegmentChar&)>& metric_fn, const realtech_support::CharGrid& grid,
                               FitBasisKind basis_kind) -> std::optional<std::array<double, 6>>
{
  const std::size_t basis_size = FitBasisSize(basis_kind);
  if (group.size() < basis_size) {
    return std::nullopt;
  }

  std::array<std::array<double, 6>, 6> normal_matrix{};
  std::array<double, 6> normal_rhs{};
  for (const auto* entry : group) {
    const double input_slew_ns = static_cast<double>(entry->get_input_slew_idx()) * grid.slew_step_ns;
    const double load_cap_pf = static_cast<double>(entry->get_load_cap_idx()) * grid.cap_step_pf;
    const auto basis = MakeFitBasis(basis_kind, input_slew_ns, load_cap_pf);
    const double y = metric_fn(*entry);
    for (std::size_t row = 0U; row < basis_size; ++row) {
      normal_rhs[row] += basis[row] * y;
      for (std::size_t col = 0U; col < basis_size; ++col) {
        normal_matrix[row][col] += basis[row] * basis[col];
      }
    }
  }

  std::array<double, 6> coefficients{};
  if (!SolveSmallLinearSystem(normal_matrix, normal_rhs, basis_size, coefficients)) {
    return std::nullopt;
  }
  return coefficients;
}

auto FitMetricByPattern(const std::vector<icts::SegmentChar>& entries, unsigned length_idx,
                        const std::function<double(const icts::SegmentChar&)>& metric_fn, FitBasisKind basis_kind) -> FitStats
{
  std::unordered_map<icts::PatternId, std::vector<const icts::SegmentChar*>> groups;
  for (const auto& entry : entries) {
    if (entry.get_length_idx() == length_idx) {
      groups[entry.get_pattern_id()].push_back(&entry);
    }
  }

  FitStats stats;
  for (const auto& [pattern_id, group] : groups) {
    (void) pattern_id;
    const auto group_result = TryFitGroup(group, metric_fn, basis_kind);
    if (!group_result.has_value()) {
      ++stats.skipped_groups;
      continue;
    }

    ++stats.fitted_groups;
    stats.sample_count += group_result->sample_count;
    stats.sum_abs_y += group_result->sum_abs_y;
    stats.max_abs_y = std::max(stats.max_abs_y, group_result->max_abs_y);
    stats.sse += group_result->sse;
    stats.sst += group_result->sst;
    stats.max_group_rmse = std::max(stats.max_group_rmse, group_result->rmse);
    stats.min_group_r2 = std::min(stats.min_group_r2, group_result->r2);
  }
  return stats;
}

auto OverallRmse(const FitStats& stats) -> double
{
  return stats.sample_count == 0U ? 0.0 : std::sqrt(stats.sse / static_cast<double>(stats.sample_count));
}

auto OverallR2(const FitStats& stats) -> double
{
  if (stats.sst > 1e-30) {
    return 1.0 - stats.sse / stats.sst;
  }
  return stats.sse <= 1e-30 ? 1.0 : 0.0;
}

auto RelativeRmse(const FitStats& stats) -> double
{
  if (stats.sample_count == 0U) {
    return 0.0;
  }
  const double mean_abs_y = stats.sum_abs_y / static_cast<double>(stats.sample_count);
  const double denominator = std::max(mean_abs_y, 1e-30);
  return OverallRmse(stats) / denominator;
}

auto AppendFitStats(std::ostringstream& report_stream, const std::string& metric_name, FitBasisKind basis_kind, const FitStats& stats)
    -> void
{
  report_stream << "iter1_fit{metric=" << metric_name << ",basis=" << FitBasisName(basis_kind) << ",fitted_groups=" << stats.fitted_groups
                << ",skipped_groups=" << stats.skipped_groups << ",samples=" << stats.sample_count << ",rmse=" << OverallRmse(stats)
                << ",relative_rmse=" << RelativeRmse(stats) << ",r2=" << OverallR2(stats) << ",max_group_rmse=" << stats.max_group_rmse
                << ",min_group_r2=" << stats.min_group_r2 << "}\n";
}

auto AppendIterOneFitReport(std::ostringstream& report_stream, const std::vector<icts::SegmentChar>& entries,
                            const realtech_support::CharGrid& grid, unsigned length_idx) -> void
{
  std::size_t length_one_sample_count = 0U;
  std::unordered_map<icts::PatternId, std::size_t> pattern_sample_counts;
  for (const auto& entry : entries) {
    if (entry.get_length_idx() != length_idx) {
      continue;
    }
    ++length_one_sample_count;
    ++pattern_sample_counts[entry.get_pattern_id()];
  }

  report_stream << "iter1_fit_input_space=input_slew_idx,load_cap_idx\n";
  report_stream << "iter1_fit_length_idx=" << length_idx << "\n";
  report_stream << "iter1_fit_length_um=" << (static_cast<double>(length_idx) * grid.length_step_um) << "\n";
  report_stream << "iter1_fit_raw_sample_count=" << length_one_sample_count << "\n";
  report_stream << "iter1_fit_pattern_group_count=" << pattern_sample_counts.size() << "\n";

  const auto append_metric = [&](const std::string& metric_name, const std::function<double(const icts::SegmentChar&)>& metric_fn) -> void {
    AppendFitStats(report_stream, metric_name, FitBasisKind::kLinear,
                   FitMetricByPattern(entries, length_idx, metric_fn, FitBasisKind::kLinear));
    AppendFitStats(report_stream, metric_name, FitBasisKind::kQuadratic,
                   FitMetricByPattern(entries, length_idx, metric_fn, FitBasisKind::kQuadratic));
  };

  append_metric("output_slew_ns", [&grid](const icts::SegmentChar& entry) -> double {
    return static_cast<double>(entry.get_output_slew_idx()) * grid.slew_step_ns;
  });
  append_metric("driven_cap_pf", [&grid](const icts::SegmentChar& entry) -> double {
    return static_cast<double>(entry.get_driven_cap_idx()) * grid.cap_step_pf;
  });
  append_metric("delay_ns", [](const icts::SegmentChar& entry) -> double { return entry.get_delay(); });
  append_metric("power_w", [](const icts::SegmentChar& entry) -> double { return entry.get_power(); });
  append_metric("source_boundary_net_switch_power_w",
                [](const icts::SegmentChar& entry) -> double { return entry.get_source_boundary_net_switch_power(); });
}

struct SegmentCompareKey
{
  std::string pattern_signature;
  unsigned input_slew_idx = 0U;
  unsigned output_slew_idx = 0U;
  unsigned driven_cap_idx = 0U;
  unsigned load_cap_idx = 0U;

  auto operator==(const SegmentCompareKey& rhs) const -> bool = default;
};

struct SegmentCompareKeyHash
{
  auto operator()(const SegmentCompareKey& key) const -> std::size_t
  {
    std::size_t seed = std::hash<std::string>{}(key.pattern_signature);
    seed ^= std::hash<unsigned>{}(key.input_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.output_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.driven_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.load_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

auto MakePatternSignature(const realtech_support::SegmentFrontierContext& segment_context, icts::PatternId pattern_id) -> std::string
{
  const auto pattern_it = segment_context.patterns.find(pattern_id);
  if (pattern_it == segment_context.patterns.end()) {
    std::ostringstream stream;
    stream << "missing{domain=" << static_cast<unsigned>(pattern_id.domain) << ",local_id=" << pattern_id.local_id << "}";
    return stream.str();
  }

  const auto& pattern = pattern_it->second;
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream << std::setprecision(6);
  stream << "len=" << pattern.get_length_idx();
  stream << ";terminal=" << (pattern.hasTerminalBranchBuffer() ? 1 : 0);
  stream << ";src_has=" << (pattern.get_monotonic_boundary_state().source.has_buffer ? 1 : 0);
  stream << ";src_rank=" << pattern.get_monotonic_boundary_state().source.strength_rank;
  stream << ";snk_has=" << (pattern.get_monotonic_boundary_state().sink.has_buffer ? 1 : 0);
  stream << ";snk_rank=" << pattern.get_monotonic_boundary_state().sink.strength_rank;
  stream << ";pos=";
  for (double pos : pattern.get_buffer_positions()) {
    stream << pos << ",";
  }
  stream << ";masters=";
  for (const auto& cell_master : pattern.get_cell_masters()) {
    stream << cell_master << ",";
  }
  return stream.str();
}

auto MakeSegmentCompareKey(const realtech_support::SegmentFrontierContext& segment_context, const icts::SegmentChar& entry)
    -> SegmentCompareKey
{
  return SegmentCompareKey{
      .pattern_signature = MakePatternSignature(segment_context, entry.get_pattern_id()),
      .input_slew_idx = entry.get_input_slew_idx(),
      .output_slew_idx = entry.get_output_slew_idx(),
      .driven_cap_idx = entry.get_driven_cap_idx(),
      .load_cap_idx = entry.get_load_cap_idx(),
  };
}

struct ComposeGapStats
{
  unsigned target_length_idx = 0U;
  std::size_t direct_count = 0U;
  std::size_t composed_count = 0U;
  std::size_t direct_duplicate_key_count = 0U;
  std::size_t matched_count = 0U;
  std::size_t missing_composed_count = 0U;
  std::size_t composed_lower_delay_count = 0U;
  std::size_t composed_higher_delay_count = 0U;
  std::size_t composed_lower_power_count = 0U;
  std::size_t composed_higher_power_count = 0U;
  double sum_direct_delay_ns = 0.0;
  double sum_composed_delay_ns = 0.0;
  double sum_abs_delay_delta_ns = 0.0;
  double sum_sq_delay_delta_ns = 0.0;
  double max_abs_delay_delta_ns = 0.0;
  double max_rel_delay_delta = 0.0;
  double sum_direct_power_w = 0.0;
  double sum_composed_power_w = 0.0;
  double sum_abs_power_delta_w = 0.0;
  double sum_sq_power_delta_w = 0.0;
  double max_abs_power_delta_w = 0.0;
  double max_rel_power_delta = 0.0;
  std::string worst_delay_example;
  std::string worst_power_example;
  std::string missing_composed_example;
};

auto IsPreferredDirectEntry(const icts::SegmentChar& candidate, const icts::SegmentChar& current) -> bool
{
  if (candidate.get_delay() != current.get_delay()) {
    return candidate.get_delay() < current.get_delay();
  }
  return candidate.get_power() < current.get_power();
}

auto SafeRatio(double numerator, double denominator) -> double
{
  return std::abs(denominator) > 1e-30 ? numerator / denominator : 0.0;
}

auto CompareComposedFrontierToDirect(unsigned target_length_idx, const std::vector<icts::SegmentChar>& direct_entries,
                                     const std::vector<icts::SegmentChar>& composed_entries,
                                     const realtech_support::SegmentFrontierContext& segment_context,
                                     const realtech_support::CharGrid& grid) -> ComposeGapStats
{
  ComposeGapStats stats;
  stats.target_length_idx = target_length_idx;
  stats.direct_count = direct_entries.size();
  stats.composed_count = composed_entries.size();

  std::unordered_map<SegmentCompareKey, icts::SegmentChar, SegmentCompareKeyHash> direct_by_key;
  direct_by_key.reserve(direct_entries.size());
  for (const auto& entry : direct_entries) {
    const auto key = MakeSegmentCompareKey(segment_context, entry);
    auto [it, inserted] = direct_by_key.emplace(key, entry);
    if (!inserted) {
      ++stats.direct_duplicate_key_count;
      if (IsPreferredDirectEntry(entry, it->second)) {
        it->second = entry;
      }
    }
  }

  for (const auto& composed_entry : composed_entries) {
    const auto direct_it = direct_by_key.find(MakeSegmentCompareKey(segment_context, composed_entry));
    if (direct_it == direct_by_key.end()) {
      ++stats.missing_composed_count;
      if (stats.missing_composed_example.empty()) {
        stats.missing_composed_example = realtech_support::FormatSegmentChar(composed_entry, grid);
      }
      continue;
    }

    const auto& direct_entry = direct_it->second;
    ++stats.matched_count;
    stats.sum_direct_delay_ns += direct_entry.get_delay();
    stats.sum_composed_delay_ns += composed_entry.get_delay();
    stats.sum_direct_power_w += direct_entry.get_power();
    stats.sum_composed_power_w += composed_entry.get_power();

    const double delay_delta_ns = composed_entry.get_delay() - direct_entry.get_delay();
    const double abs_delay_delta_ns = std::abs(delay_delta_ns);
    stats.sum_abs_delay_delta_ns += abs_delay_delta_ns;
    stats.sum_sq_delay_delta_ns += delay_delta_ns * delay_delta_ns;
    stats.max_rel_delay_delta
        = std::max(stats.max_rel_delay_delta, abs_delay_delta_ns / std::max(std::abs(direct_entry.get_delay()), 1e-30));
    if (delay_delta_ns < -1e-15) {
      ++stats.composed_lower_delay_count;
    } else if (delay_delta_ns > 1e-15) {
      ++stats.composed_higher_delay_count;
    }
    if (abs_delay_delta_ns > stats.max_abs_delay_delta_ns) {
      stats.max_abs_delay_delta_ns = abs_delay_delta_ns;
      std::ostringstream example;
      example << "direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
              << " composed=" << realtech_support::FormatSegmentChar(composed_entry, grid);
      stats.worst_delay_example = example.str();
    }

    const double power_delta_w = composed_entry.get_power() - direct_entry.get_power();
    const double abs_power_delta_w = std::abs(power_delta_w);
    stats.sum_abs_power_delta_w += abs_power_delta_w;
    stats.sum_sq_power_delta_w += power_delta_w * power_delta_w;
    stats.max_rel_power_delta
        = std::max(stats.max_rel_power_delta, abs_power_delta_w / std::max(std::abs(direct_entry.get_power()), 1e-30));
    if (power_delta_w < -1e-15) {
      ++stats.composed_lower_power_count;
    } else if (power_delta_w > 1e-15) {
      ++stats.composed_higher_power_count;
    }
    if (abs_power_delta_w > stats.max_abs_power_delta_w) {
      stats.max_abs_power_delta_w = abs_power_delta_w;
      std::ostringstream example;
      example << "direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
              << " composed=" << realtech_support::FormatSegmentChar(composed_entry, grid);
      stats.worst_power_example = example.str();
    }
  }

  return stats;
}

auto AppendComposeGapStats(std::ostringstream& report_stream, const ComposeGapStats& stats, const realtech_support::CharGrid& grid) -> void
{
  const auto matched_count = static_cast<double>(stats.matched_count);
  const double delay_rmse_ns = stats.matched_count == 0U ? 0.0 : std::sqrt(stats.sum_sq_delay_delta_ns / matched_count);
  const double delay_mean_abs_ns = stats.matched_count == 0U ? 0.0 : stats.sum_abs_delay_delta_ns / matched_count;
  const double power_rmse_w = stats.matched_count == 0U ? 0.0 : std::sqrt(stats.sum_sq_power_delta_w / matched_count);
  const double power_mean_abs_w = stats.matched_count == 0U ? 0.0 : stats.sum_abs_power_delta_w / matched_count;

  report_stream << "compose_gap{target_length_idx=" << stats.target_length_idx
                << ",target_length_um=" << (static_cast<double>(stats.target_length_idx) * grid.length_step_um)
                << ",direct_frontier_count=" << stats.direct_count << ",composed_frontier_count=" << stats.composed_count
                << ",direct_duplicate_key_count=" << stats.direct_duplicate_key_count << ",matched_count=" << stats.matched_count
                << ",missing_composed_count=" << stats.missing_composed_count
                << ",match_over_composed=" << SafeRatio(static_cast<double>(stats.matched_count), static_cast<double>(stats.composed_count))
                << ",match_over_direct=" << SafeRatio(static_cast<double>(stats.matched_count), static_cast<double>(stats.direct_count))
                << ",sum_direct_delay_ns=" << stats.sum_direct_delay_ns << ",sum_composed_delay_ns=" << stats.sum_composed_delay_ns
                << ",delay_sum_delta_ns=" << (stats.sum_composed_delay_ns - stats.sum_direct_delay_ns)
                << ",delay_ratio_composed_over_direct=" << SafeRatio(stats.sum_composed_delay_ns, stats.sum_direct_delay_ns)
                << ",delay_rmse_ns=" << delay_rmse_ns << ",delay_mean_abs_ns=" << delay_mean_abs_ns
                << ",delay_max_abs_ns=" << stats.max_abs_delay_delta_ns << ",delay_max_rel=" << stats.max_rel_delay_delta
                << ",composed_lower_delay_count=" << stats.composed_lower_delay_count
                << ",composed_higher_delay_count=" << stats.composed_higher_delay_count
                << ",sum_direct_power_w=" << stats.sum_direct_power_w << ",sum_composed_power_w=" << stats.sum_composed_power_w
                << ",power_sum_delta_w=" << (stats.sum_composed_power_w - stats.sum_direct_power_w)
                << ",power_ratio_composed_over_direct=" << SafeRatio(stats.sum_composed_power_w, stats.sum_direct_power_w)
                << ",power_rmse_w=" << power_rmse_w << ",power_mean_abs_w=" << power_mean_abs_w
                << ",power_max_abs_w=" << stats.max_abs_power_delta_w << ",power_max_rel=" << stats.max_rel_power_delta
                << ",composed_lower_power_count=" << stats.composed_lower_power_count
                << ",composed_higher_power_count=" << stats.composed_higher_power_count << "}\n";
  report_stream << "compose_gap_worst_delay{target_length_idx=" << stats.target_length_idx << "," << stats.worst_delay_example << "}\n";
  report_stream << "compose_gap_worst_power{target_length_idx=" << stats.target_length_idx << "," << stats.worst_power_example << "}\n";
  report_stream << "compose_gap_missing_example{target_length_idx=" << stats.target_length_idx << "," << stats.missing_composed_example
                << "}\n";
}

auto MakeUnitPatternKey(const std::vector<double>& buffer_positions, const std::vector<std::string>& cell_masters) -> std::string
{
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream << std::setprecision(9);
  stream << "pos=";
  for (double position : buffer_positions) {
    stream << position << ",";
  }
  stream << ";masters=";
  for (const auto& cell_master : cell_masters) {
    stream << cell_master << ",";
  }
  return stream.str();
}

auto MakeUnitPatternKey(const icts::BufferingPattern& pattern) -> std::string
{
  return MakeUnitPatternKey(pattern.get_buffer_positions(), pattern.get_cell_masters());
}

struct FunctionalSurfaceModel
{
  std::string unit_key;
  FitBasisKind basis_kind = FitBasisKind::kLinear;
  std::array<double, 6> output_slew_coefficients{};
  std::array<double, 6> driven_cap_coefficients{};
  std::array<double, 6> delay_coefficients{};
  std::array<double, 6> power_coefficients{};
  std::array<double, 6> source_boundary_power_coefficients{};
};

struct StructuralCapOperator
{
  std::string unit_key;
  double alpha = 0.0;
  double eta_pf = 0.0;
  std::size_t sample_count = 0U;
  double max_abs_residual_pf = 0.0;
};

auto EvalSurface(const FunctionalSurfaceModel& model, const std::array<double, 6>& coefficients, double input_slew_ns, double load_cap_pf)
    -> double
{
  const auto basis = MakeFitBasis(model.basis_kind, input_slew_ns, load_cap_pf);
  double value = 0.0;
  for (std::size_t term = 0U; term < FitBasisSize(model.basis_kind); ++term) {
    value += coefficients[term] * basis[term];
  }
  return value;
}

struct FunctionalSurfaceResponse
{
  double output_slew_ns = 0.0;
  double driven_cap_pf = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_power_w = 0.0;
};

auto EvalFunctionalSurfaceModel(const FunctionalSurfaceModel& model, double input_slew_ns, double load_cap_pf) -> FunctionalSurfaceResponse
{
  return FunctionalSurfaceResponse{
      .output_slew_ns = EvalSurface(model, model.output_slew_coefficients, input_slew_ns, load_cap_pf),
      .driven_cap_pf = EvalSurface(model, model.driven_cap_coefficients, input_slew_ns, load_cap_pf),
      .delay_ns = EvalSurface(model, model.delay_coefficients, input_slew_ns, load_cap_pf),
      .power_w = EvalSurface(model, model.power_coefficients, input_slew_ns, load_cap_pf),
      .source_boundary_power_w = EvalSurface(model, model.source_boundary_power_coefficients, input_slew_ns, load_cap_pf),
  };
}

auto IsFinitePositive(double value) -> bool
{
  return std::isfinite(value) && value > 0.0;
}

auto BuildFunctionalSurfaceModels(const std::vector<icts::SegmentChar>& entries,
                                  const realtech_support::SegmentFrontierContext& segment_context, const realtech_support::CharGrid& grid,
                                  FitBasisKind basis_kind) -> std::unordered_map<std::string, FunctionalSurfaceModel>
{
  std::unordered_map<icts::PatternId, std::vector<const icts::SegmentChar*>> groups;
  for (const auto& entry : entries) {
    if (entry.get_length_idx() == 1U) {
      groups[entry.get_pattern_id()].push_back(&entry);
    }
  }

  std::unordered_map<std::string, FunctionalSurfaceModel> models;
  for (const auto& [pattern_id, group] : groups) {
    const auto pattern_it = segment_context.patterns.find(pattern_id);
    if (pattern_it == segment_context.patterns.end()) {
      continue;
    }

    const auto output_slew_coefficients = TryFitSurfaceCoefficients(
        group,
        [&grid](const icts::SegmentChar& entry) -> double { return static_cast<double>(entry.get_output_slew_idx()) * grid.slew_step_ns; },
        grid, basis_kind);
    const auto driven_cap_coefficients = TryFitSurfaceCoefficients(
        group,
        [&grid](const icts::SegmentChar& entry) -> double { return static_cast<double>(entry.get_driven_cap_idx()) * grid.cap_step_pf; },
        grid, basis_kind);
    const auto delay_coefficients
        = TryFitSurfaceCoefficients(group, [](const icts::SegmentChar& entry) -> double { return entry.get_delay(); }, grid, basis_kind);
    const auto power_coefficients
        = TryFitSurfaceCoefficients(group, [](const icts::SegmentChar& entry) -> double { return entry.get_power(); }, grid, basis_kind);
    const auto source_boundary_power_coefficients = TryFitSurfaceCoefficients(
        group, [](const icts::SegmentChar& entry) -> double { return entry.get_source_boundary_net_switch_power(); }, grid, basis_kind);
    if (!output_slew_coefficients.has_value() || !driven_cap_coefficients.has_value() || !delay_coefficients.has_value()
        || !power_coefficients.has_value() || !source_boundary_power_coefficients.has_value()) {
      continue;
    }

    const std::string unit_key = MakeUnitPatternKey(pattern_it->second);
    models[unit_key] = FunctionalSurfaceModel{
        .unit_key = unit_key,
        .basis_kind = basis_kind,
        .output_slew_coefficients = *output_slew_coefficients,
        .driven_cap_coefficients = *driven_cap_coefficients,
        .delay_coefficients = *delay_coefficients,
        .power_coefficients = *power_coefficients,
        .source_boundary_power_coefficients = *source_boundary_power_coefficients,
    };
  }

  return models;
}

auto BuildPhysicalStructuralCapOperators(const realtech_support::SegmentFrontierContext& segment_context,
                                         const icts::CharBuilder::InitOptions& options, const realtech_support::CharGrid& grid)
    -> std::unordered_map<std::string, StructuralCapOperator>
{
  std::unordered_map<std::string, StructuralCapOperator> operators;
  if (!options.routing_layer.has_value() || *options.routing_layer <= 0) {
    return operators;
  }
  const int routing_layer = *options.routing_layer;
  for (const auto& [pattern_id, pattern] : segment_context.patterns) {
    (void) pattern_id;
    if (pattern.get_length_idx() != 1U) {
      continue;
    }

    const auto& buffer_positions = pattern.get_buffer_positions();
    const auto& cell_masters = pattern.get_cell_masters();
    if (buffer_positions.size() != cell_masters.size()) {
      continue;
    }

    const double unit_length_um = static_cast<double>(pattern.get_length_idx()) * grid.length_step_um;
    double alpha = 1.0;
    double eta_pf = STA_ADAPTER_INST.queryWireCapacitance(routing_layer, unit_length_um, options.wire_width);
    if (!cell_masters.empty()) {
      alpha = 0.0;
      const double first_buffer_position = buffer_positions.front();
      const double prewire_length_um = std::clamp(first_buffer_position, 0.0, 1.0) * unit_length_um;
      eta_pf = STA_ADAPTER_INST.queryCharInputPinCap(cell_masters.front());
      eta_pf += STA_ADAPTER_INST.queryWireCapacitance(routing_layer, prewire_length_um, options.wire_width);
    }

    if (!std::isfinite(eta_pf) || eta_pf < 0.0) {
      continue;
    }

    operators[MakeUnitPatternKey(pattern)] = StructuralCapOperator{
        .unit_key = MakeUnitPatternKey(pattern),
        .alpha = alpha,
        .eta_pf = eta_pf,
        .sample_count = 0U,
        .max_abs_residual_pf = 0.0,
    };
  }

  return operators;
}

auto DecomposeToUnitPatternKeys(const icts::BufferingPattern& pattern, unsigned target_length_idx)
    -> std::optional<std::vector<std::string>>
{
  if (target_length_idx == 0U) {
    return std::nullopt;
  }

  std::vector<std::vector<double>> slot_positions(target_length_idx);
  std::vector<std::vector<std::string>> slot_masters(target_length_idx);

  const auto& positions = pattern.get_buffer_positions();
  const auto& masters = pattern.get_cell_masters();
  if (positions.size() != masters.size()) {
    return std::nullopt;
  }

  constexpr double position_epsilon = 1e-7;
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    const double scaled_position = positions.at(index) * static_cast<double>(target_length_idx);
    auto one_based_slot = static_cast<int>(std::ceil(scaled_position - position_epsilon));
    one_based_slot = std::clamp(one_based_slot, 1, static_cast<int>(target_length_idx));
    const auto slot_index = static_cast<unsigned>(one_based_slot - 1);
    double local_position = scaled_position - static_cast<double>(slot_index);
    if (std::abs(local_position - 1.0) <= position_epsilon) {
      local_position = 1.0;
    }
    if (local_position <= position_epsilon || local_position > 1.0 + position_epsilon) {
      return std::nullopt;
    }

    slot_positions.at(slot_index).push_back(local_position);
    slot_masters.at(slot_index).push_back(masters.at(index));
  }

  std::vector<std::string> unit_keys;
  unit_keys.reserve(target_length_idx);
  for (unsigned slot_index = 0U; slot_index < target_length_idx; ++slot_index) {
    unit_keys.push_back(MakeUnitPatternKey(slot_positions.at(slot_index), slot_masters.at(slot_index)));
  }
  return unit_keys;
}

struct FunctionalComposePrediction
{
  bool is_valid = false;
  bool is_out_of_domain = false;
  bool converged = false;
  unsigned iterations = 0U;
  double residual = 0.0;
  double output_slew_ns = 0.0;
  double driven_cap_pf = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_power_w = 0.0;
};

auto PredictFunctionalCompose(const std::vector<const FunctionalSurfaceModel*>& unit_models, double input_slew_ns, double load_cap_pf,
                              double max_slew_ns, double max_cap_pf) -> FunctionalComposePrediction
{
  FunctionalComposePrediction prediction;
  const std::size_t unit_count = unit_models.size();
  if (unit_count == 0U || !IsFinitePositive(input_slew_ns) || !IsFinitePositive(load_cap_pf)) {
    return prediction;
  }

  std::vector<double> slews(unit_count + 1U, input_slew_ns);
  std::vector<double> load_caps(unit_count + 1U, load_cap_pf);
  load_caps.back() = load_cap_pf;

  constexpr unsigned max_iterations = 100U;
  constexpr double convergence_tolerance = 1e-12;
  for (unsigned iteration = 1U; iteration <= max_iterations; ++iteration) {
    slews.front() = input_slew_ns;
    bool finite_iteration = true;
    for (std::size_t unit_index = 0U; unit_index < unit_count; ++unit_index) {
      const auto response = EvalFunctionalSurfaceModel(*unit_models.at(unit_index), slews.at(unit_index), load_caps.at(unit_index + 1U));
      if (!std::isfinite(response.output_slew_ns)) {
        finite_iteration = false;
        break;
      }
      slews.at(unit_index + 1U) = response.output_slew_ns;
    }
    if (!finite_iteration) {
      return prediction;
    }

    auto next_load_caps = load_caps;
    next_load_caps.back() = load_cap_pf;
    for (std::size_t reverse_index = 0U; reverse_index < unit_count; ++reverse_index) {
      const std::size_t unit_index = unit_count - 1U - reverse_index;
      const auto response
          = EvalFunctionalSurfaceModel(*unit_models.at(unit_index), slews.at(unit_index), next_load_caps.at(unit_index + 1U));
      if (!std::isfinite(response.driven_cap_pf)) {
        return prediction;
      }
      next_load_caps.at(unit_index) = response.driven_cap_pf;
    }

    double residual = 0.0;
    for (std::size_t index = 0U; index < unit_count; ++index) {
      residual = std::max(residual, std::abs(next_load_caps.at(index) - load_caps.at(index)));
    }
    load_caps = std::move(next_load_caps);
    prediction.iterations = iteration;
    prediction.residual = residual;
    if (residual <= convergence_tolerance) {
      prediction.converged = true;
      break;
    }
  }

  if (!prediction.converged) {
    return prediction;
  }

  std::vector<FunctionalSurfaceResponse> responses;
  responses.reserve(unit_count);
  slews.front() = input_slew_ns;
  for (std::size_t unit_index = 0U; unit_index < unit_count; ++unit_index) {
    const auto response = EvalFunctionalSurfaceModel(*unit_models.at(unit_index), slews.at(unit_index), load_caps.at(unit_index + 1U));
    if (!std::isfinite(response.output_slew_ns) || !std::isfinite(response.driven_cap_pf) || !std::isfinite(response.delay_ns)
        || !std::isfinite(response.power_w) || !std::isfinite(response.source_boundary_power_w)) {
      return prediction;
    }
    responses.push_back(response);
    slews.at(unit_index + 1U) = response.output_slew_ns;
  }

  prediction.output_slew_ns = responses.back().output_slew_ns;
  prediction.driven_cap_pf = responses.front().driven_cap_pf;
  prediction.source_boundary_power_w = responses.front().source_boundary_power_w;
  for (std::size_t unit_index = 0U; unit_index < unit_count; ++unit_index) {
    prediction.delay_ns += responses.at(unit_index).delay_ns;
    prediction.power_w += responses.at(unit_index).power_w;
    if (unit_index > 0U) {
      prediction.power_w -= responses.at(unit_index).source_boundary_power_w;
    }
  }

  prediction.is_valid = IsFinitePositive(prediction.output_slew_ns) && IsFinitePositive(prediction.driven_cap_pf)
                        && std::isfinite(prediction.delay_ns) && prediction.delay_ns >= 0.0 && std::isfinite(prediction.power_w)
                        && prediction.power_w >= 0.0 && std::isfinite(prediction.source_boundary_power_w)
                        && prediction.source_boundary_power_w >= 0.0;
  if (!prediction.is_valid) {
    return prediction;
  }

  for (double slew_ns : slews) {
    if (slew_ns <= 0.0 || slew_ns > max_slew_ns) {
      prediction.is_out_of_domain = true;
    }
  }
  for (double cap_pf : load_caps) {
    if (cap_pf <= 0.0 || cap_pf > max_cap_pf) {
      prediction.is_out_of_domain = true;
    }
  }
  return prediction;
}

auto PredictStructuralCapFunctionalCompose(const std::vector<const FunctionalSurfaceModel*>& unit_models,
                                           const std::vector<const StructuralCapOperator*>& cap_operators, double input_slew_ns,
                                           double load_cap_pf, double max_slew_ns, double max_cap_pf) -> FunctionalComposePrediction
{
  FunctionalComposePrediction prediction;
  const std::size_t unit_count = unit_models.size();
  if (unit_count == 0U || unit_count != cap_operators.size() || !IsFinitePositive(input_slew_ns) || !IsFinitePositive(load_cap_pf)) {
    return prediction;
  }

  std::vector<double> load_caps(unit_count + 1U, load_cap_pf);
  load_caps.back() = load_cap_pf;
  for (std::size_t reverse_index = 0U; reverse_index < unit_count; ++reverse_index) {
    const std::size_t unit_index = unit_count - 1U - reverse_index;
    const auto& cap_operator = *cap_operators.at(unit_index);
    load_caps.at(unit_index) = (cap_operator.alpha * load_caps.at(unit_index + 1U)) + cap_operator.eta_pf;
    if (!std::isfinite(load_caps.at(unit_index))) {
      return prediction;
    }
  }

  std::vector<FunctionalSurfaceResponse> responses;
  responses.reserve(unit_count);
  std::vector<double> slews(unit_count + 1U, input_slew_ns);
  slews.front() = input_slew_ns;
  for (std::size_t unit_index = 0U; unit_index < unit_count; ++unit_index) {
    const auto response = EvalFunctionalSurfaceModel(*unit_models.at(unit_index), slews.at(unit_index), load_caps.at(unit_index + 1U));
    if (!std::isfinite(response.output_slew_ns) || !std::isfinite(response.delay_ns) || !std::isfinite(response.power_w)
        || !std::isfinite(response.source_boundary_power_w)) {
      return prediction;
    }
    responses.push_back(response);
    slews.at(unit_index + 1U) = response.output_slew_ns;
  }

  prediction.converged = true;
  prediction.iterations = 1U;
  prediction.residual = 0.0;
  prediction.output_slew_ns = responses.back().output_slew_ns;
  prediction.driven_cap_pf = load_caps.front();
  prediction.source_boundary_power_w = responses.front().source_boundary_power_w;
  for (std::size_t unit_index = 0U; unit_index < unit_count; ++unit_index) {
    prediction.delay_ns += responses.at(unit_index).delay_ns;
    prediction.power_w += responses.at(unit_index).power_w;
    if (unit_index > 0U) {
      prediction.power_w -= responses.at(unit_index).source_boundary_power_w;
    }
  }

  prediction.is_valid = IsFinitePositive(prediction.output_slew_ns) && IsFinitePositive(prediction.driven_cap_pf)
                        && std::isfinite(prediction.delay_ns) && prediction.delay_ns >= 0.0 && std::isfinite(prediction.power_w)
                        && prediction.power_w >= 0.0 && std::isfinite(prediction.source_boundary_power_w)
                        && prediction.source_boundary_power_w >= 0.0;
  if (!prediction.is_valid) {
    return prediction;
  }

  for (double slew_ns : slews) {
    if (slew_ns <= 0.0 || slew_ns > max_slew_ns) {
      prediction.is_out_of_domain = true;
    }
  }
  for (double cap_pf : load_caps) {
    if (cap_pf <= 0.0 || cap_pf > max_cap_pf) {
      prediction.is_out_of_domain = true;
    }
  }
  return prediction;
}

struct MetricGapAccumulator
{
  std::size_t count = 0U;
  std::size_t predicted_lower_count = 0U;
  std::size_t predicted_higher_count = 0U;
  double sum_direct = 0.0;
  double sum_predicted = 0.0;
  double sum_abs_delta = 0.0;
  double sum_sq_delta = 0.0;
  double max_abs_delta = 0.0;
  double max_rel_delta = 0.0;
  std::string worst_example;
};

auto AddMetricGap(MetricGapAccumulator& accumulator, double direct_value, double predicted_value, const std::string& example) -> void
{
  ++accumulator.count;
  accumulator.sum_direct += direct_value;
  accumulator.sum_predicted += predicted_value;
  const double delta = predicted_value - direct_value;
  const double abs_delta = std::abs(delta);
  accumulator.sum_abs_delta += abs_delta;
  accumulator.sum_sq_delta += delta * delta;
  accumulator.max_rel_delta = std::max(accumulator.max_rel_delta, abs_delta / std::max(std::abs(direct_value), 1e-30));
  if (delta < -1e-15) {
    ++accumulator.predicted_lower_count;
  } else if (delta > 1e-15) {
    ++accumulator.predicted_higher_count;
  }
  if (abs_delta > accumulator.max_abs_delta) {
    accumulator.max_abs_delta = abs_delta;
    accumulator.worst_example = example;
  }
}

auto MetricRmse(const MetricGapAccumulator& accumulator) -> double
{
  return accumulator.count == 0U ? 0.0 : std::sqrt(accumulator.sum_sq_delta / static_cast<double>(accumulator.count));
}

auto MetricMeanAbs(const MetricGapAccumulator& accumulator) -> double
{
  return accumulator.count == 0U ? 0.0 : accumulator.sum_abs_delta / static_cast<double>(accumulator.count);
}

struct FunctionComposeGapStats
{
  std::string source_label;
  unsigned target_length_idx = 0U;
  FitBasisKind basis_kind = FitBasisKind::kLinear;
  std::size_t direct_count = 0U;
  std::size_t decomposable_count = 0U;
  std::size_t missing_model_count = 0U;
  std::size_t convergence_failure_count = 0U;
  std::size_t invalid_prediction_count = 0U;
  std::size_t out_of_domain_count = 0U;
  std::size_t evaluated_count = 0U;
  double max_fixed_point_residual = 0.0;
  unsigned max_fixed_point_iterations = 0U;
  MetricGapAccumulator output_slew;
  MetricGapAccumulator driven_cap;
  MetricGapAccumulator delay;
  MetricGapAccumulator power;
  MetricGapAccumulator source_boundary_power;
  std::string missing_model_example;
  std::string convergence_failure_example;
  std::string invalid_prediction_example;
};

auto AnalyzeFunctionComposeGap(const std::string& source_label, unsigned target_length_idx, FitBasisKind basis_kind,
                               const std::vector<icts::SegmentChar>& direct_entries,
                               const realtech_support::SegmentFrontierContext& segment_context,
                               const std::unordered_map<std::string, FunctionalSurfaceModel>& model_by_unit_key,
                               const realtech_support::CharGrid& grid, double max_slew_ns, double max_cap_pf) -> FunctionComposeGapStats
{
  FunctionComposeGapStats stats;
  stats.source_label = source_label;
  stats.target_length_idx = target_length_idx;
  stats.basis_kind = basis_kind;

  for (const auto& direct_entry : direct_entries) {
    if (direct_entry.get_length_idx() != target_length_idx) {
      continue;
    }
    ++stats.direct_count;

    const auto pattern_it = segment_context.patterns.find(direct_entry.get_pattern_id());
    if (pattern_it == segment_context.patterns.end()) {
      continue;
    }
    const auto unit_keys = DecomposeToUnitPatternKeys(pattern_it->second, target_length_idx);
    if (!unit_keys.has_value()) {
      continue;
    }
    ++stats.decomposable_count;

    std::vector<const FunctionalSurfaceModel*> unit_models;
    unit_models.reserve(unit_keys->size());
    bool has_missing_model = false;
    for (const auto& unit_key : *unit_keys) {
      const auto model_it = model_by_unit_key.find(unit_key);
      if (model_it == model_by_unit_key.end()) {
        has_missing_model = true;
        if (stats.missing_model_example.empty()) {
          stats.missing_model_example = unit_key;
        }
        break;
      }
      unit_models.push_back(&model_it->second);
    }
    if (has_missing_model) {
      ++stats.missing_model_count;
      continue;
    }

    const double input_slew_ns = static_cast<double>(direct_entry.get_input_slew_idx()) * grid.slew_step_ns;
    const double load_cap_pf = static_cast<double>(direct_entry.get_load_cap_idx()) * grid.cap_step_pf;
    const auto prediction = PredictFunctionalCompose(unit_models, input_slew_ns, load_cap_pf, max_slew_ns, max_cap_pf);
    stats.max_fixed_point_residual = std::max(stats.max_fixed_point_residual, prediction.residual);
    stats.max_fixed_point_iterations = std::max(stats.max_fixed_point_iterations, prediction.iterations);
    if (!prediction.converged) {
      ++stats.convergence_failure_count;
      if (stats.convergence_failure_example.empty()) {
        stats.convergence_failure_example = realtech_support::FormatSegmentChar(direct_entry, grid);
      }
      continue;
    }
    if (!prediction.is_valid) {
      ++stats.invalid_prediction_count;
      if (stats.invalid_prediction_example.empty()) {
        stats.invalid_prediction_example = realtech_support::FormatSegmentChar(direct_entry, grid);
      }
      continue;
    }
    if (prediction.is_out_of_domain) {
      ++stats.out_of_domain_count;
    }

    ++stats.evaluated_count;
    std::ostringstream example;
    example << "direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
            << ", predicted{output_slew_ns=" << prediction.output_slew_ns << ",driven_cap_pf=" << prediction.driven_cap_pf
            << ",delay_ns=" << prediction.delay_ns << ",power_w=" << prediction.power_w
            << ",source_boundary_power_w=" << prediction.source_boundary_power_w << "}";
    const auto example_text = example.str();

    AddMetricGap(stats.output_slew, static_cast<double>(direct_entry.get_output_slew_idx()) * grid.slew_step_ns, prediction.output_slew_ns,
                 example_text);
    AddMetricGap(stats.driven_cap, static_cast<double>(direct_entry.get_driven_cap_idx()) * grid.cap_step_pf, prediction.driven_cap_pf,
                 example_text);
    AddMetricGap(stats.delay, direct_entry.get_delay(), prediction.delay_ns, example_text);
    AddMetricGap(stats.power, direct_entry.get_power(), prediction.power_w, example_text);
    AddMetricGap(stats.source_boundary_power, direct_entry.get_source_boundary_net_switch_power(), prediction.source_boundary_power_w,
                 example_text);
  }

  return stats;
}

auto AnalyzeStructuralCapFunctionComposeGap(const std::string& source_label, unsigned target_length_idx, FitBasisKind basis_kind,
                                            const std::vector<icts::SegmentChar>& direct_entries,
                                            const realtech_support::SegmentFrontierContext& segment_context,
                                            const std::unordered_map<std::string, FunctionalSurfaceModel>& model_by_unit_key,
                                            const std::unordered_map<std::string, StructuralCapOperator>& cap_operator_by_unit_key,
                                            const realtech_support::CharGrid& grid, const icts::UniformValueLattice& cap_lattice,
                                            double max_slew_ns, double max_cap_pf) -> FunctionComposeGapStats
{
  FunctionComposeGapStats stats;
  stats.source_label = source_label;
  stats.target_length_idx = target_length_idx;
  stats.basis_kind = basis_kind;

  for (const auto& direct_entry : direct_entries) {
    if (direct_entry.get_length_idx() != target_length_idx) {
      continue;
    }
    ++stats.direct_count;

    const auto pattern_it = segment_context.patterns.find(direct_entry.get_pattern_id());
    if (pattern_it == segment_context.patterns.end()) {
      continue;
    }
    const auto unit_keys = DecomposeToUnitPatternKeys(pattern_it->second, target_length_idx);
    if (!unit_keys.has_value()) {
      continue;
    }
    ++stats.decomposable_count;

    std::vector<const FunctionalSurfaceModel*> unit_models;
    std::vector<const StructuralCapOperator*> cap_operators;
    unit_models.reserve(unit_keys->size());
    cap_operators.reserve(unit_keys->size());
    bool has_missing_model = false;
    for (const auto& unit_key : *unit_keys) {
      const auto model_it = model_by_unit_key.find(unit_key);
      const auto cap_operator_it = cap_operator_by_unit_key.find(unit_key);
      if (model_it == model_by_unit_key.end() || cap_operator_it == cap_operator_by_unit_key.end()) {
        has_missing_model = true;
        if (stats.missing_model_example.empty()) {
          stats.missing_model_example = unit_key;
        }
        break;
      }
      unit_models.push_back(&model_it->second);
      cap_operators.push_back(&cap_operator_it->second);
    }
    if (has_missing_model) {
      ++stats.missing_model_count;
      continue;
    }

    const double input_slew_ns = static_cast<double>(direct_entry.get_input_slew_idx()) * grid.slew_step_ns;
    const double load_cap_pf = static_cast<double>(direct_entry.get_load_cap_idx()) * grid.cap_step_pf;
    const auto prediction
        = PredictStructuralCapFunctionalCompose(unit_models, cap_operators, input_slew_ns, load_cap_pf, max_slew_ns, max_cap_pf);
    stats.max_fixed_point_residual = std::max(stats.max_fixed_point_residual, prediction.residual);
    stats.max_fixed_point_iterations = std::max(stats.max_fixed_point_iterations, prediction.iterations);
    if (!prediction.converged) {
      ++stats.convergence_failure_count;
      if (stats.convergence_failure_example.empty()) {
        stats.convergence_failure_example = realtech_support::FormatSegmentChar(direct_entry, grid);
      }
      continue;
    }
    if (!prediction.is_valid) {
      ++stats.invalid_prediction_count;
      if (stats.invalid_prediction_example.empty()) {
        stats.invalid_prediction_example = realtech_support::FormatSegmentChar(direct_entry, grid);
      }
      continue;
    }
    if (prediction.is_out_of_domain) {
      ++stats.out_of_domain_count;
    }

    ++stats.evaluated_count;
    std::ostringstream example;
    example << "direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
            << ", predicted{output_slew_ns=" << prediction.output_slew_ns << ",driven_cap_pf=" << prediction.driven_cap_pf
            << ",delay_ns=" << prediction.delay_ns << ",power_w=" << prediction.power_w
            << ",source_boundary_power_w=" << prediction.source_boundary_power_w << "}";
    const auto example_text = example.str();
    const double predicted_driven_cap_bucket_pf
        = static_cast<double>(cap_lattice.coveringIndex(prediction.driven_cap_pf)) * grid.cap_step_pf;

    AddMetricGap(stats.output_slew, static_cast<double>(direct_entry.get_output_slew_idx()) * grid.slew_step_ns, prediction.output_slew_ns,
                 example_text);
    AddMetricGap(stats.driven_cap, static_cast<double>(direct_entry.get_driven_cap_idx()) * grid.cap_step_pf,
                 predicted_driven_cap_bucket_pf, example_text);
    AddMetricGap(stats.delay, direct_entry.get_delay(), prediction.delay_ns, example_text);
    AddMetricGap(stats.power, direct_entry.get_power(), prediction.power_w, example_text);
    AddMetricGap(stats.source_boundary_power, direct_entry.get_source_boundary_net_switch_power(), prediction.source_boundary_power_w,
                 example_text);
  }

  return stats;
}

auto AppendMetricGap(std::ostringstream& report_stream, const std::string& source_label, FitBasisKind basis_kind,
                     unsigned target_length_idx, const std::string& metric_name, const MetricGapAccumulator& accumulator) -> void
{
  report_stream << "function_compose_metric{source=" << source_label << ",basis=" << FitBasisName(basis_kind)
                << ",target_length_idx=" << target_length_idx << ",metric=" << metric_name << ",count=" << accumulator.count
                << ",sum_direct=" << accumulator.sum_direct << ",sum_predicted=" << accumulator.sum_predicted
                << ",ratio_predicted_over_direct=" << SafeRatio(accumulator.sum_predicted, accumulator.sum_direct)
                << ",rmse=" << MetricRmse(accumulator) << ",mean_abs=" << MetricMeanAbs(accumulator)
                << ",max_abs=" << accumulator.max_abs_delta << ",max_rel=" << accumulator.max_rel_delta
                << ",predicted_lower_count=" << accumulator.predicted_lower_count
                << ",predicted_higher_count=" << accumulator.predicted_higher_count << "}\n";
  report_stream << "function_compose_metric_worst{source=" << source_label << ",basis=" << FitBasisName(basis_kind)
                << ",target_length_idx=" << target_length_idx << ",metric=" << metric_name << "," << accumulator.worst_example << "}\n";
}

auto AppendFunctionComposeGapStats(std::ostringstream& report_stream, const FunctionComposeGapStats& stats,
                                   const realtech_support::CharGrid& grid) -> void
{
  const std::string label = "function_compose_gap{source=" + stats.source_label + ",basis=" + FitBasisName(stats.basis_kind)
                            + ",target_length_idx=" + std::to_string(stats.target_length_idx);
  report_stream << label << ",target_length_um=" << (static_cast<double>(stats.target_length_idx) * grid.length_step_um)
                << ",direct_count=" << stats.direct_count << ",decomposable_count=" << stats.decomposable_count
                << ",missing_model_count=" << stats.missing_model_count << ",convergence_failure_count=" << stats.convergence_failure_count
                << ",invalid_prediction_count=" << stats.invalid_prediction_count << ",out_of_domain_count=" << stats.out_of_domain_count
                << ",evaluated_count=" << stats.evaluated_count << ",max_fixed_point_residual=" << stats.max_fixed_point_residual
                << ",max_fixed_point_iterations=" << stats.max_fixed_point_iterations << "}\n";
  AppendMetricGap(report_stream, stats.source_label, stats.basis_kind, stats.target_length_idx, "output_slew_ns", stats.output_slew);
  AppendMetricGap(report_stream, stats.source_label, stats.basis_kind, stats.target_length_idx, "driven_cap_pf", stats.driven_cap);
  AppendMetricGap(report_stream, stats.source_label, stats.basis_kind, stats.target_length_idx, "delay_ns", stats.delay);
  AppendMetricGap(report_stream, stats.source_label, stats.basis_kind, stats.target_length_idx, "power_w", stats.power);
  AppendMetricGap(report_stream, stats.source_label, stats.basis_kind, stats.target_length_idx, "source_boundary_power_w",
                  stats.source_boundary_power);
  report_stream << "function_compose_missing_model_example{source=" << stats.source_label << ",basis=" << FitBasisName(stats.basis_kind)
                << ",target_length_idx=" << stats.target_length_idx << "," << stats.missing_model_example << "}\n";
  report_stream << "function_compose_convergence_failure_example{source=" << stats.source_label
                << ",basis=" << FitBasisName(stats.basis_kind) << ",target_length_idx=" << stats.target_length_idx << ","
                << stats.convergence_failure_example << "}\n";
  report_stream << "function_compose_invalid_prediction_example{source=" << stats.source_label
                << ",basis=" << FitBasisName(stats.basis_kind) << ",target_length_idx=" << stats.target_length_idx << ","
                << stats.invalid_prediction_example << "}\n";
}

auto AppendStructuralCapOperatorStats(std::ostringstream& report_stream,
                                      const std::unordered_map<std::string, StructuralCapOperator>& cap_operator_by_unit_key) -> void
{
  std::size_t wire_operator_count = 0U;
  std::size_t buffered_operator_count = 0U;
  std::size_t sample_count = 0U;
  double max_abs_residual_pf = 0.0;
  for (const auto& [unit_key, cap_operator] : cap_operator_by_unit_key) {
    (void) unit_key;
    if (cap_operator.alpha == 1.0) {
      ++wire_operator_count;
    } else {
      ++buffered_operator_count;
    }
    sample_count += cap_operator.sample_count;
    max_abs_residual_pf = std::max(max_abs_residual_pf, cap_operator.max_abs_residual_pf);
  }

  report_stream << "structural_cap_operator_count{total=" << cap_operator_by_unit_key.size() << ",wire=" << wire_operator_count
                << ",buffered=" << buffered_operator_count << ",samples=" << sample_count << ",max_abs_residual_pf=" << max_abs_residual_pf
                << "}\n";
}

auto AppendStructuralCapOperatorSampleGap(std::ostringstream& report_stream,
                                          const std::unordered_map<std::string, StructuralCapOperator>& cap_operator_by_unit_key,
                                          const std::vector<icts::SegmentChar>& entries,
                                          const realtech_support::SegmentFrontierContext& segment_context,
                                          const realtech_support::CharGrid& grid, const icts::UniformValueLattice& cap_lattice) -> void
{
  MetricGapAccumulator physical_gap;
  MetricGapAccumulator bucket_gap;
  std::size_t missing_operator_count = 0U;
  for (const auto& entry : entries) {
    if (entry.get_length_idx() != 1U) {
      continue;
    }
    const auto pattern_it = segment_context.patterns.find(entry.get_pattern_id());
    if (pattern_it == segment_context.patterns.end()) {
      continue;
    }
    const auto operator_it = cap_operator_by_unit_key.find(MakeUnitPatternKey(pattern_it->second));
    if (operator_it == cap_operator_by_unit_key.end()) {
      ++missing_operator_count;
      continue;
    }

    const double direct_pf = static_cast<double>(entry.get_driven_cap_idx()) * grid.cap_step_pf;
    const double load_cap_pf = static_cast<double>(entry.get_load_cap_idx()) * grid.cap_step_pf;
    const double predicted_physical_pf = (operator_it->second.alpha * load_cap_pf) + operator_it->second.eta_pf;
    const double predicted_bucket_pf
        = static_cast<double>(cap_lattice.tryObservedIndex(predicted_physical_pf).value_or(0U)) * grid.cap_step_pf;
    const std::string example = realtech_support::FormatSegmentChar(entry, grid);
    AddMetricGap(physical_gap, direct_pf, predicted_physical_pf, example);
    AddMetricGap(bucket_gap, direct_pf, predicted_bucket_pf, example);
  }

  report_stream << "structural_cap_operator_sample_gap{missing_operator_count=" << missing_operator_count << "}\n";
  AppendMetricGap(report_stream, "iter1_unit_physical_structural_cap", FitBasisKind::kLinear, 1U, "driven_cap_pf", physical_gap);
  AppendMetricGap(report_stream, "iter1_unit_bucket_structural_cap", FitBasisKind::kLinear, 1U, "driven_cap_pf", bucket_gap);
}

TEST(CharacterizationRealTechExactRegressionTest, ExactComposeAndExactJoinRemainUsable)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("exact_regression", std::nullopt, 0.0, 0.0); prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  const auto usable_buffers = realtech_support::CollectUsableBufferMasters(realtech_support::CollectConfiguredBufferLimitInfo());
  if (usable_buffers.empty()) {
    GTEST_SKIP() << "No configured buffer has both slew and cap support via port or table limits.";
  }

  icts::CharBuilder builder;
  builder.init(MakeExactRegressionCharBuilderInitOptions());
  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  auto segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  const auto lattice_summary = realtech_support::SummarizeSegmentCharLattice(builder.get_segment_chars(), builder);
  EXPECT_EQ(lattice_summary.out_of_range_entries, 0U) << realtech_support::FormatSegmentCharLatticeSummary(lattice_summary, builder);
  EXPECT_LE(lattice_summary.max_length_idx, builder.get_wirelength_iterations());
  EXPECT_LE(lattice_summary.max_input_slew_idx, builder.get_slew_steps());
  EXPECT_LE(lattice_summary.max_output_slew_idx, builder.get_slew_steps());
  EXPECT_LE(lattice_summary.max_driven_cap_idx, builder.get_cap_steps());
  EXPECT_LE(lattice_summary.max_load_cap_idx, builder.get_cap_steps());
  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);

  const unsigned length_25_idx = realtech_support::MakeLengthIndex(realtech_support::kExactLeafLevelLengthUm, grid.length_step_um);
  const unsigned length_50_idx = realtech_support::MakeLengthIndex(realtech_support::kExactMidLevelLengthUm, grid.length_step_um);
  const unsigned length_100_idx = realtech_support::MakeLengthIndex(realtech_support::kExactRootLevelLengthUm, grid.length_step_um);

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), segment_context);
  ASSERT_TRUE(frontier_by_length.contains(length_25_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_50_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_100_idx));
  ASSERT_FALSE(frontier_by_length.at(length_25_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_50_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_100_idx).empty());

  auto exact_segment_100_raw = realtech_support::ComposeSegmentEntriesExact(frontier_by_length.at(length_50_idx),
                                                                            frontier_by_length.at(length_50_idx), segment_context);
  ASSERT_FALSE(exact_segment_100_raw.empty());
  auto exact_segment_100_frontier = realtech_support::BuildSegmentStateFrontier(exact_segment_100_raw, segment_context);
  ASSERT_FALSE(exact_segment_100_frontier.empty());
  EXPECT_TRUE(std::ranges::all_of(exact_segment_100_frontier, [length_100_idx](const icts::SegmentChar& entry) -> bool {
    return entry.get_length_idx() == length_100_idx;
  }));

  realtech_support::HTreeFrontierContext htree_context;

  realtech_support::HTreeStageSummary leaf_stage;
  leaf_stage.label = "leaf_25um";
  leaf_stage.raw_entries = realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_25_idx), segment_context, htree_context);
  leaf_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(leaf_stage.raw_entries, htree_context);
  ASSERT_FALSE(leaf_stage.frontier_entries.empty());

  realtech_support::HTreeStageSummary mid_stage;
  mid_stage.label = "mid_50um_to_25um";
  mid_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
      realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_50_idx), segment_context, htree_context),
      leaf_stage.frontier_entries, htree_context);
  mid_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(mid_stage.raw_entries, htree_context);
  ASSERT_FALSE(mid_stage.frontier_entries.empty());

  realtech_support::HTreeStageSummary root_stage;
  root_stage.label = "root_100um_to_50um_to_25um";
  root_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
      realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_100_idx), segment_context, htree_context),
      mid_stage.frontier_entries, htree_context);
  root_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(root_stage.raw_entries, htree_context);
  ASSERT_FALSE(root_stage.frontier_entries.empty());

  EXPECT_GE(exact_segment_100_raw.size(), exact_segment_100_frontier.size());
  EXPECT_GE(mid_stage.raw_entries.size(), mid_stage.frontier_entries.size());
  EXPECT_GE(root_stage.raw_entries.size(), root_stage.frontier_entries.size());

  const auto best_exact_htree = realtech_support::SelectBestHTreeChar(root_stage.frontier_entries);
  if (!best_exact_htree.has_value()) {
    GTEST_FAIL() << "Failed to select exact H-tree characterization entry.";
    return;
  }

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(3);
  report_stream << "scenario=exact_regression\n";
  report_stream << "usable_buffers=" << realtech_support::JoinStrings(usable_buffers) << "\n";
  report_stream << "segment_char_lattice=" << realtech_support::FormatSegmentCharLatticeSummary(lattice_summary, builder) << "\n";
  report_stream << "observed_sample_bounds{output_slew_overflow_samples=" << builder.get_output_slew_overflow_samples()
                << ",max_observed_output_slew_ns=" << builder.get_max_observed_output_slew_ns()
                << ",max_observed_output_slew_idx=" << builder.get_max_observed_output_slew_idx()
                << ",driven_cap_overflow_samples=" << builder.get_driven_cap_overflow_samples()
                << ",max_observed_driven_cap_pf=" << builder.get_max_observed_driven_cap_pf()
                << ",max_observed_driven_cap_idx=" << builder.get_max_observed_driven_cap_idx() << "}\n";
  report_stream << "exact_segment_compose{lhs=50um,rhs=50um,target=100um,raw_count=" << exact_segment_100_raw.size()
                << ",frontier_count=" << exact_segment_100_frontier.size() << "}\n";
  report_stream << "exact_htree_frontier_counts{leaf=" << leaf_stage.frontier_entries.size() << ",mid=" << mid_stage.frontier_entries.size()
                << ",root=" << root_stage.frontier_entries.size() << "}\n";
  realtech_support::AppendExamples(
      report_stream, "exact_segment_100_example=", exact_segment_100_frontier,
      [&](const icts::SegmentChar& entry) -> std::string { return realtech_support::FormatSegmentChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "exact_htree_leaf_example=", leaf_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "exact_htree_mid_example=", mid_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "exact_htree_root_example=", root_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  report_stream << "best_exact_htree_char=" << realtech_support::FormatHTreeChar(best_exact_htree.value(), grid) << "\n";

  ASSERT_TRUE(realtech_support::WriteScenarioLog("exact_regression", "exact_regression_report.txt", report_stream.str()));
}

TEST(CharacterizationRealTechExactRegressionTest, IterOneFitAndComposedFrontierGapReport)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("iter1_fit_compose_gap", std::nullopt, 0.0, 0.0); prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  icts::CharBuilder builder;
  const auto init_options = MakeIterOneExperimentCharBuilderInitOptions();
  builder.init(init_options);
  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  auto direct_segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);

  auto direct_frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), direct_segment_context);
  ASSERT_TRUE(direct_frontier_by_length.contains(1U));
  ASSERT_TRUE(direct_frontier_by_length.contains(2U));
  ASSERT_TRUE(direct_frontier_by_length.contains(3U));
  ASSERT_FALSE(direct_frontier_by_length.at(1U).empty());
  ASSERT_FALSE(direct_frontier_by_length.at(2U).empty());
  ASSERT_FALSE(direct_frontier_by_length.at(3U).empty());

  auto iter_one_segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  std::unordered_map<unsigned, std::vector<icts::SegmentChar>> iter_one_frontier_by_length;
  iter_one_frontier_by_length[1U] = direct_frontier_by_length.at(1U);
  ASSERT_TRUE(realtech_support::SynthesizeSegmentFrontierExactOnly(iter_one_frontier_by_length, 2U, iter_one_segment_context));
  ASSERT_TRUE(realtech_support::SynthesizeSegmentFrontierExactOnly(iter_one_frontier_by_length, 3U, iter_one_segment_context));
  ASSERT_FALSE(iter_one_frontier_by_length.at(2U).empty());
  ASSERT_FALSE(iter_one_frontier_by_length.at(3U).empty());

  const auto length_two_gap = CompareComposedFrontierToDirect(2U, direct_frontier_by_length.at(2U), iter_one_frontier_by_length.at(2U),
                                                              iter_one_segment_context, grid);
  const auto length_three_gap = CompareComposedFrontierToDirect(3U, direct_frontier_by_length.at(3U), iter_one_frontier_by_length.at(3U),
                                                                iter_one_segment_context, grid);
  ASSERT_GT(length_two_gap.matched_count, 0U);
  ASSERT_GT(length_three_gap.matched_count, 0U);

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::scientific, std::ostringstream::floatfield);
  report_stream << std::setprecision(12);
  report_stream << "scenario=iter1_fit_compose_gap\n";
  report_stream << "wirelength_unit_um=" << grid.length_step_um << "\n";
  report_stream << "slew_step_ns=" << grid.slew_step_ns << "\n";
  report_stream << "cap_step_pf=" << grid.cap_step_pf << "\n";
  report_stream << "wirelength_iterations=" << builder.get_wirelength_iterations() << "\n";
  report_stream << "raw_segment_char_count=" << builder.get_segment_chars().size() << "\n";
  report_stream << "direct_frontier_count{length_idx=1,count=" << direct_frontier_by_length.at(1U).size() << "}\n";
  report_stream << "direct_frontier_count{length_idx=2,count=" << direct_frontier_by_length.at(2U).size() << "}\n";
  report_stream << "direct_frontier_count{length_idx=3,count=" << direct_frontier_by_length.at(3U).size() << "}\n";
  report_stream << "iter1_composed_frontier_count{length_idx=2,count=" << iter_one_frontier_by_length.at(2U).size() << "}\n";
  report_stream << "iter1_composed_frontier_count{length_idx=3,count=" << iter_one_frontier_by_length.at(3U).size() << "}\n";
  AppendIterOneFitReport(report_stream, builder.get_segment_chars(), grid, 1U);
  AppendComposeGapStats(report_stream, length_two_gap, grid);
  AppendComposeGapStats(report_stream, length_three_gap, grid);

  ASSERT_TRUE(realtech_support::WriteScenarioLog("iter1_fit_compose_gap", "iter1_fit_compose_gap_report.txt", report_stream.str()));
}

TEST(CharacterizationRealTechExactRegressionTest, IterOneFunctionComposeGapReport)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("iter1_function_compose_gap", std::nullopt, 0.0, 0.0); prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  icts::CharBuilder builder;
  const auto init_options = MakeIterOneExperimentCharBuilderInitOptions();
  builder.init(init_options);
  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  auto segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);
  ASSERT_GT(grid.slew_step_ns, 0.0);
  ASSERT_GT(grid.cap_step_pf, 0.0);

  auto direct_frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), segment_context);
  ASSERT_TRUE(direct_frontier_by_length.contains(2U));
  ASSERT_TRUE(direct_frontier_by_length.contains(3U));
  ASSERT_FALSE(direct_frontier_by_length.at(2U).empty());
  ASSERT_FALSE(direct_frontier_by_length.at(3U).empty());

  const auto linear_models = BuildFunctionalSurfaceModels(builder.get_segment_chars(), segment_context, grid, FitBasisKind::kLinear);
  const auto quadratic_models = BuildFunctionalSurfaceModels(builder.get_segment_chars(), segment_context, grid, FitBasisKind::kQuadratic);
  ASSERT_FALSE(linear_models.empty());
  ASSERT_FALSE(quadratic_models.empty());

  const auto linear_raw_length_two
      = AnalyzeFunctionComposeGap("raw", 2U, FitBasisKind::kLinear, builder.get_segment_chars(), segment_context, linear_models, grid,
                                  builder.get_max_slew(), builder.get_max_cap());
  const auto linear_raw_length_three
      = AnalyzeFunctionComposeGap("raw", 3U, FitBasisKind::kLinear, builder.get_segment_chars(), segment_context, linear_models, grid,
                                  builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_raw_length_two
      = AnalyzeFunctionComposeGap("raw", 2U, FitBasisKind::kQuadratic, builder.get_segment_chars(), segment_context, quadratic_models, grid,
                                  builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_raw_length_three
      = AnalyzeFunctionComposeGap("raw", 3U, FitBasisKind::kQuadratic, builder.get_segment_chars(), segment_context, quadratic_models, grid,
                                  builder.get_max_slew(), builder.get_max_cap());
  const auto linear_frontier_length_two
      = AnalyzeFunctionComposeGap("frontier", 2U, FitBasisKind::kLinear, direct_frontier_by_length.at(2U), segment_context, linear_models,
                                  grid, builder.get_max_slew(), builder.get_max_cap());
  const auto linear_frontier_length_three
      = AnalyzeFunctionComposeGap("frontier", 3U, FitBasisKind::kLinear, direct_frontier_by_length.at(3U), segment_context, linear_models,
                                  grid, builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_frontier_length_two
      = AnalyzeFunctionComposeGap("frontier", 2U, FitBasisKind::kQuadratic, direct_frontier_by_length.at(2U), segment_context,
                                  quadratic_models, grid, builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_frontier_length_three
      = AnalyzeFunctionComposeGap("frontier", 3U, FitBasisKind::kQuadratic, direct_frontier_by_length.at(3U), segment_context,
                                  quadratic_models, grid, builder.get_max_slew(), builder.get_max_cap());

  ASSERT_GT(linear_raw_length_two.evaluated_count, 0U);
  ASSERT_GT(linear_raw_length_three.evaluated_count, 0U);
  ASSERT_GT(quadratic_raw_length_two.evaluated_count, 0U);
  ASSERT_GT(quadratic_raw_length_three.evaluated_count, 0U);
  ASSERT_GT(linear_frontier_length_two.evaluated_count, 0U);
  ASSERT_GT(linear_frontier_length_three.evaluated_count, 0U);
  ASSERT_GT(quadratic_frontier_length_two.evaluated_count, 0U);
  ASSERT_GT(quadratic_frontier_length_three.evaluated_count, 0U);

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::scientific, std::ostringstream::floatfield);
  report_stream << std::setprecision(12);
  report_stream << "scenario=iter1_function_compose_gap\n";
  report_stream << "wirelength_unit_um=" << grid.length_step_um << "\n";
  report_stream << "slew_step_ns=" << grid.slew_step_ns << "\n";
  report_stream << "cap_step_pf=" << grid.cap_step_pf << "\n";
  report_stream << "wirelength_iterations=" << builder.get_wirelength_iterations() << "\n";
  report_stream << "raw_segment_char_count=" << builder.get_segment_chars().size() << "\n";
  report_stream << "direct_frontier_count{length_idx=2,count=" << direct_frontier_by_length.at(2U).size() << "}\n";
  report_stream << "direct_frontier_count{length_idx=3,count=" << direct_frontier_by_length.at(3U).size() << "}\n";
  report_stream << "function_model_count{basis=linear,count=" << linear_models.size() << "}\n";
  report_stream << "function_model_count{basis=quadratic,count=" << quadratic_models.size() << "}\n";
  AppendFunctionComposeGapStats(report_stream, linear_raw_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, linear_raw_length_three, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_raw_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_raw_length_three, grid);
  AppendFunctionComposeGapStats(report_stream, linear_frontier_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, linear_frontier_length_three, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_frontier_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_frontier_length_three, grid);

  ASSERT_TRUE(
      realtech_support::WriteScenarioLog("iter1_function_compose_gap", "iter1_function_compose_gap_report.txt", report_stream.str()));
}

TEST(CharacterizationRealTechExactRegressionTest, IterOneStructuralCapFunctionComposeGapReport)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("iter1_structural_cap_function_compose_gap", std::nullopt, 0.0, 0.0);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  icts::CharBuilder builder;
  const auto init_options = MakeIterOneExperimentCharBuilderInitOptions();
  builder.init(init_options);
  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  auto segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);
  ASSERT_GT(grid.slew_step_ns, 0.0);
  ASSERT_GT(grid.cap_step_pf, 0.0);

  auto direct_frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), segment_context);
  ASSERT_TRUE(direct_frontier_by_length.contains(2U));
  ASSERT_TRUE(direct_frontier_by_length.contains(3U));
  ASSERT_FALSE(direct_frontier_by_length.at(2U).empty());
  ASSERT_FALSE(direct_frontier_by_length.at(3U).empty());

  const auto linear_models = BuildFunctionalSurfaceModels(builder.get_segment_chars(), segment_context, grid, FitBasisKind::kLinear);
  const auto quadratic_models = BuildFunctionalSurfaceModels(builder.get_segment_chars(), segment_context, grid, FitBasisKind::kQuadratic);
  const auto cap_operators = BuildPhysicalStructuralCapOperators(segment_context, init_options, grid);
  ASSERT_FALSE(linear_models.empty());
  ASSERT_FALSE(quadratic_models.empty());
  ASSERT_FALSE(cap_operators.empty());

  const auto linear_raw_length_two = AnalyzeStructuralCapFunctionComposeGap(
      "raw_structural_cap", 2U, FitBasisKind::kLinear, builder.get_segment_chars(), segment_context, linear_models, cap_operators, grid,
      builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());
  const auto linear_raw_length_three = AnalyzeStructuralCapFunctionComposeGap(
      "raw_structural_cap", 3U, FitBasisKind::kLinear, builder.get_segment_chars(), segment_context, linear_models, cap_operators, grid,
      builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_raw_length_two = AnalyzeStructuralCapFunctionComposeGap(
      "raw_structural_cap", 2U, FitBasisKind::kQuadratic, builder.get_segment_chars(), segment_context, quadratic_models, cap_operators,
      grid, builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_raw_length_three = AnalyzeStructuralCapFunctionComposeGap(
      "raw_structural_cap", 3U, FitBasisKind::kQuadratic, builder.get_segment_chars(), segment_context, quadratic_models, cap_operators,
      grid, builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());
  const auto linear_frontier_length_two = AnalyzeStructuralCapFunctionComposeGap(
      "frontier_structural_cap", 2U, FitBasisKind::kLinear, direct_frontier_by_length.at(2U), segment_context, linear_models, cap_operators,
      grid, builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());
  const auto linear_frontier_length_three = AnalyzeStructuralCapFunctionComposeGap(
      "frontier_structural_cap", 3U, FitBasisKind::kLinear, direct_frontier_by_length.at(3U), segment_context, linear_models, cap_operators,
      grid, builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_frontier_length_two = AnalyzeStructuralCapFunctionComposeGap(
      "frontier_structural_cap", 2U, FitBasisKind::kQuadratic, direct_frontier_by_length.at(2U), segment_context, quadratic_models,
      cap_operators, grid, builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());
  const auto quadratic_frontier_length_three = AnalyzeStructuralCapFunctionComposeGap(
      "frontier_structural_cap", 3U, FitBasisKind::kQuadratic, direct_frontier_by_length.at(3U), segment_context, quadratic_models,
      cap_operators, grid, builder.get_cap_lattice(), builder.get_max_slew(), builder.get_max_cap());

  ASSERT_GT(linear_raw_length_two.evaluated_count, 0U);
  ASSERT_GT(linear_raw_length_three.evaluated_count, 0U);
  ASSERT_GT(quadratic_raw_length_two.evaluated_count, 0U);
  ASSERT_GT(quadratic_raw_length_three.evaluated_count, 0U);
  ASSERT_GT(linear_frontier_length_two.evaluated_count, 0U);
  ASSERT_GT(linear_frontier_length_three.evaluated_count, 0U);
  ASSERT_GT(quadratic_frontier_length_two.evaluated_count, 0U);
  ASSERT_GT(quadratic_frontier_length_three.evaluated_count, 0U);

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::scientific, std::ostringstream::floatfield);
  report_stream << std::setprecision(12);
  report_stream << "scenario=iter1_structural_cap_function_compose_gap\n";
  report_stream << "wirelength_unit_um=" << grid.length_step_um << "\n";
  report_stream << "slew_step_ns=" << grid.slew_step_ns << "\n";
  report_stream << "cap_step_pf=" << grid.cap_step_pf << "\n";
  report_stream << "wirelength_iterations=" << builder.get_wirelength_iterations() << "\n";
  report_stream << "raw_segment_char_count=" << builder.get_segment_chars().size() << "\n";
  report_stream << "direct_frontier_count{length_idx=2,count=" << direct_frontier_by_length.at(2U).size() << "}\n";
  report_stream << "direct_frontier_count{length_idx=3,count=" << direct_frontier_by_length.at(3U).size() << "}\n";
  report_stream << "function_model_count{basis=linear,count=" << linear_models.size() << "}\n";
  report_stream << "function_model_count{basis=quadratic,count=" << quadratic_models.size() << "}\n";
  AppendStructuralCapOperatorStats(report_stream, cap_operators);
  AppendStructuralCapOperatorSampleGap(report_stream, cap_operators, builder.get_segment_chars(), segment_context, grid,
                                       builder.get_cap_lattice());
  AppendFunctionComposeGapStats(report_stream, linear_raw_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, linear_raw_length_three, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_raw_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_raw_length_three, grid);
  AppendFunctionComposeGapStats(report_stream, linear_frontier_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, linear_frontier_length_three, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_frontier_length_two, grid);
  AppendFunctionComposeGapStats(report_stream, quadratic_frontier_length_three, grid);

  ASSERT_TRUE(realtech_support::WriteScenarioLog("iter1_structural_cap_function_compose_gap",
                                                 "iter1_structural_cap_function_compose_gap_report.txt", report_stream.str()));
}

TEST(CharacterizationRealTechExactRegressionTest, ExactComposePowerAccountingProducesComparableDirectReport)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("exact_compose_power_accounting", std::nullopt, 0.0, 0.0);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  icts::CharBuilder builder;
  builder.init(MakeExactRegressionCharBuilderInitOptions());
  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  auto segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);

  const unsigned length_50_idx = realtech_support::MakeLengthIndex(realtech_support::kExactMidLevelLengthUm, grid.length_step_um);
  const unsigned length_100_idx = realtech_support::MakeLengthIndex(realtech_support::kExactRootLevelLengthUm, grid.length_step_um);

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), segment_context);
  ASSERT_TRUE(frontier_by_length.contains(length_50_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_100_idx));
  ASSERT_FALSE(frontier_by_length.at(length_50_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_100_idx).empty());

  auto exact_segment_100_raw = realtech_support::ComposeSegmentEntriesExact(frontier_by_length.at(length_50_idx),
                                                                            frontier_by_length.at(length_50_idx), segment_context);
  ASSERT_FALSE(exact_segment_100_raw.empty());
  auto exact_segment_100_frontier = realtech_support::BuildSegmentStateFrontier(exact_segment_100_raw, segment_context);
  ASSERT_FALSE(exact_segment_100_frontier.empty());

  struct CompareKey
  {
    std::string pattern_signature;
    unsigned input_slew_idx = 0U;
    unsigned output_slew_idx = 0U;
    unsigned driven_cap_idx = 0U;
    unsigned load_cap_idx = 0U;

    auto operator==(const CompareKey& rhs) const -> bool = default;
  };

  struct CompareKeyHash
  {
    auto operator()(const CompareKey& key) const -> std::size_t
    {
      std::size_t seed = std::hash<std::string>{}(key.pattern_signature);
      seed ^= std::hash<unsigned>{}(key.input_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      seed ^= std::hash<unsigned>{}(key.output_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      seed ^= std::hash<unsigned>{}(key.driven_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      seed ^= std::hash<unsigned>{}(key.load_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      return seed;
    }
  };

  auto make_pattern_signature = [&segment_context](icts::PatternId pattern_id) -> std::string {
    const auto pattern_it = segment_context.patterns.find(pattern_id);
    if (pattern_it == segment_context.patterns.end()) {
      return {};
    }

    const auto& pattern = pattern_it->second;
    std::ostringstream stream;
    stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    stream << std::setprecision(6);
    stream << "len=" << pattern.get_length_idx();
    stream << ";terminal=" << (pattern.hasTerminalBranchBuffer() ? 1 : 0);
    stream << ";src_has=" << (pattern.get_monotonic_boundary_state().source.has_buffer ? 1 : 0);
    stream << ";src_rank=" << pattern.get_monotonic_boundary_state().source.strength_rank;
    stream << ";snk_has=" << (pattern.get_monotonic_boundary_state().sink.has_buffer ? 1 : 0);
    stream << ";snk_rank=" << pattern.get_monotonic_boundary_state().sink.strength_rank;
    stream << ";pos=";
    for (double pos : pattern.get_buffer_positions()) {
      stream << pos << ",";
    }
    stream << ";masters=";
    for (const auto& cell_master : pattern.get_cell_masters()) {
      stream << cell_master << ",";
    }
    return stream.str();
  };

  auto make_compare_key = [&make_pattern_signature](const icts::SegmentChar& entry) -> CompareKey {
    return CompareKey{
        .pattern_signature = make_pattern_signature(entry.get_pattern_id()),
        .input_slew_idx = entry.get_input_slew_idx(),
        .output_slew_idx = entry.get_output_slew_idx(),
        .driven_cap_idx = entry.get_driven_cap_idx(),
        .load_cap_idx = entry.get_load_cap_idx(),
    };
  };

  std::unordered_map<CompareKey, icts::SegmentChar, CompareKeyHash> direct_entries;
  direct_entries.reserve(frontier_by_length.at(length_100_idx).size());
  for (const auto& entry : frontier_by_length.at(length_100_idx)) {
    direct_entries.emplace(make_compare_key(entry), entry);
  }

  std::size_t matched_entry_count = 0U;
  std::size_t exact_lower_power_count = 0U;
  std::size_t exact_higher_power_count = 0U;
  std::size_t exact_lower_delay_count = 0U;
  std::size_t exact_higher_delay_count = 0U;
  double sum_direct_power_w = 0.0;
  double sum_exact_power_w = 0.0;
  double sum_direct_delay_ns = 0.0;
  double sum_exact_delay_ns = 0.0;
  double worst_power_underestimate_w = 0.0;
  double worst_delay_underestimate_ns = 0.0;
  std::string worst_power_example;
  std::string worst_delay_example;

  for (const auto& exact_entry : exact_segment_100_frontier) {
    const auto direct_it = direct_entries.find(make_compare_key(exact_entry));
    if (direct_it == direct_entries.end()) {
      continue;
    }

    const auto& direct_entry = direct_it->second;
    ++matched_entry_count;
    sum_direct_power_w += direct_entry.get_power();
    sum_exact_power_w += exact_entry.get_power();
    sum_direct_delay_ns += direct_entry.get_delay();
    sum_exact_delay_ns += exact_entry.get_delay();

    const double power_delta_w = exact_entry.get_power() - direct_entry.get_power();
    if (power_delta_w < -1e-15) {
      ++exact_lower_power_count;
      const double power_underestimate_w = -power_delta_w;
      if (power_underestimate_w > worst_power_underestimate_w) {
        worst_power_underestimate_w = power_underestimate_w;
        std::ostringstream example;
        example << "key{input_slew_idx=" << exact_entry.get_input_slew_idx() << ",output_slew_idx=" << exact_entry.get_output_slew_idx()
                << ",driven_cap_idx=" << exact_entry.get_driven_cap_idx() << ",load_cap_idx=" << exact_entry.get_load_cap_idx()
                << "} direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
                << " exact=" << realtech_support::FormatSegmentChar(exact_entry, grid);
        worst_power_example = example.str();
      }
    } else if (power_delta_w > 1e-15) {
      ++exact_higher_power_count;
    }

    const double delay_delta_ns = exact_entry.get_delay() - direct_entry.get_delay();
    if (delay_delta_ns < -1e-15) {
      ++exact_lower_delay_count;
      const double delay_underestimate_ns = -delay_delta_ns;
      if (delay_underestimate_ns > worst_delay_underestimate_ns) {
        worst_delay_underestimate_ns = delay_underestimate_ns;
        std::ostringstream example;
        example << "key{input_slew_idx=" << exact_entry.get_input_slew_idx() << ",output_slew_idx=" << exact_entry.get_output_slew_idx()
                << ",driven_cap_idx=" << exact_entry.get_driven_cap_idx() << ",load_cap_idx=" << exact_entry.get_load_cap_idx()
                << "} direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
                << " exact=" << realtech_support::FormatSegmentChar(exact_entry, grid);
        worst_delay_example = example.str();
      }
    } else if (delay_delta_ns > 1e-15) {
      ++exact_higher_delay_count;
    }
  }

  ASSERT_GT(matched_entry_count, 0U);

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::scientific, std::ostringstream::floatfield);
  report_stream << std::setprecision(12);
  report_stream << "scenario=exact_compose_power_accounting\n";
  report_stream << "direct_length_100_frontier_count=" << frontier_by_length.at(length_100_idx).size() << "\n";
  report_stream << "exact_compose_length_100_frontier_count=" << exact_segment_100_frontier.size() << "\n";
  report_stream << "matched_entry_count=" << matched_entry_count << "\n";
  report_stream << "exact_lower_power_count=" << exact_lower_power_count << "\n";
  report_stream << "exact_higher_power_count=" << exact_higher_power_count << "\n";
  report_stream << "exact_lower_delay_count=" << exact_lower_delay_count << "\n";
  report_stream << "exact_higher_delay_count=" << exact_higher_delay_count << "\n";
  report_stream << "sum_direct_power_w=" << sum_direct_power_w << "\n";
  report_stream << "sum_exact_power_w=" << sum_exact_power_w << "\n";
  report_stream << "sum_power_delta_w=" << (sum_exact_power_w - sum_direct_power_w) << "\n";
  report_stream << "power_ratio_exact_over_direct=" << (sum_direct_power_w > 0.0 ? (sum_exact_power_w / sum_direct_power_w) : 0.0) << "\n";
  report_stream << "sum_direct_delay_ns=" << sum_direct_delay_ns << "\n";
  report_stream << "sum_exact_delay_ns=" << sum_exact_delay_ns << "\n";
  report_stream << "sum_delay_delta_ns=" << (sum_exact_delay_ns - sum_direct_delay_ns) << "\n";
  report_stream << "delay_ratio_exact_over_direct=" << (sum_direct_delay_ns > 0.0 ? (sum_exact_delay_ns / sum_direct_delay_ns) : 0.0)
                << "\n";
  report_stream << "worst_power_underestimate_w=" << worst_power_underestimate_w << "\n";
  report_stream << "worst_power_underestimate_example=" << worst_power_example << "\n";
  report_stream << "worst_delay_underestimate_ns=" << worst_delay_underestimate_ns << "\n";
  report_stream << "worst_delay_underestimate_example=" << worst_delay_example << "\n";

  ASSERT_TRUE(realtech_support::WriteScenarioLog("exact_compose_power_accounting", "exact_compose_power_accounting_report.txt",
                                                 report_stream.str()));
}

}  // namespace
}  // namespace icts_test
