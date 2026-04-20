#include "itfrSettings.hpp"

namespace itf
{
  
itfrSettings* itfSettings = nullptr;

itfrSettings::itfrSettings()
: user_data(nullptr)
{ }

itfrSettings::~itfrSettings()
{
  user_data = nullptr;
}

void
itfrSettings::reset()
{
  if (itfSettings) {
    delete itfSettings;
  }

  itfSettings = new itfrSettings();
}

} // namespace itf
