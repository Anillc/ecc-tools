#pragma once

#include <vector>

#include "itfiVariationParam.hpp"

namespace itf
{


class VariationParams {
 public:
  // constructor

  // getter

  // setter
  void add_variation_param(const itfiVariationParam&);

  // function

 private:
  // members
  std::vector<itfiVariationParam*> _params;
};

} // namespace itf