#pragma once

#include <stdlib.h>
#include <string.h>

namespace itf
{

#define ITF_FREE(ptr) \
  if (ptr) {      \
    free(ptr);    \
    ptr = nullptr;\
  }               

#define ITF_STR_CPY(dst, src) \
  ITF_FREE(dst);        \
  if (src) {            \
    dst = strdup(src);  \
  }

} // namespace itf