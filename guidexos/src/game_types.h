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

enum class CellType : uint8_t {
    Wall,
    Empty,
    Pill,
    PowerPill,
    Tunnel,
    GhostHouse
};

struct LevelState {
    CellType cells[kPacManMazeRows][kPacManMazeColumns];
    uint32_t normalPillsRemaining;
    uint32_t powerPillsRemaining;
    uint32_t totalConsumablesRemaining;
};
