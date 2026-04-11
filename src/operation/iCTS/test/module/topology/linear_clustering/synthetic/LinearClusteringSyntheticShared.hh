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
 * @file LinearClusteringSyntheticShared.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Scenario runners shared by synthetic linear clustering tests.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace icts_test::linear_clustering::synthetic {

enum class DistKind : std::uint8_t
{
  kNormal,
  kMixture,
  kQuadrants
};

struct SyntheticSweepCase
{
  std::string name;
  DistKind kind = DistKind::kNormal;
  std::size_t load_count = 0;
  unsigned seed = 0;
  std::array<double, 4> quadrant_weights = {1.0, 1.0, 1.0, 1.0};
};

auto BuildSyntheticSweepCases() -> std::vector<SyntheticSweepCase>;
auto DistName(DistKind kind) -> const char*;

auto RunSyntheticSweepAndVisualization(const SyntheticSweepCase& test_case) -> void;
auto RunTightConstraintsAreLegalOrEmpty() -> void;
auto RunContinuousHilbertMatchesSinkReferenceOrder() -> void;
auto RunDensityScaledDiscreteHilbertMatchesSeparableMarginalReference() -> void;
auto RunDensityScaledContinuousHilbertMatchesSeparableMarginalReference() -> void;
auto RunRemainingOrderStrategiesProduceLegalResults() -> void;
auto RunPrefixSweepResolvesSinkStyleOffsets() -> void;
auto RunStridedSweepSamplesDistinctRingStarts() -> void;
auto RunCombinedSweepNormalizesToFullSequenceWhenAnchorWindowsCoverRing() -> void;
auto RunMaxDiameterScoringPenalizesZeroDiameterSingletons() -> void;
auto RunFanoutLadderIsMonotonic() -> void;
auto RunDiameterLadderIsMonotonic() -> void;
auto RunTighterCombinedConstraintsIncreasePressure() -> void;

}  // namespace icts_test::linear_clustering::synthetic
