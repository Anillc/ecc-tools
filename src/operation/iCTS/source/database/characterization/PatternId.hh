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
 * @file PatternId.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Domain-tagged pattern identifier to prevent cross-domain misuse.
 */

#pragma once

#include <cstdint>
#include <functional>

namespace icts {

/**
 * @brief Pattern domain to distinguish segment patterns from topology patterns.
 */
enum class PatternDomain : uint8_t
{
  kSegmentPattern,  ///< Wire segment buffering pattern
  kTopologyPattern  ///< H-tree or other topology pattern
};

/**
 * @brief Domain-tagged pattern identifier.
 *
 * Prevents accidental misuse of pattern IDs across domains (e.g., using a
 * segment pattern ID where a topology pattern ID is expected).
 */
struct PatternId
{
  PatternDomain domain;
  unsigned local_id;

  /**
   * @brief Create a segment pattern ID.
   */
  static PatternId segment(unsigned id) { return PatternId{PatternDomain::kSegmentPattern, id}; }

  /**
   * @brief Create a topology pattern ID.
   */
  static PatternId topology(unsigned id) { return PatternId{PatternDomain::kTopologyPattern, id}; }

  bool operator==(const PatternId& other) const { return domain == other.domain && local_id == other.local_id; }

  bool operator!=(const PatternId& other) const { return !(*this == other); }

  /**
   * @brief Pack into a single unsigned for use as hash key.
   */
  unsigned pack() const { return (static_cast<unsigned>(domain) << 30) | local_id; }
};

}  // namespace icts

namespace std {

template <>
struct hash<icts::PatternId>
{
  std::size_t operator()(const icts::PatternId& pid) const noexcept { return std::hash<unsigned>{}(pid.pack()); }
};

}  // namespace std
