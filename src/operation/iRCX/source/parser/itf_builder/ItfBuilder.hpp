#pragma once

#include <memory>
#include <string>

#include "ItfService.hpp"

namespace itf
{

class ItfBuilder {
 public:
  // constructor
  ItfBuilder() = default;
  ~ItfBuilder() = default;

  // getter
  ItfService* get_itf_service() const;

  // function
  void buildItf(const std::string&);

 private:
  // members
  std::unique_ptr<ItfService> _itf_service{std::make_unique<ItfService>()};
};

} // namespace itf
