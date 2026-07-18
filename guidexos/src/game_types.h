#pragma once

#include <stdint.h>

enum {
    kPacManWidth = 448,
    kPacManMazeHeight = 496,
    kPacManFrameHeight = 553,
    kPacManTileSize = 16,
    kPacManSpriteSize = 32,
    kPacManMazeRows = 31,
    kPacManMazeColumns = 28
};

struct PacImage {
    uint32_t width;
    uint32_t height;
    uint32_t strideBytes;
    const uint32_t* pixels;
};
