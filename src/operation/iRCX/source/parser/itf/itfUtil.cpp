#include "itfUtil.h"

#include "string.h"

namespace itf
{

bool itfStrCmp(const char* s1, const char* s2)
{
  return (s1 && s2) ? strcmp(s1, s2) == 0 : s1 == s2;
}

} // namespace itf
