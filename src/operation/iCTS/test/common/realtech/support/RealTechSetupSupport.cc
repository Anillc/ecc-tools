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
 * @file RealTechSetupSupport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Facade for shared real-tech setup helpers used by linear-clustering tests.
 */

#include "common/realtech/support/RealTechSetupSupport.hh"

#include <cstddef>
#include <string>
#include <vector>

#include "common/realtech/asset/RealTechAssetLoader.hh"
#include "common/realtech/load/RealTechLoadFactory.hh"
#include "common/types/TestDataTypes.hh"
#include "utils/logger/Logger.hh"

namespace icts_test::common::realtech {

auto EnsureRealTechSetup() -> const RealTechSetupState&
{
  static const RealTechSetupState setup_state = asset::BuildRealTechSetupState();
  return setup_state;
}

auto MakeRealTechOrSyntheticLoads(std::size_t target_count, unsigned seed, std::string& source_label) -> GeneratedPins
{
  const auto& setup_state = EnsureRealTechSetup();
  if (setup_state.mode == RealTechMode::kRealTech && setup_state.setup_succeeded) {
    auto real_loads = load::MakeRealDesignLoads(target_count, source_label, seed);
    if (!real_loads.loads.empty()) {
      return real_loads;
    }
    CTS_LOG_WARNING << "RealTechSetup: real design load extraction failed, use synthetic fallback.";
  }

  return load::MakeSyntheticFallbackLoads(target_count, source_label, seed);
}

}  // namespace icts_test::common::realtech
