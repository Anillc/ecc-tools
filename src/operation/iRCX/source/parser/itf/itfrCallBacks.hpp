#pragma once

#include "itfrReader.hpp"

namespace itf
{
  
class itfrCallBacks {
 public:
  // constructor
  itfrCallBacks();

  // function
  static void reset();

  // members
  itfrStringCbFnType technology_cb;
  itfrStringCbFnType process_foundry_cb;
  itfrIntegerCbFnType process_node_cb;
  itfrStringCbFnType process_type_cb;
  itfrDoubleCbFnType process_version_cb;
  itfrStringCbFnType process_corner_cb;
  itfrStringCbFnType reference_direction_cb;
  itfrDoubleCbFnType global_temperature_cb;
  itfrDoubleCbFnType background_er_cb;
  itfrDoubleCbFnType half_node_scale_factor_cb;
  itfrIntegerCbFnType use_si_density_cb;
  itfrDoubleCbFnType drop_factor_lateral_spacing_cb;
  itfrConductorCbFnType conductor_cb;
  itfrDielectricCbFnType dielectric_cb;
  itfrViaCbFnType via_cb;
  itfrVariationParamCbFnType variation_cb;
};

extern itfrCallBacks* itfCallbacks;

} // namespace itf
