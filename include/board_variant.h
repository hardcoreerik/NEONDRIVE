#pragma once

#if defined(NEONDRIVE_TARGET_CYD35)
  #include "variants/cyd35/board_variant.h"
#elif defined(NEONDRIVE_TARGET_CYD28)
  #include "variants/cyd28/board_variant.h"
#else
  #include "variants/cyd24/board_variant.h"
#endif
