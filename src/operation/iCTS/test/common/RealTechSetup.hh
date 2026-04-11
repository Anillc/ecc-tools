// compatibility header
#pragma once

#include <cstddef>
#include <string>

#include "realtech/support/RealTechSetupSupport.hh"
#include "types/TestDataTypes.hh"

namespace icts_test {

using RealTechMode = common::realtech::RealTechMode;
using RealTechSetupState = common::realtech::RealTechSetupState;

auto EnsureRealTechSetup() -> const RealTechSetupState&;
auto MakeRealTechOrSyntheticLoads(std::size_t target_count, unsigned seed, std::string& source_label) -> GeneratedPins;

}  // namespace icts_test
