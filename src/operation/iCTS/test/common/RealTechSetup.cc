// compatibility source

#include "common/RealTechSetup.hh"

#include <cstddef>
#include <string>

#include "common/realtech/support/RealTechSetupSupport.hh"
#include "common/types/TestDataTypes.hh"

namespace icts_test {

auto EnsureRealTechSetup() -> const RealTechSetupState&
{
  return common::realtech::EnsureRealTechSetup();
}

auto MakeRealTechOrSyntheticLoads(std::size_t target_count, unsigned seed, std::string& source_label) -> GeneratedPins
{
  return common::realtech::MakeRealTechOrSyntheticLoads(target_count, seed, source_label);
}

}  // namespace icts_test
