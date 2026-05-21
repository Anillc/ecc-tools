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
 * @file CharacterizationLibrary.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Reusable characterization cache shared by CTS synthesis stages.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ClockRouteSegmentRc.hh"
#include "characterization/Characterization.hh"

namespace icts {

class CharacterizationLibrary
{
 public:
  struct EnsureResult
  {
    bool success = false;
    bool reused = false;
    std::string failure_reason;
  };

  CharacterizationLibrary() = default;
  ~CharacterizationLibrary() = default;
  CharacterizationLibrary(const CharacterizationLibrary&) = delete;
  CharacterizationLibrary(CharacterizationLibrary&&) noexcept = default;
  auto operator=(const CharacterizationLibrary&) -> CharacterizationLibrary& = delete;
  auto operator=(CharacterizationLibrary&&) noexcept -> CharacterizationLibrary& = default;

  auto ensure(const CharBuilder::InitOptions& options) -> EnsureResult;
  auto getCharBuilder() const -> const CharBuilder& { return _char_builder; }
  auto isReady() const -> bool { return _ready; }

  static auto buildRuntimeOptions() -> CharBuilder::InitOptions;

 private:
  struct CharacterizationCacheKey
  {
    std::optional<double> wirelength_unit_um = std::nullopt;
    std::optional<unsigned> wirelength_iterations = std::nullopt;
    std::optional<std::vector<unsigned>> wirelength_indices = std::nullopt;
    std::optional<double> max_slew_ns = std::nullopt;
    std::optional<double> max_cap_pf = std::nullopt;
    std::vector<std::string> buffer_types;
    std::vector<CharacterizationBufferCell> characterization_buffer_cells;
    std::optional<double> char_buf_redundancy_pct = std::nullopt;
    std::optional<unsigned> slew_steps = std::nullopt;
    std::optional<unsigned> cap_steps = std::nullopt;
    std::optional<int> routing_layer = std::nullopt;
    std::optional<double> wire_width = std::nullopt;
    ClockRouteSegmentRc clock_route_segment_rc;

    auto operator==(const CharacterizationCacheKey& rhs) const -> bool = default;
  };

  static auto makeCharacterizationCacheKey(const CharBuilder::InitOptions& options) -> CharacterizationCacheKey;

  CharBuilder _char_builder;
  CharacterizationCacheKey _characterization_cache_key;
  bool _ready = false;
};

}  // namespace icts
