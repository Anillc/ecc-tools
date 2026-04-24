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
 * @file FitMetrics.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Fit quality metrics for numerical characterization models.
 */

#pragma once

namespace icts {

enum class FitStatus
{
  kOk,
  kFallbackAffine,
  kFallbackConstant,
  kNoSamples,
  kUnderdetermined,
  kRankDeficient
};

inline auto FitStatusName(FitStatus status) -> const char*
{
  switch (status) {
    case FitStatus::kOk:
      return "ok";
    case FitStatus::kFallbackAffine:
      return "fallback_affine";
    case FitStatus::kFallbackConstant:
      return "fallback_constant";
    case FitStatus::kNoSamples:
      return "no_samples";
    case FitStatus::kUnderdetermined:
      return "underdetermined";
    case FitStatus::kRankDeficient:
      return "rank_deficient";
  }
  return "unknown";
}

/**
 * @brief Deterministic residual metrics for one fitted response surface.
 */
struct FitMetrics
{
  unsigned sample_count = 0U;
  unsigned rank = 0U;
  unsigned basis_term_count = 0U;
  double rmse = 0.0;
  double r2 = 0.0;
  double max_abs_error = 0.0;
  FitStatus status = FitStatus::kNoSamples;

  auto isUsable() const -> bool
  {
    return status == FitStatus::kOk || status == FitStatus::kFallbackAffine || status == FitStatus::kFallbackConstant;
  }
};

}  // namespace icts
