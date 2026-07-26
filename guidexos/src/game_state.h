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

enum class PlayState : uint8_t {
    Playing,
    Dying,
    ReadyAfterDeath,
    LevelComplete,
    GameOver
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

enum class GhostKind : uint8_t {
    Red = 0,
    Pink = 1,
    Cyan = 2,
    Orange = 3
};

// Nexgen does not have a shared Chase/Scatter mode.  This condition is the
// per-ghost power-pill/eaten lifecycle only; normal target selection remains
// independent for each ghost.
enum class GhostCondition : uint8_t {
    Normal,
    Frightened,
    Eaten,
    Returning
};

enum class GhostReleaseState : uint8_t {
    Normal,
    ReturningHouse,
    PinkHouseBounce,
    PinkToCenter,
    PinkExiting,
    CyanHouseBounce,
    CyanExiting,
    OrangeHouseBounce,
    OrangeToCenter,
    OrangeExiting
};

struct GhostState {
    GhostKind kind;
    int x;
    int y;
    Direction direction;
    Direction requestedDirection;
    int offset;
    int speed;
    int targetX;
    int targetY;
    uint8_t animationFrame;
    GhostCondition condition;
    uint32_t powerPillStepsRemaining;
    bool frightenedDelayToggle;
    bool active;
    bool collisionActive;
    GhostReleaseState releaseState;
    uint32_t releaseStepsRemaining;
};

struct GameState {
    PacManState pacman;
    GhostState ghosts[4];
    LevelState level;
    uint32_t score;
    uint32_t levelNumber;
    uint32_t gameSpeed;
    uint8_t ghostEatChain;
    uint8_t frightenedFlashPhase;
    uint8_t lives;
    PlayState playState;
    uint32_t levelCompleteStepsRemaining;
    uint32_t deathStepsRemaining;
    uint32_t readyStepsRemaining;
    uint8_t deathAnimationFrame;
    uint32_t levelCompleteTransitions;
    uint32_t levelResetCount;
    uint32_t deathTransitions;
    uint32_t gameOverTransitions;
    HeldDirections held;
    bool focused;
    bool visualDirty;
    bool turnAccepted;
    bool becameBlocked;
    bool tunnelWrapped;
    bool redTunnelWrapped;
    bool pinkTunnelWrapped;
    bool cyanTunnelWrapped;
    bool pinkReleaseStarted;
    bool pinkReleaseCompleted;
    bool cyanReleaseStarted;
    bool cyanReleaseCompleted;
    bool orangeTunnelWrapped;
    bool orangeReleaseStarted;
    bool orangeReleaseCompleted;
    bool normalPillConsumed;
    bool powerPillConsumed;
    bool scoreChanged;
    bool levelCompleteEntered;
    bool levelReset;
    bool countUnderflow;
    bool collisionDetected;
    bool deathEntered;
    bool lifeDecremented;
    bool actorReset;
    bool gameOverEntered;
    bool sessionRestarted;
    bool suppressGhostCollisionsForValidation;
    bool powerPillEncounterReset;
    bool ghostReversalRequested[4];
    bool ghostReversalApplied[4];
    bool ghostEnteredFrightened[4];
    bool ghostTimerInitialized[4];
    bool frightenedFlashingBegan[4];
    bool ghostTimerExpired[4];
    bool ghostEaten[4];
    bool ghostEnteredReturning[4];
    bool ghostReturned[4];
    bool ghostEatScoreAwarded[4];
    uint32_t ghostEatScore[4];
    bool ghostEatScoreChanged;
    uint64_t simulationSteps;
};
