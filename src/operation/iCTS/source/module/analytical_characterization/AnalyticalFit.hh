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
 * @file AnalyticalFit.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Least-squares fitting helpers for analytical characterization surfaces.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "AnalyticalModel.hh"

namespace icts::analytical {

struct AnalyticalFitSample
{
  double input_slew_ns = 0.0;
  double load_cap_pf = 0.0;
  double value = 0.0;
};

struct AnalyticalFitOptions
{
  AnalyticalMetric metric = AnalyticalMetric::kDelay;
  AnalyticalModelBasis basis = AnalyticalModelBasis::kAffine;
  double max_abs_residual = 0.0;
  double max_relative_residual = 0.0;
  double bucket_size = 0.0;
  double max_bucket_residual = 0.0;
  double min_r2 = 0.0;
  double relative_floor = 1e-30;
  bool require_monotonic = false;
  double monotonic_tolerance = 1e-12;
};

struct AnalyticalFitResult
{
  bool success = false;
  std::string failure_reason;
  std::optional<AnalyticalSurfaceModel> model = std::nullopt;
};

auto FitAnalyticalSurface(const std::vector<AnalyticalFitSample>& samples, const AnalyticalFitOptions& options) -> AnalyticalFitResult;

}  // namespace icts::analytical
