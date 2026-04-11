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
 * @file RealTechSetupSupport.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Public facade for shared real-tech setup helpers used by linear-clustering tests.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "common/types/TestDataTypes.hh"

namespace icts_test::common::realtech {

enum class RealTechMode : std::uint8_t
{
  kRealTech,
  kSyntheticFallback
};

struct RealTechSetupState
{
  RealTechMode mode = RealTechMode::kSyntheticFallback;
  bool assets_available = false;
  bool setup_succeeded = false;
  std::filesystem::path output_dir;
  std::filesystem::path config_path;
  std::string source_label;
  std::string summary;
};

auto EnsureRealTechSetup() -> const RealTechSetupState&;
auto MakeRealTechOrSyntheticLoads(std::size_t target_count, unsigned seed, std::string& source_label) -> GeneratedPins;

}  // namespace icts_test::common::realtech
