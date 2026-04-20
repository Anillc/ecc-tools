#include "VariationParams.hpp"

namespace itf
{
  
void
VariationParams::add_variation_param(const itfiVariationParam& vp)
{
  auto ps = new itf::itfiVariationParam(vp);
  _params.push_back(ps);
}

} // namespace itf