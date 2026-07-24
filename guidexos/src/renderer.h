#pragma once

#include <guidexos/app.h>
#include "game_types.h"
#include "game_state.h"

bool build_background_frame(const PacImage* level, uint32_t* backgroundPixels, uint32_t framePixelCount);
bool render_game_scene(const PacImage* sprites, const GameState* game, const uint32_t* backgroundPixels,
                       uint32_t* framePixels, uint32_t framePixelCount, uint64_t validationFrameSequence = 0);
bool render_static_scene(const PacImage* level, const PacImage* sprites, uint32_t* framePixels, uint32_t framePixelCount);
