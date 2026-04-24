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
 * @file PatternResponseModel.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Per-pattern fitted response models for numerical characterization.
 */

#pragma once

#include <vector>

#include "PatternId.hh"
#include "numerical_characterization/Polynomial2D.hh"

namespace icts {

struct NumericalSample;

enum class NumericalResponseMetric
{
  kDelay,
  kOutputSlew,
  kPower,
  kDrivenCap,
  kSourceBoundarySwitchPower
};

struct NumericalResponse
{
  double delay_ns = 0.0;
  double slew_out_ns = 0.0;
  double power_w = 0.0;
  double driven_cap_pf = 0.0;
  double source_boundary_net_switch_power_w = 0.0;
};

/**
 * @brief Fitted delay/slew/power/cap response for one segment pattern and length.
 */
class PatternResponseModel
{
 public:
  PatternResponseModel() = default;

  static auto fit(PatternId pattern_id, unsigned length_idx, const std::vector<NumericalSample>& samples,
                  const PolynomialFitOptions& options = {}) -> PatternResponseModel;

  auto evaluate(double slew_in_ns, double cap_load_pf) const -> NumericalResponse;

  auto isValid() const -> bool;
  auto get_pattern_id() const -> PatternId { return _pattern_id; }
  auto get_length_idx() const -> unsigned { return _length_idx; }
  auto get_length_um() const -> double { return _length_um; }
  auto get_sample_count() const -> unsigned { return _sample_count; }
  auto get_delay_fit() const -> const Polynomial2DFitResult& { return _delay_fit; }
  auto get_output_slew_fit() const -> const Polynomial2DFitResult& { return _output_slew_fit; }
  auto get_power_fit() const -> const Polynomial2DFitResult& { return _power_fit; }
  auto get_driven_cap_fit() const -> const Polynomial2DFitResult& { return _driven_cap_fit; }
  auto get_source_boundary_switch_power_fit() const -> const Polynomial2DFitResult& { return _source_boundary_switch_power_fit; }

 private:
  PatternId _pattern_id = PatternId::segment(0U);
  unsigned _length_idx = 0U;
  double _length_um = 0.0;
  unsigned _sample_count = 0U;
  Polynomial2DFitResult _delay_fit;
  Polynomial2DFitResult _output_slew_fit;
  Polynomial2DFitResult _power_fit;
  Polynomial2DFitResult _driven_cap_fit;
  Polynomial2DFitResult _source_boundary_switch_power_fit;
};

}  // namespace icts
