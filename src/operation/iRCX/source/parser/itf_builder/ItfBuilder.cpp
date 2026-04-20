#include "ItfBuilder.hpp"

#include "ItfRead.hpp"

namespace itf
{

ItfService*
ItfBuilder::get_itf_service() const
{
  return _itf_service.get();
}

void
ItfBuilder::buildItf(const std::string& fname)
{
  ItfRead itf_read(_itf_service.get());
  itf_read.createDb(fname);
}

} // namespace itf
