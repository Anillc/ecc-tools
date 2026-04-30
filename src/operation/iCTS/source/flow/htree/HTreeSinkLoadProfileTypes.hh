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
 * @file HTreeSinkLoadProfileTypes.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief H-tree sink-load-profile legality data types.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "PatternId.hh"
#include "Point.hh"
#include "ValueLattice.hh"

namespace icts {

class Pin;

namespace htree_builder {

struct CapDistributionStats
{
  std::size_t group_count = 0U;
  double cap_min_pf = 0.0;
  double cap_max_pf = 0.0;
  double cap_mean_pf = 0.0;
  double cap_median_pf = 0.0;
};

struct SinkLoadProfileBoundaryGroup
{
  std::size_t node_id = std::numeric_limits<std::size_t>::max();
  Point<int> anchor;
  const std::vector<Pin*>* loads = nullptr;
};

struct SinkLoadProfileLegalitySignature
{
  int bottom_most_buffered_level = -1;
  PatternId segment_pattern_id = PatternId::segment(0);

  auto operator==(const SinkLoadProfileLegalitySignature& rhs) const -> bool = default;
};

struct SinkLoadProfileLegalitySignatureHash
{
  auto operator()(const SinkLoadProfileLegalitySignature& signature) const noexcept -> std::size_t
  {
    std::size_t hash_value = std::hash<int>{}(signature.bottom_most_buffered_level);
    hash_value ^= std::hash<unsigned>{}(signature.segment_pattern_id.pack()) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    return hash_value;
  }
};

enum class SinkLoadProfileViolation
{
  kNone,
  kMissingTopologyRoot,
  kMissingTopologyNode,
  kMissingTopologyLevel,
  kMissingSegmentPattern,
  kMissingBufferPosition,
  kEmptyLoadGroup,
  kFanout,
  kPinCapLowerBound,
  kRoutingFailed,
  kCapacitance,
};

struct SinkLoadProfileLegalityResult
{
  bool legal = false;
  bool monotone_hard_fail = false;
  SinkLoadProfileViolation violation = SinkLoadProfileViolation::kMissingTopologyRoot;
  std::string failure_reason;
  CapDistributionStats cap_distribution;
  double required_leaf_load_cap_pf = 0.0;
  std::optional<unsigned> required_leaf_load_cap_covering_idx = std::nullopt;
  int bottom_most_buffered_level = -1;
  PatternId segment_pattern_id = PatternId::segment(0);
};

struct SinkLoadProfileLegalityContext
{
  std::unordered_map<SinkLoadProfileLegalitySignature, SinkLoadProfileLegalityResult, SinkLoadProfileLegalitySignatureHash>
      result_by_signature;
  int max_monotone_failed_level = std::numeric_limits<int>::min();
  UniformValueLattice cap_lattice;
};

}  // namespace htree_builder
}  // namespace icts
