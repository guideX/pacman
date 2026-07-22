#pragma once

#include <stdint.h>

#include "game_types.h"

enum class Direction : int8_t {
    None = -1,
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3
};

struct HeldDirections {
    bool left;
    bool right;
    bool up;
    bool down;
};

struct PacManState {
    int x;
    int y;
    Direction direction;
    Direction facingDirection;
    Direction requestedDirection;
    int offset;
    int speed;
    int mouth;
    int mouthDirection;
    int mouthSpeed;
};

struct GameState {
    PacManState pacman;
    HeldDirections held;
    bool focused;
    bool visualDirty;
    bool turnAccepted;
    bool becameBlocked;
    bool tunnelWrapped;
    uint64_t simulationSteps;
};
