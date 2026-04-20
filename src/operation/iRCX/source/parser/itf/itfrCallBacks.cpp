#include "itfrCallBacks.hpp"

namespace itf
{
  
itfrCallBacks* itfCallbacks = nullptr;

itfrCallBacks::itfrCallBacks()
: technology_cb(nullptr)
, process_foundry_cb(nullptr)
, process_node_cb(0)
, process_type_cb(nullptr)
, process_version_cb(0)
, process_corner_cb(nullptr)
, reference_direction_cb(nullptr)
, global_temperature_cb(nullptr)
, background_er_cb(nullptr)
, half_node_scale_factor_cb(nullptr)
, use_si_density_cb(nullptr)
, drop_factor_lateral_spacing_cb(nullptr)
, conductor_cb(nullptr)
, dielectric_cb(nullptr)
, via_cb(nullptr)
, variation_cb(nullptr)
{

}

void
itfrCallBacks::reset()
{
  if (itfCallbacks) {
    delete itfCallbacks;
  }

  itfCallbacks = new itfrCallBacks();
}

} // namespace itf
