#pragma once 

#include <string.h>

#include <iostream>

#include "itfiConductor.hpp"
#include "itfiDielectric.hpp"
#include "itfiVia.hpp"
#include "itfiVariationParam.hpp"

namespace itf
{

// refers to StarRC User Guide. Version F-2011.06, June 2011     # 2022-12-06 # 
// refers to StarRC User Guide. Version U-2022.12, December 2022 # 2024-01-18 # add some feature in 14 nm
class itfrData {
 public:
  // constructor
  itfrData();
  ~itfrData();

  // function
  static void reset();
  void initRead();

  // members
  char* itf_file;
  FILE* log_file;

  char* process_name;
  char* process_foundry;
  int   process_node;
  char* process_type;
  float process_version;
  char* process_corner;
  char* reference_direction;
  float global_temperature;
  float background_er;  // Relative permittivity
  float half_node_scale_factor;
  float drop_factor_lateral_spacing;  // Units: microns

  itfiDielectric dielectric;
  itfiConductor conductor;
  itfiVia via;
  itfiVariationParam variation_param;

  unsigned use_si_density : 1;
  unsigned has_open_log_file : 1;
  unsigned has_global_temperature : 1;
  unsigned has_background_er : 1;
  unsigned has_half_node_scale_factor : 1;
  unsigned has_use_si_density : 1;
  unsigned has_drop_factor_lateral_spacing : 1;
  unsigned has_variation_params : 1;
};

extern itfrData* itfData;

} // namespace itf
