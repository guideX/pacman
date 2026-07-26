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

enum class GhostReleaseState : uint8_t {
    Normal,
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
    uint64_t simulationSteps;
};
