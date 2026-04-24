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
 * @file NumericalCharLibrary.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Grouped numerical characterization model library.
 */

#include "NumericalCharLibrary.hh"

#include <algorithm>
#include <utility>

#include "PatternId.hh"
#include "numerical_characterization/NumericalSample.hh"

namespace icts {
namespace {

struct SampleGroup
{
  PatternId pattern_id = PatternId::segment(0U);
  unsigned length_idx = 0U;
  std::vector<NumericalSample> samples;
};

auto findGroup(std::vector<SampleGroup>& groups, PatternId pattern_id, unsigned length_idx) -> SampleGroup*
{
  for (auto& group : groups) {
    if (group.pattern_id == pattern_id && group.length_idx == length_idx) {
      return &group;
    }
  }
  return nullptr;
}

auto buildSampleGroups(const std::vector<NumericalSample>& samples) -> std::vector<SampleGroup>
{
  std::vector<SampleGroup> groups;
  for (const auto& sample : samples) {
    auto* group = findGroup(groups, sample.pattern_id, sample.length_idx);
    if (group == nullptr) {
      groups.push_back(SampleGroup{.pattern_id = sample.pattern_id, .length_idx = sample.length_idx, .samples = {}});
      group = &groups.back();
    }
    group->samples.push_back(sample);
  }

  std::ranges::sort(groups, [](const SampleGroup& lhs, const SampleGroup& rhs) -> bool {
    if (lhs.length_idx != rhs.length_idx) {
      return lhs.length_idx < rhs.length_idx;
    }
    return lhs.pattern_id.pack() < rhs.pattern_id.pack();
  });
  return groups;
}

}  // namespace

auto NumericalCharLibrary::buildFromSamples(const std::vector<NumericalSample>& samples, const PolynomialFitOptions& options)
    -> NumericalCharLibrary
{
  NumericalCharLibrary library;
  const auto groups = buildSampleGroups(samples);
  for (const auto& group : groups) {
    library.addModel(PatternResponseModel::fit(group.pattern_id, group.length_idx, group.samples, options));
  }
  return library;
}

auto NumericalCharLibrary::buildFromSegmentChars(const std::vector<SegmentChar>& segment_chars, const NumericalSampleLattices& lattices,
                                                 const PolynomialFitOptions& options) -> NumericalCharLibrary
{
  return buildFromSamples(ExtractNumericalSamples(segment_chars, lattices), options);
}

auto NumericalCharLibrary::addModel(PatternResponseModel model) -> void
{
  for (auto& existing_model : _models) {
    if (existing_model.get_pattern_id() == model.get_pattern_id() && existing_model.get_length_idx() == model.get_length_idx()) {
      existing_model = std::move(model);
      return;
    }
  }
  _models.push_back(std::move(model));
}

auto NumericalCharLibrary::findModel(PatternId pattern_id, unsigned length_idx) const -> const PatternResponseModel*
{
  for (const auto& model : _models) {
    if (model.get_pattern_id() == pattern_id && model.get_length_idx() == length_idx) {
      return &model;
    }
  }
  return nullptr;
}

}  // namespace icts
