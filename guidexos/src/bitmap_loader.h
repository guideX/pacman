#pragma once

#include <guidexos/ui.h>
#include "game_types.h"

bool load_gximg(gx_app_context* ctx, const char* path, uint32_t* destination, uint32_t capacityPixels, PacImage* outImage);
