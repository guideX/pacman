#pragma once

#include "game_state.h"

static const uint32_t kPacManLevelCompleteDelaySteps = 100u;
static const uint32_t kPacManDeathDurationSteps = 100u;
static const uint32_t kPacManReadyAfterDeathSteps = 60u;
static const uint8_t kPacManInitialLives = 3u;
static const uint32_t kPacManGhostCount = 4u;
static const uint32_t kPacManNormalPillScore = 10u;
static const uint32_t kPacManPowerPillScore = 10u;

struct GhostTarget {
    int x;
    int y;
};

void game_initialize(GameState* game);
void game_reset_level(GameState* game);
bool game_restart_session(GameState* game);
void add_score(GameState& game, uint32_t points);
bool pacman_collides_with_ghost(const PacManState& pacman, const GhostState& ghost);
const char* ghost_kind_name(GhostKind kind);
const char* ghost_release_state_name(GhostReleaseState state);
Direction choose_ghost_direction(const GameState& game, const GhostState& ghost,
                                 int targetX, int targetY);
Direction choose_pink_direction(const GameState& game, const GhostState& ghost,
                                int targetX, int targetY);
GhostTarget calculate_pink_target(const GameState& game, const PacManState& pacman);
Direction game_direction_for_key(int keyCode);
void game_press_direction(GameState* game, Direction direction);
void game_release_direction(GameState* game, Direction direction);
void game_focus_lost(GameState* game);
void game_focus_gained(GameState* game);
void game_update(GameState* game);
const char* game_direction_name(Direction direction);
