#pragma once

#include <guidexos/app.h>
#include "game_types.h"

bool render_static_scene(const PacImage* level, const PacImage* sprites, uint32_t* framePixels, uint32_t framePixelCount);
