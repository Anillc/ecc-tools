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
 * @file NumericalCharLibrary.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Grouped numerical characterization model library.
 */

#pragma once

#include <vector>

#include "numerical_characterization/PatternResponseModel.hh"

namespace icts {

class SegmentChar;
struct NumericalSample;
struct NumericalSampleLattices;
struct PatternId;
struct PolynomialFitOptions;

/**
 * @brief Collection of fitted response models keyed by segment pattern and length.
 */
class NumericalCharLibrary
{
 public:
  NumericalCharLibrary() = default;

  static auto buildFromSamples(const std::vector<NumericalSample>& samples, const PolynomialFitOptions& options = {})
      -> NumericalCharLibrary;

  static auto buildFromSegmentChars(const std::vector<SegmentChar>& segment_chars, const NumericalSampleLattices& lattices,
                                    const PolynomialFitOptions& options = {}) -> NumericalCharLibrary;

  auto addModel(PatternResponseModel model) -> void;
  auto findModel(PatternId pattern_id, unsigned length_idx) const -> const PatternResponseModel*;

  auto get_models() const -> const std::vector<PatternResponseModel>& { return _models; }
  auto size() const -> unsigned { return static_cast<unsigned>(_models.size()); }
  auto empty() const -> bool { return _models.empty(); }

 private:
  std::vector<PatternResponseModel> _models;
};

}  // namespace icts
