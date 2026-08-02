#pragma once

#include "game_state.h"
#include "level_rules.h"

static const uint32_t kPacManLevelCompleteDelaySteps = 100u;
static const uint32_t kPacManDeathDurationSteps = 100u;
static const uint32_t kPacManReadyAfterDeathSteps = 300u;
static const uint32_t kPacManLevelCompleteBonus = 1000u;
static const uint8_t kPacManInitialLives = 3u;
static const uint32_t kPacManGhostCount = 4u;
static const uint32_t kPacManNormalPillScore = 10u;
static const uint32_t kPacManPowerPillScore = 10u;
static const uint32_t kPacManGhostEatScore[4] = {200u, 400u, 800u, 1600u};
static const uint32_t kPacManFruitTriggerTime = 4000u;
static const uint32_t kPacManFruitExpirationTime = 5000u;
// The VB6 form's only presentation timer is 500 ms.  Historical source does
// not blink power pills, so this fixed cadence is the smallest deterministic
// approximation for the requested native polish.
static const uint32_t kPacManPowerPillBlinkIntervalSteps = 50u;
static const uint32_t kPacManInitialReadyDurationSteps = 450u;
static const uint32_t kPacManInitialHighScore = 10000u;
static const int kPacManFruitCenterX = 232;
static const int kPacManFruitCenterY = 280;
static const uint8_t kPacManMaximumLives = 255u;

struct SpriteRect {
    int x;
    int y;
    int width;
    int height;
};

struct FruitRules {
    uint8_t type;
    uint32_t score;
    SpriteRect sprite;
    SpriteRect mask;
    uint32_t triggerTimeSteps;
    uint32_t expirationTimeSteps;
    uint32_t visibleDurationSteps;
    int x;
    int y;
};

struct ScoreAwardResult {
    uint32_t previousScore;
    uint32_t newScore;
    bool saturated;
    uint8_t extraLivesAwarded;
};

struct GhostTarget {
    int x;
    int y;
};

void game_initialize(GameState* game);
void game_begin_initial_ready(GameState* game);
void game_reset_level(GameState* game);
bool game_restart_session(GameState* game);
FruitRules calculate_fruit_rules(uint32_t level);
const char* fruit_type_name(uint8_t fruitType);
const char* fruit_phase_name(FruitPhase phase);
bool pacman_collides_with_fruit(const PacManState& pacman, const FruitState& fruit);
ScoreAwardResult award_score(GameState& game, uint32_t points);
void add_score(GameState& game, uint32_t points);
bool pacman_collides_with_ghost(const PacManState& pacman, const GhostState& ghost);
const char* ghost_kind_name(GhostKind kind);
const char* ghost_condition_name(GhostCondition condition);
const char* ghost_release_state_name(GhostReleaseState state);
uint32_t game_frightened_duration_steps(const GameState& game);
uint32_t game_frightened_flash_threshold(const GameState& game);
Direction choose_ghost_direction(const GameState& game, const GhostState& ghost,
                                 int targetX, int targetY);
Direction choose_pink_direction(const GameState& game, const GhostState& ghost,
                                int targetX, int targetY);
Direction choose_cyan_direction(const GameState& game, const GhostState& ghost,
                               int targetX, int targetY);
Direction choose_orange_direction(const GameState& game, const GhostState& ghost,
                                  int targetX, int targetY);
GhostTarget calculate_pink_target(const GameState& game, const PacManState& pacman);
GhostTarget calculate_cyan_target(const GameState& game, const PacManState& pacman,
                                  const GhostState& red);
GhostTarget calculate_orange_target(const GameState& game, const PacManState& pacman,
                                    const GhostState& orange);
int orange_distance_tiles(const PacManState& pacman, const GhostState& orange);
bool orange_uses_far_target(const PacManState& pacman, const GhostState& orange);
Direction game_direction_for_key(int keyCode);
void game_press_direction(GameState* game, Direction direction);
void game_release_direction(GameState* game, Direction direction);
void game_focus_lost(GameState* game);
void game_focus_gained(GameState* game);
void game_update(GameState* game);
const char* game_direction_name(Direction direction);
