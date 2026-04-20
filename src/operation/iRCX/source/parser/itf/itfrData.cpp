#include "itfrData.hpp"

#include "itfMarco.h"

namespace itf
{

itfrData* itfData = nullptr;

itfrData::itfrData()
: itf_file(nullptr),
  log_file(nullptr),

  process_name(nullptr),
  process_foundry(nullptr),
  process_node(0),
  process_type(nullptr),
  process_version(0),
  process_corner(nullptr),
  reference_direction(nullptr),
  global_temperature(25.f),
  background_er(1.f),
  half_node_scale_factor(1.f),
  drop_factor_lateral_spacing(.5f),
  
  dielectric(),
  conductor(),
  via(),
  variation_param(),

  use_si_density(0),
  has_open_log_file(0),
  has_global_temperature(0),
  has_background_er(0),
  has_half_node_scale_factor(0),
  has_use_si_density(0),
  has_drop_factor_lateral_spacing(0),
  has_variation_params(0)
{

}

itfrData::~itfrData()
{
  log_file = nullptr;  // not release here
  
  ITF_FREE(itf_file);
  ITF_FREE(process_name);
  ITF_FREE(process_foundry);
}

void itfrData::reset() {
  if (itfData) {
    delete itfData;
  }

  itfData = new itfrData();
}

void itfrData::initRead() {
  
}

} // namespace itf