#pragma once

#include <stdint.h>

// basPacMan/frmPacMan keep Pacman.Level in the inclusive range 1..8.  The
// native uint32_t state uses the same saturated value instead of allowing a
// manually corrupted or very large value to wrap or underflow a timer.
static const uint32_t kPacManHistoricalMaximumLevel = 8u;
static const uint32_t kPacManHistoricalMaximumGameSpeed = 4u;

struct LevelRules {
    uint32_t level;
    uint32_t pacmanMovePixelsPerStep;
    uint32_t normalGhostMovePixelsPerStep;
    uint32_t frightenedGhostMoveIntervalSteps;
    uint32_t frightenedDurationSteps;
    uint32_t frightenedFlashStartSteps;
    uint32_t readyDurationSteps;
};

uint32_t normalize_level_number(uint32_t level);
uint32_t sanitize_game_speed(uint32_t speed);
LevelRules calculate_level_rules(uint32_t level);
