#include "level_rules.h"

uint32_t normalize_level_number(uint32_t level) {
    if (level == 0u) return 1u;
    return level > kPacManHistoricalMaximumLevel ? kPacManHistoricalMaximumLevel : level;
}

uint32_t sanitize_game_speed(uint32_t speed) {
    if (speed == 0u) return 1u;
    // The VB6 S key cycles 1, 2, and 4.  Native validation may write an
    // arbitrary uint32_t directly, so bound it before converting to int or
    // using it as a divisor.  Values 1..4 retain their deterministic value.
    return speed > kPacManHistoricalMaximumGameSpeed
        ? kPacManHistoricalMaximumGameSpeed : speed;
}

LevelRules calculate_level_rules(uint32_t level) {
    const uint32_t historicalLevel = normalize_level_number(level);
    LevelRules rules{};
    rules.level = historicalLevel;

    // Nexgen's tmrKeyboard runs every 10 ms.  PacmanMovement and
    // AIGhostMonsters add Game.Speed pixels on each pass; no level-dependent
    // movement table or interval exists in the historical source.  Frightened
    // ghosts additionally skip every second AI movement pass in basGhostAI.
    rules.pacmanMovePixelsPerStep = 1u;
    rules.normalGhostMovePixelsPerStep = 1u;
    rules.frightenedGhostMoveIntervalSteps = 2u;

    // basPacman.bas assigns:
    //   PPTimer = (1000 - 100 * Pacman.Level) / Game.Speed
    // frmPacMan clamps Pacman.Level to 8 before this can run again.  Keep the
    // numerator at the normal Game.Speed=1 scale; callers apply the source's
    // integer division by the sanitized Game.Speed.
    rules.frightenedDurationSteps = 1000u - 100u * historicalLevel;

    // basSetUp.bas flashes when PPTimer < 200 / Game.Speed.  This threshold is
    // level-independent.  The source has no separate flash count or cadence
    // table; the existing 16-step phase remains the native presentation.
    rules.frightenedFlashStartSteps = 200u;

    // The source's ordinary Ready path flashes three times (six 500 ms timer
    // callbacks after a life).  Native has no sound dependency and retains
    // the existing bounded 60 fixed-step Ready presentation for level starts.
    rules.readyDurationSteps = 60u;
    return rules;
}
