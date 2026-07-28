#include "game.h"

#include <guidexos/abi.h>

#include "level.h"

namespace {

static int direction_x(Direction direction) {
    return direction == Direction::Left ? -1 : direction == Direction::Right ? 1 : 0;
}

static int direction_y(Direction direction) {
    return direction == Direction::Up ? -1 : direction == Direction::Down ? 1 : 0;
}

static bool is_horizontal(Direction direction) {
    return direction == Direction::Left || direction == Direction::Right;
}

static int absolute_value(int value) {
    const int64_t wide = static_cast<int64_t>(value);
    const int64_t magnitude = wide < 0 ? -wide : wide;
    return magnitude > 0x7FFFFFFFll ? 0x7FFFFFFF : static_cast<int>(magnitude);
}

// Count aligned house bounces, not render or simulation ticks. Two direction
// reversals reproduce the short historical vertical wait before Pink heads to
// the center of the box.
static const uint32_t kPinkHouseBounceSteps = 2u;
static const int kOrangeTargetThresholdTiles = 4;
static const int kOrangeProjectionTiles = 12;

static uint32_t effective_game_speed(const GameState& game) {
    return sanitize_game_speed(game.gameSpeed);
}

static uint32_t frightened_duration_steps(const GameState& game) {
    const LevelRules rules = calculate_level_rules(game.levelNumber);
    const uint32_t speed = effective_game_speed(game);
    return rules.frightenedDurationSteps / speed;
}

static uint32_t frightened_flash_threshold(const GameState& game) {
    const LevelRules rules = calculate_level_rules(game.levelNumber);
    return rules.frightenedFlashStartSteps / effective_game_speed(game);
}

static void clear_power_event_flags(GameState* game) {
    if (!game) return;
    game->powerPillEncounterReset = false;
    game->ghostEatScoreChanged = false;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        game->ghostReversalRequested[index] = false;
        game->ghostReversalApplied[index] = false;
        game->ghostEnteredFrightened[index] = false;
        game->ghostTimerInitialized[index] = false;
        game->frightenedFlashingBegan[index] = false;
        game->ghostTimerExpired[index] = false;
        game->ghostEaten[index] = false;
        game->ghostEnteredReturning[index] = false;
        game->ghostReturned[index] = false;
        game->ghostEatScoreAwarded[index] = false;
        game->ghostEatScore[index] = 0;
    }
}

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int historical_tile_coordinate(int position) {
    const int64_t wide = static_cast<int64_t>(position);
    if (wide >= 0) return static_cast<int>(wide / kPacManTileSize);
    return static_cast<int>(-((-wide + kPacManTileSize - 1) / kPacManTileSize));
}

static void clear_held(HeldDirections* held) {
    if (!held) return;
    held->left = false;
    held->right = false;
    held->up = false;
    held->down = false;
}

static void set_held(HeldDirections* held, Direction direction, bool value) {
    if (!held) return;
    switch (direction) {
    case Direction::Left: held->left = value; break;
    case Direction::Right: held->right = value; break;
    case Direction::Up: held->up = value; break;
    case Direction::Down: held->down = value; break;
    case Direction::None: break;
    }
}

static int next_offset(int offset, Direction direction, int speed) {
    int result = offset + (direction_x(direction) + direction_y(direction)) * speed;
    while (result < 0) result += kPacManTileSize;
    while (result >= kPacManTileSize) result -= kPacManTileSize;
    return result;
}

static bool is_reverse_direction(Direction direction, Direction current) {
    return (direction == Direction::Left && current == Direction::Right) ||
        (direction == Direction::Right && current == Direction::Left) ||
        (direction == Direction::Up && current == Direction::Down) ||
        (direction == Direction::Down && current == Direction::Up);
}

static Direction opposite_direction(Direction direction) {
    switch (direction) {
    case Direction::Up: return Direction::Down;
    case Direction::Down: return Direction::Up;
    case Direction::Left: return Direction::Right;
    case Direction::Right: return Direction::Left;
    case Direction::None: return Direction::None;
    }
    return Direction::None;
}

static void reset_pacman(PacManState* pacman) {
    if (!pacman) return;
    pacman->x = 224;
    pacman->y = 376;
    pacman->direction = Direction::Right;
    pacman->facingDirection = Direction::Right;
    // The historical actor faces and starts moving right, but native buffered
    // input is deliberately empty after a reset so a held key cannot leak
    // across a death, level transition, or restart.
    pacman->requestedDirection = Direction::None;
    pacman->offset = 8;
    pacman->speed = 1;
    pacman->mouth = 0;
    pacman->mouthDirection = 1;
    pacman->mouthSpeed = 0;
}

static void reset_ghosts(GhostState* ghosts) {
    if (!ghosts) return;

    // Historical DefaultPositions: Red's VB6 direction was 2 + Rnd. The
    // native milestone chooses Left for deterministic stationary resets.
    ghosts[0].kind = GhostKind::Red;
    ghosts[0].x = 224;
    ghosts[0].y = 184;
    ghosts[0].direction = Direction::Left;
    ghosts[0].requestedDirection = Direction::Left;
    ghosts[0].offset = 0;
    ghosts[0].speed = 1;
    ghosts[0].targetX = 224;
    ghosts[0].targetY = 376;
    ghosts[0].animationFrame = 0;
    ghosts[0].condition = GhostCondition::Normal;
    ghosts[0].powerPillStepsRemaining = 0;
    ghosts[0].frightenedDelayToggle = false;
    ghosts[0].active = true;
    ghosts[0].collisionActive = true;
    ghosts[0].releaseState = GhostReleaseState::Normal;
    ghosts[0].releaseStepsRemaining = 0;

    ghosts[1].kind = GhostKind::Pink;
    ghosts[1].x = 192;
    ghosts[1].y = 224;
    ghosts[1].direction = Direction::Up;
    ghosts[1].requestedDirection = Direction::Up;
    ghosts[1].offset = 0;
    ghosts[1].speed = 1;
    ghosts[1].targetX = 0;
    ghosts[1].targetY = 0;
    ghosts[1].animationFrame = 0;
    ghosts[1].condition = GhostCondition::Normal;
    ghosts[1].powerPillStepsRemaining = 0;
    ghosts[1].frightenedDelayToggle = false;
    ghosts[1].active = true;
    ghosts[1].collisionActive = false;
    ghosts[1].releaseState = GhostReleaseState::PinkHouseBounce;
    ghosts[1].releaseStepsRemaining = kPinkHouseBounceSteps;

    ghosts[2].kind = GhostKind::Cyan;
    ghosts[2].x = 224;
    ghosts[2].y = 240;
    ghosts[2].direction = Direction::Down;
    ghosts[2].requestedDirection = Direction::Down;
    ghosts[2].offset = 0;
    ghosts[2].speed = 1;
    ghosts[2].targetX = 0;
    ghosts[2].targetY = 0;
    ghosts[2].animationFrame = 0;
    ghosts[2].condition = GhostCondition::Normal;
    ghosts[2].powerPillStepsRemaining = 0;
    ghosts[2].frightenedDelayToggle = false;
    ghosts[2].active = true;
    ghosts[2].collisionActive = false;
    ghosts[2].releaseState = GhostReleaseState::CyanHouseBounce;
    ghosts[2].releaseStepsRemaining = 2;

    ghosts[3].kind = GhostKind::Orange;
    ghosts[3].x = 256;
    ghosts[3].y = 224;
    ghosts[3].direction = Direction::Up;
    ghosts[3].requestedDirection = Direction::Up;
    ghosts[3].offset = 0;
    ghosts[3].speed = 1;
    ghosts[3].targetX = 0;
    ghosts[3].targetY = 0;
    ghosts[3].animationFrame = 0;
    ghosts[3].condition = GhostCondition::Normal;
    ghosts[3].powerPillStepsRemaining = 0;
    ghosts[3].frightenedDelayToggle = false;
    ghosts[3].active = true;
    ghosts[3].collisionActive = false;
    ghosts[3].releaseState = GhostReleaseState::OrangeHouseBounce;
    ghosts[3].releaseStepsRemaining = 0;
}

static void reset_actor_positions(GameState* game) {
    if (!game) return;
    reset_pacman(&game->pacman);
    reset_ghosts(game->ghosts);
    const LevelRules rules = calculate_level_rules(game->levelNumber);
    const uint32_t speed = effective_game_speed(*game);
    game->pacman.speed = static_cast<int>(rules.pacmanMovePixelsPerStep * speed);
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        game->ghosts[index].speed = static_cast<int>(rules.normalGhostMovePixelsPerStep * speed);
    }
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    const GhostTarget cyanTarget = calculate_cyan_target(*game, game->pacman, game->ghosts[0]);
    game->ghosts[2].targetX = cyanTarget.x;
    game->ghosts[2].targetY = cyanTarget.y;
    const GhostTarget orangeTarget = calculate_orange_target(*game, game->pacman, game->ghosts[3]);
    game->ghosts[3].targetX = orangeTarget.x;
    game->ghosts[3].targetY = orangeTarget.y;
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    clear_held(&game->held);
    game->deathAnimationFrame = 0;
}

static bool ghost_can_receive_power_pill(const GhostState& ghost) {
    return ghost.active && ghost.releaseState == GhostReleaseState::Normal &&
        (ghost.condition == GhostCondition::Normal || ghost.condition == GhostCondition::Frightened);
}

static void align_for_ghost_speed(GhostState* ghost) {
    if (!ghost || ghost->speed <= 1) return;
    uint32_t speed = static_cast<uint32_t>(ghost->speed);
    uint32_t mask = 0;
    while (speed > 1u) {
        mask = (mask << 1u) | 1u;
        speed >>= 1u;
    }
    ghost->offset &= ~static_cast<int>(mask);
    ghost->x &= ~static_cast<int>(mask);
    ghost->y &= ~static_cast<int>(mask);
    // The VB6 bit-mask alignment guarantees divisibility for its doubled
    // speed. Native direction decisions additionally require the fixed-step
    // offset and logical tile center to agree, so normalize to the center of
    // the current safe cell before resuming eyes movement.
    const int column = level_column_from_position(ghost->x);
    const int row = level_row_from_position(ghost->y);
    if (column >= 0 && column < kPacManMazeColumns && row >= 0 && row < kPacManMazeRows) {
        ghost->x = (column + 1) * kPacManTileSize;
        ghost->y = (row + 1) * kPacManTileSize;
        ghost->offset = 0;
    }
}

static void activate_power_pill(GameState* game) {
    if (!game) return;
    game->ghostEatChain = 0;
    game->powerPillEncounterReset = true;
    const uint32_t duration = frightened_duration_steps(*game);
    // A defensive zero-duration result is a normal power-pill encounter, not
    // a one-update frightened flash.  This branch is unreachable through the
    // historical level clamp (level 8 is 200 ticks at normal speed), but it
    // keeps manually supplied high-speed state safe and source-consistent.
    if (duration == 0u) return;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        GhostState& ghost = game->ghosts[index];
        if (!ghost_can_receive_power_pill(ghost)) continue;
        if (ghost.condition != GhostCondition::Frightened) {
            game->ghostEnteredFrightened[index] = true;
        }
        ghost.condition = GhostCondition::Frightened;
        ghost.powerPillStepsRemaining = duration;
        game->ghostTimerInitialized[index] = true;
        // The VB6 routine reverses immediately, before the next movement
        // sample, and does this once for each eligible ghost on each pill.
        game->ghostReversalRequested[index] = true;
        ghost.direction = opposite_direction(ghost.direction);
        ghost.requestedDirection = ghost.direction;
        game->ghostReversalApplied[index] = true;
        game->visualDirty = true;
    }
}

static void restore_ghost_normal_speed(const GameState& game, GhostState* ghost) {
    if (!ghost) return;
    const LevelRules rules = calculate_level_rules(game.levelNumber);
    ghost->speed = static_cast<int>(rules.normalGhostMovePixelsPerStep * effective_game_speed(game));
}

static void enter_returning_house(GameState* game, GhostState* ghost) {
    if (!game || !ghost) return;
    ghost->condition = GhostCondition::Returning;
    ghost->releaseState = GhostReleaseState::ReturningHouse;
    ghost->collisionActive = false;
    restore_ghost_normal_speed(*game, ghost);
    ghost->direction = Direction::Down;
    ghost->requestedDirection = Direction::Down;
    game->ghostEnteredReturning[static_cast<uint32_t>(ghost->kind)] = true;
    game->visualDirty = true;
}

static void advance_power_pill_timer(GameState* game, GhostState* ghost) {
    if (!game || !ghost || ghost->condition != GhostCondition::Frightened ||
        ghost->powerPillStepsRemaining == 0) return;

    const uint32_t threshold = frightened_flash_threshold(*game);
    const uint32_t before = ghost->powerPillStepsRemaining;
    --ghost->powerPillStepsRemaining;
    if (before >= threshold && ghost->powerPillStepsRemaining < threshold) {
        game->frightenedFlashingBegan[static_cast<uint32_t>(ghost->kind)] = true;
    }
    if (ghost->powerPillStepsRemaining == 0) {
        ghost->condition = GhostCondition::Normal;
        game->ghostTimerExpired[static_cast<uint32_t>(ghost->kind)] = true;
        game->visualDirty = true;
    }
}

static void clear_level_completion_combat_state(GameState* game) {
    if (!game) return;
    clear_held(&game->held);
    game->pacman.direction = Direction::None;
    game->pacman.requestedDirection = Direction::None;
    game->ghostEatChain = 0;
    game->frightenedFlashPhase = 0;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        GhostState& ghost = game->ghosts[index];
        ghost.condition = GhostCondition::Normal;
        ghost.powerPillStepsRemaining = 0;
        ghost.frightenedDelayToggle = false;
        restore_ghost_normal_speed(*game, &ghost);
        if (ghost.releaseState == GhostReleaseState::Normal) ghost.collisionActive = true;
        else ghost.collisionActive = false;
    }
}

static int wrap_tunnel_column(int column) {
    while (column < 0) column += kPacManMazeColumns;
    while (column >= kPacManMazeColumns) column -= kPacManMazeColumns;
    return column;
}

static void consume_target_pill(GameState* game) {
    if (!game || game->pacman.direction == Direction::None) return;

    const Direction direction = game->pacman.direction;
    const int targetX = game->pacman.x + direction_x(direction) * kPacManTileSize;
    const int targetY = game->pacman.y + direction_y(direction) * kPacManTileSize;
    int targetColumn = level_column_from_position(targetX);
    const int targetRow = level_row_from_position(targetY);
    if (is_horizontal(direction) && is_tunnel_row(targetRow)) {
        targetColumn = wrap_tunnel_column(targetColumn);
    }
    if (targetColumn < 0 || targetColumn >= kPacManMazeColumns ||
        targetRow < 0 || targetRow >= kPacManMazeRows) return;

    const ConsumptionResult result = level_consume(&game->level, targetColumn, targetRow);
    if (result == ConsumptionResult::CountUnderflow) {
        game->countUnderflow = true;
        return;
    }
    if (result == ConsumptionResult::None) return;

    game->visualDirty = true;
    if (result == ConsumptionResult::NormalPill) {
        game->normalPillConsumed = true;
        add_score(*game, kPacManNormalPillScore);
    } else {
        game->powerPillConsumed = true;
        add_score(*game, kPacManPowerPillScore);
        activate_power_pill(game);
    }

    if (game->level.totalConsumablesRemaining == 0) {
        // frmPacMan adds the level bonus in the same one-shot completion
        // branch, before ResetLevel/DefaultPositions are called.
        add_score(*game, kPacManLevelCompleteBonus);
        clear_level_completion_combat_state(game);
        game->playState = PlayState::LevelComplete;
        game->levelCompleteStepsRemaining = kPacManLevelCompleteDelaySteps;
        ++game->levelCompleteTransitions;
        game->levelCompleteEntered = true;
    }
}

static void enter_dying(GameState* game);

static void eat_frightened_ghost(GameState* game, GhostState* ghost) {
    if (!game || !ghost || ghost->condition != GhostCondition::Frightened ||
        ghost->powerPillStepsRemaining == 0) return;
    const uint32_t index = static_cast<uint32_t>(ghost->kind);
    ghost->condition = GhostCondition::Eaten;
    ghost->powerPillStepsRemaining = 0;
    ghost->collisionActive = false;
    ghost->speed *= 2;
    align_for_ghost_speed(ghost);
    const uint32_t nextChain = game->ghostEatChain < 4u ? game->ghostEatChain + 1u : 4u;
    game->ghostEatChain = static_cast<uint8_t>(nextChain);
    const uint32_t score = kPacManGhostEatScore[nextChain - 1u];
    add_score(*game, score);
    game->ghostEaten[index] = true;
    game->ghostEatScoreAwarded[index] = true;
    game->ghostEatScore[index] = score;
    game->ghostEatScoreChanged = true;
    game->visualDirty = true;
}

static void resolve_ghost_collisions(GameState* game) {
    if (!game) return;
    // Historical TestCollisions scans Ghost(1) through Ghost(4). Preserve
    // that deterministic ordering and stop after the first lethal normal
    // ghost so no later ghost can score after Pac-Man has died.
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        GhostState& ghost = game->ghosts[index];
        if (!pacman_collides_with_ghost(game->pacman, ghost)) continue;
        if (ghost.condition == GhostCondition::Frightened &&
            ghost.powerPillStepsRemaining > 0) {
            eat_frightened_ghost(game, &ghost);
        } else if (ghost.condition == GhostCondition::Normal) {
            enter_dying(game);
            break;
        }
    }
}

static int wrap_tunnel_position(int position) {
    while (position < 16) position += 416;
    while (position > 431) position -= 416;
    return position;
}

static int red_target_distance(const GhostState& red, int targetX, int targetY,
                               Direction direction) {
    // Red follows the historical direct-target rule. Use Manhattan distance
    // in logical pixels, with the tunnel's shortest wrapped horizontal span.
    const int candidateX = red.x + direction_x(direction) * kPacManTileSize;
    const int candidateY = red.y + direction_y(direction) * kPacManTileSize;
    int deltaX = absolute_value(candidateX - targetX);
    const int redRow = level_row_from_position(red.y);
    const int targetRow = level_row_from_position(targetY);
    if (redRow == kPacManTunnelRow && targetRow == kPacManTunnelRow) {
        const int wrappedCandidateX = wrap_tunnel_position(candidateX);
        const int wrappedTargetX = wrap_tunnel_position(targetX);
        deltaX = absolute_value(wrappedCandidateX - wrappedTargetX);
        if (deltaX > 208) deltaX = 416 - deltaX;
    }
    return deltaX + absolute_value(candidateY - targetY);
}

static Direction choose_red_direction_impl(const GameState& game, const GhostState& ghost,
                                            int targetX, int targetY) {
    // This order is the historical VB6 sign-priority order: vertical choices
    // are considered before horizontal choices, with Up before Down and Left
    // before Right. It also provides a stable tie-break for equal distances.
    static const Direction candidates[] = {
        Direction::Up, Direction::Down, Direction::Left, Direction::Right
    };
    bool hasNonReverse = false;
    for (uint32_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (!is_reverse_direction(candidates[index], ghost.direction) &&
            can_move(game, ghost.x, ghost.y, candidates[index])) {
            hasNonReverse = true;
            break;
        }
    }

    Direction bestDirection = Direction::None;
    int bestDistance = 0x7FFFFFFF;
    for (uint32_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        const Direction candidate = candidates[index];
        if (!can_move(game, ghost.x, ghost.y, candidate)) continue;
        if (hasNonReverse && is_reverse_direction(candidate, ghost.direction)) continue;
        const int distance = red_target_distance(ghost, targetX, targetY, candidate);
        if (bestDirection == Direction::None || distance < bestDistance) {
            bestDirection = candidate;
            bestDistance = distance;
        }
    }
    return bestDirection;
}

static bool has_non_reverse_direction(const GameState& game, const GhostState& ghost) {
    static const Direction candidates[] = {
        Direction::Up, Direction::Down, Direction::Left, Direction::Right
    };
    for (uint32_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (!is_reverse_direction(candidates[index], ghost.direction) &&
            can_move(game, ghost.x, ghost.y, candidates[index])) return true;
    }
    return false;
}

static bool can_choose_direction(const GameState& game, const GhostState& ghost,
                                 Direction direction, bool hasNonReverse) {
    if (!can_move(game, ghost.x, ghost.y, direction)) return false;
    return !hasNonReverse || !is_reverse_direction(direction, ghost.direction);
}

static Direction choose_pink_direction_impl(const GameState& game, const GhostState& ghost,
                                             int targetX, int targetY) {
    // basGhostAI.bas uses Sgn/Abs at a junction rather than a numeric
    // distance. Its four independent If statements make a legal horizontal
    // choice win over a legal vertical choice when both point at the target;
    // Right wins over Left, and Down wins over Up within each pair.
    const bool hasNonReverse = has_non_reverse_direction(game, ghost);
    const int64_t deltaX = static_cast<int64_t>(targetX) - ghost.x;
    const int64_t deltaY = static_cast<int64_t>(targetY) - ghost.y;
    const int signX = deltaX < 0 ? -1 : deltaX > 0 ? 1 : 0;
    const int signY = deltaY < 0 ? -1 : deltaY > 0 ? 1 : 0;
    Direction selected = Direction::None;

    if (signY < 0 && can_choose_direction(game, ghost, Direction::Up, hasNonReverse)) {
        selected = Direction::Up;
    }
    if (signY > 0 && can_choose_direction(game, ghost, Direction::Down, hasNonReverse)) {
        selected = Direction::Down;
    }
    if (signX < 0 && can_choose_direction(game, ghost, Direction::Left, hasNonReverse)) {
        selected = Direction::Left;
    }
    if (signX > 0 && can_choose_direction(game, ghost, Direction::Right, hasNonReverse)) {
        selected = Direction::Right;
    }
    if (selected != Direction::None) return selected;

    // The historical side checks are reached only when the target-directed
    // choice was blocked and the target shares an axis with Pink.
    if (signY == 0) {
        if (can_choose_direction(game, ghost, Direction::Up, hasNonReverse)) selected = Direction::Up;
        if (can_choose_direction(game, ghost, Direction::Down, hasNonReverse)) selected = Direction::Down;
    }
    if (signX == 0) {
        if (can_choose_direction(game, ghost, Direction::Left, hasNonReverse)) selected = Direction::Left;
        if (can_choose_direction(game, ghost, Direction::Right, hasNonReverse)) selected = Direction::Right;
    }
    if (selected != Direction::None) return selected;

    static const Direction candidates[] = {
        Direction::Up, Direction::Down, Direction::Left, Direction::Right
    };
    for (uint32_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (can_choose_direction(game, ghost, candidates[index], hasNonReverse)) {
            return candidates[index];
        }
    }
    // A dead end has no non-reverse candidate, so the reverse is legal.
    for (uint32_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (can_move(game, ghost.x, ghost.y, candidates[index])) return candidates[index];
    }
    return Direction::None;
}

static Direction choose_frightened_direction(const GameState& game, const GhostState& ghost,
                                             int targetX, int targetY) {
    // The historical routine negates the target signs at a junction. The
    // native chooser is expressed as a mirrored target, while retaining the
    // same legal-direction and reverse rules as the normal helper.
    const int64_t mirroredX = static_cast<int64_t>(ghost.x) * 2ll - targetX;
    const int64_t mirroredY = static_cast<int64_t>(ghost.y) * 2ll - targetY;
    const int minimumInt = -0x7FFFFFFF - 1;
    const int maximumInt = 0x7FFFFFFF;
    const int safeX = mirroredX < minimumInt ? minimumInt : mirroredX > maximumInt ? maximumInt : static_cast<int>(mirroredX);
    const int safeY = mirroredY < minimumInt ? minimumInt : mirroredY > maximumInt ? maximumInt : static_cast<int>(mirroredY);
    return choose_pink_direction_impl(game, ghost, safeX, safeY);
}

static Direction choose_returning_path_direction(const GameState& game, const GhostState& ghost) {
    const int startColumn = level_column_from_position(ghost.x);
    const int startRow = level_row_from_position(ghost.y);
    const int targetColumn = level_column_from_position(224);
    const int targetRow = level_row_from_position(184);
    if (startColumn < 0 || startColumn >= kPacManMazeColumns || startRow < 0 ||
        startRow >= kPacManMazeRows) return Direction::None;
    int distances[kPacManMazeRows][kPacManMazeColumns];
    int queueColumns[kPacManMazeRows * kPacManMazeColumns];
    int queueRows[kPacManMazeRows * kPacManMazeColumns];
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) distances[row][column] = -1;
    }
    int head = 0;
    int tail = 0;
    distances[startRow][startColumn] = 0;
    queueColumns[tail] = startColumn;
    queueRows[tail++] = startRow;
    const Direction directions[] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
    while (head < tail) {
        const int column = queueColumns[head];
        const int row = queueRows[head++];
        for (uint32_t index = 0; index < 4u; ++index) {
            int nextColumn = column;
            int nextRow = row;
            switch (directions[index]) {
            case Direction::Up: --nextRow; break;
            case Direction::Down: ++nextRow; break;
            case Direction::Left: --nextColumn; break;
            case Direction::Right: ++nextColumn; break;
            case Direction::None: break;
            }
            if (nextRow == kPacManTunnelRow && (nextColumn < 0 || nextColumn >= kPacManMazeColumns)) {
                nextColumn = nextColumn < 0 ? kPacManMazeColumns - 1 : 0;
            }
            if (nextColumn < 0 || nextColumn >= kPacManMazeColumns || nextRow < 0 ||
                nextRow >= kPacManMazeRows || distances[nextRow][nextColumn] >= 0 ||
                !is_walkable_cell(level_cell(game.level, nextColumn, nextRow))) continue;
            distances[nextRow][nextColumn] = distances[row][column] + 1;
            queueColumns[tail] = nextColumn;
            queueRows[tail++] = nextRow;
        }
    }
    if (targetColumn < 0 || targetColumn >= kPacManMazeColumns || targetRow < 0 ||
        targetRow >= kPacManMazeRows || distances[targetRow][targetColumn] < 0) return Direction::None;

    Direction best = Direction::None;
    int bestDistance = 0x7FFFFFFF;
    for (uint32_t index = 0; index < 4u; ++index) {
        if (!can_move(game, ghost.x, ghost.y, directions[index])) continue;
        int nextColumn = startColumn;
        int nextRow = startRow;
        switch (directions[index]) {
        case Direction::Up: --nextRow; break;
        case Direction::Down: ++nextRow; break;
        case Direction::Left: --nextColumn; break;
        case Direction::Right: ++nextColumn; break;
        case Direction::None: break;
        }
        if (nextRow == kPacManTunnelRow && (nextColumn < 0 || nextColumn >= kPacManMazeColumns)) {
            nextColumn = nextColumn < 0 ? kPacManMazeColumns - 1 : 0;
        }
        if (nextColumn < 0 || nextColumn >= kPacManMazeColumns || nextRow < 0 ||
            nextRow >= kPacManMazeRows || distances[nextRow][nextColumn] < 0) continue;
        if (nextColumn == targetColumn && nextRow == targetRow) return directions[index];
        if (distances[nextRow][nextColumn] < bestDistance) {
            bestDistance = distances[nextRow][nextColumn];
            best = directions[index];
        }
    }
    return best;
}

static void move_ghost_one_step(GameState* game, GhostState* ghost) {
    if (!game || !ghost || ghost->direction == Direction::None || ghost->speed <= 0) return;
    // The historical level data represents the house door as part of the
    // sprite/box convention rather than a normal maze cell. Permit only the
    // fixed x=224 upward door lane during a house release; normal maze
    // targeting still uses can_move with no house exception.
    const bool houseDoor = (ghost->kind == GhostKind::Pink &&
        ghost->releaseState == GhostReleaseState::PinkExiting) ||
        (ghost->kind == GhostKind::Cyan &&
        ghost->releaseState == GhostReleaseState::CyanExiting) ||
        (ghost->kind == GhostKind::Orange &&
        ghost->releaseState == GhostReleaseState::OrangeExiting) ||
        ghost->releaseState == GhostReleaseState::ReturningHouse;
    const bool houseDoorLane = houseDoor &&
        ghost->x == 224 && ghost->direction == Direction::Up &&
        ghost->y >= 184 && ghost->y <= 224;
    const bool returningDoorLane = ghost->condition == GhostCondition::Returning &&
        ghost->x == 224 && ghost->direction == Direction::Up &&
        ghost->y >= 184 && ghost->y <= 224;
    const bool returningDownDoorLane = ghost->releaseState == GhostReleaseState::ReturningHouse &&
        ghost->x == 224 && ghost->direction == Direction::Down &&
        ghost->y >= 184 && ghost->y <= 224;
    if (ghost->offset == 0 && !can_move(*game, ghost->x, ghost->y, ghost->direction) &&
        !houseDoorLane && !returningDoorLane && !returningDownDoorLane) {
        ghost->direction = Direction::None;
        ghost->requestedDirection = Direction::None;
        return;
    }

    const int oldX = ghost->x;
    const int oldY = ghost->y;
    ghost->x += direction_x(ghost->direction) * ghost->speed;
    ghost->y += direction_y(ghost->direction) * ghost->speed;
    ghost->offset = next_offset(ghost->offset, ghost->direction, ghost->speed);
    if (is_horizontal(ghost->direction) && is_tunnel_row(level_row_from_position(ghost->y))) {
        if (ghost->x > 416) {
            ghost->x -= 416;
            if (ghost->kind == GhostKind::Red) game->redTunnelWrapped = true;
            if (ghost->kind == GhostKind::Pink) game->pinkTunnelWrapped = true;
            if (ghost->kind == GhostKind::Cyan) game->cyanTunnelWrapped = true;
            if (ghost->kind == GhostKind::Orange) game->orangeTunnelWrapped = true;
        } else if (ghost->x < 16) {
            ghost->x += 416;
            if (ghost->kind == GhostKind::Red) game->redTunnelWrapped = true;
            if (ghost->kind == GhostKind::Pink) game->pinkTunnelWrapped = true;
            if (ghost->kind == GhostKind::Cyan) game->cyanTunnelWrapped = true;
            if (ghost->kind == GhostKind::Orange) game->orangeTunnelWrapped = true;
        }
    }
    ghost->animationFrame = static_cast<uint8_t>((ghost->animationFrame + 1u) % 2u);
    if (oldX != ghost->x || oldY != ghost->y) game->visualDirty = true;
}

static void update_active_ghost(GameState* game, GhostState* ghost,
                                int targetX, int targetY, bool signPriorityPolicy) {
    if (!game || !ghost || !ghost->active || ghost->speed <= 0) return;
    if (ghost->offset == 0) {
        const Direction selected = ghost->condition == GhostCondition::Frightened
            ? choose_frightened_direction(*game, *ghost, targetX, targetY)
            : signPriorityPolicy
                ? choose_pink_direction_impl(*game, *ghost, targetX, targetY)
                : choose_red_direction_impl(*game, *ghost, targetX, targetY);
        if (selected != Direction::None) {
            ghost->requestedDirection = selected;
            if (ghost->direction != selected) {
                ghost->direction = selected;
                game->visualDirty = true;
            }
        } else {
            ghost->direction = Direction::None;
        }
    }

    if (ghost->condition == GhostCondition::Frightened) {
        // basGhostAI.bas toggles DelayTime and suppresses every second
        // movement update while PPTimer is positive. The first frightened
        // update moves because the zero-initialized delay becomes one.
        const LevelRules rules = calculate_level_rules(game->levelNumber);
        if (rules.frightenedGhostMoveIntervalSteps > 1u) {
            ghost->frightenedDelayToggle = !ghost->frightenedDelayToggle;
            if (!ghost->frightenedDelayToggle) return;
        }
    }

    // Direction is selected only at an aligned tile center. The committed
    // direction then carries the ghost through the next 15 logical pixels.
    move_ghost_one_step(game, ghost);
}

static void update_returning_ghost(GameState* game, GhostState* ghost) {
    if (!game || !ghost || !ghost->active) return;
    const uint32_t index = static_cast<uint32_t>(ghost->kind);

    if (ghost->condition == GhostCondition::Eaten) {
        ghost->condition = GhostCondition::Returning;
        game->ghostEnteredReturning[index] = true;
        game->visualDirty = true;
    }

    if (ghost->releaseState == GhostReleaseState::ReturningHouse) {
        // The source's gate-to-box renewal segment is a fixed vertical lane.
        // Keep it explicit instead of asking normal maze targeting to sample
        // the non-maze door cell.
        restore_ghost_normal_speed(*game, ghost);
        ghost->x = 224;
        if (ghost->direction == Direction::Down) {
            ghost->y += ghost->speed;
            ghost->offset = (ghost->offset + ghost->speed) % kPacManTileSize;
            if (ghost->y >= 224) {
                ghost->y = 224;
                ghost->offset = 0;
                ghost->condition = GhostCondition::Normal;
                ghost->collisionActive = false;
                ghost->direction = Direction::Up;
                ghost->requestedDirection = Direction::Up;
                game->visualDirty = true;
            }
        } else {
            ghost->direction = Direction::Up;
            ghost->requestedDirection = Direction::Up;
            ghost->y -= ghost->speed;
            ghost->offset = (ghost->offset + kPacManTileSize - ghost->speed) % kPacManTileSize;
            if (ghost->y <= 184) {
                ghost->y = 184;
                ghost->offset = 8;
                ghost->releaseState = GhostReleaseState::Normal;
                ghost->condition = GhostCondition::Normal;
                ghost->collisionActive = true;
                ghost->direction = Direction::Left;
                ghost->requestedDirection = Direction::Left;
                game->ghostReturned[index] = true;
                game->visualDirty = true;
            }
        }
        return;
    }

    ghost->targetX = 224;
    ghost->targetY = 184;
    ghost->collisionActive = false;
    if (ghost->x == 224 && ghost->y == 192 && ghost->offset == 0) {
        // Native tile centers place the gate one half-tile above the row-11
        // center. Cross the final eight logical pixels through the fixed
        // house door lane before entering the renewal bounce.
        ghost->direction = Direction::Up;
        ghost->requestedDirection = Direction::Up;
        move_ghost_one_step(game, ghost);
        if (ghost->y <= 184) {
            ghost->y = 184;
            ghost->offset = 8;
            enter_returning_house(game, ghost);
        }
        return;
    }
    if (ghost->x == ghost->targetX && ghost->y == ghost->targetY && ghost->offset == 0) {
        enter_returning_house(game, ghost);
        return;
    }
    if (ghost->offset == 0) {
        const int column = level_column_from_position(ghost->x);
        const int row = level_row_from_position(ghost->y);
        if (column >= 0 && column < kPacManMazeColumns && row >= 0 && row < kPacManMazeRows) {
            ghost->x = (column + 1) * kPacManTileSize;
            ghost->y = (row + 1) * kPacManTileSize;
        }
        // The historical Eyesonly path targets the house gate with the same
        // maze junction semantics. Use a bounded maze path here so the native
        // eyes sprite cannot be trapped by a converted-coordinate dead-end;
        // it remains wall-safe and collision-inactive throughout the route.
        Direction selected = choose_returning_path_direction(*game, *ghost);
        if (selected == Direction::None) {
            selected = choose_pink_direction_impl(*game, *ghost,
                                                   ghost->targetX, ghost->targetY);
        }
        if (selected != Direction::None) {
            ghost->direction = selected;
            ghost->requestedDirection = selected;
        }
    }
    move_ghost_one_step(game, ghost);
    if (ghost->x == 224 && ghost->direction == Direction::Up && ghost->y <= 184 && ghost->y >= 176) {
        ghost->y = 184;
        ghost->offset = 8;
        enter_returning_house(game, ghost);
    } else if (ghost->x == 224 && ghost->y == 184 && ghost->offset == 0) {
        enter_returning_house(game, ghost);
    }
}

static void update_red_ghost(GameState* game) {
    if (!game) return;
    GhostState& red = game->ghosts[0];
    if (red.condition == GhostCondition::Eaten || red.condition == GhostCondition::Returning ||
        red.releaseState == GhostReleaseState::ReturningHouse) {
        update_returning_ghost(game, &red);
        return;
    }
    red.targetX = game->pacman.x;
    red.targetY = game->pacman.y;
    update_active_ghost(game, &red, red.targetX, red.targetY, false);
}

static void update_pink_house_release(GameState* game) {
    if (!game) return;
    GhostState& pink = game->ghosts[1];
    if (pink.releaseState == GhostReleaseState::PinkHouseBounce) {
        if (pink.offset == 0) {
            if (pink.x == 192 && pink.y == 224 && pink.direction == Direction::Up) {
                pink.direction = Direction::Down;
            } else if (pink.x == 192 && pink.y == 240 && pink.direction == Direction::Down) {
                pink.direction = Direction::Up;
            }
            if (pink.releaseStepsRemaining > 0) --pink.releaseStepsRemaining;
            if (pink.releaseStepsRemaining == 0 && pink.x == 192 && pink.y == 224) {
                pink.direction = Direction::Right;
                pink.requestedDirection = Direction::Right;
                pink.releaseState = GhostReleaseState::PinkToCenter;
                game->pinkReleaseStarted = true;
                game->visualDirty = true;
            }
        }
        move_ghost_one_step(game, &pink);
        return;
    }

    if (pink.releaseState == GhostReleaseState::PinkToCenter) {
        if (pink.offset == 0 && pink.x == 224 && pink.y == 224) {
            pink.direction = Direction::Up;
            pink.requestedDirection = Direction::Up;
            pink.releaseState = GhostReleaseState::PinkExiting;
            game->visualDirty = true;
        }
        move_ghost_one_step(game, &pink);
        return;
    }

    if (pink.releaseState == GhostReleaseState::PinkExiting) {
        if (pink.offset == 0 && pink.x == 224 && pink.y == 184) {
            pink.releaseState = GhostReleaseState::Normal;
            pink.collisionActive = true;
            pink.direction = Direction::Right;
            pink.requestedDirection = Direction::Right;
            pink.offset = 8;
            game->pinkReleaseCompleted = true;
            game->visualDirty = true;
            return;
        }
        move_ghost_one_step(game, &pink);
        // Ghost positions in the VB6 release routine use Offset=8 at y=184,
        // while the shared native mover aligns at Offset=0. Clamp the single
        // crossing step to the historical outside-house state instead of
        // allowing the fixed route to enter the row above the door.
        if (pink.y < 184) {
            pink.y = 184;
            pink.offset = 8;
            pink.releaseState = GhostReleaseState::Normal;
            pink.collisionActive = true;
            pink.direction = Direction::Right;
            pink.requestedDirection = Direction::Right;
            game->pinkReleaseCompleted = true;
            game->visualDirty = true;
        }
    }
}

static void update_pink_ghost(GameState* game) {
    if (!game) return;
    GhostState& pink = game->ghosts[1];
    if (!pink.active) return;
    if (pink.condition == GhostCondition::Eaten || pink.condition == GhostCondition::Returning ||
        pink.releaseState == GhostReleaseState::ReturningHouse) {
        update_returning_ghost(game, &pink);
        return;
    }
    const GhostTarget target = calculate_pink_target(*game, game->pacman);
    pink.targetX = target.x;
    pink.targetY = target.y;
    if (pink.releaseState != GhostReleaseState::Normal) {
        update_pink_house_release(game);
        return;
    }
    pink.collisionActive = true;
    update_active_ghost(game, &pink, pink.targetX, pink.targetY, true);
}

static void update_cyan_house_release(GameState* game) {
    if (!game) return;
    GhostState& cyan = game->ghosts[2];

    if (cyan.releaseState == GhostReleaseState::CyanHouseBounce) {
        if (cyan.offset == 0) {
            // This is the literal Ghost(3) branch in basGhostAI.bas: the
            // middle ghost reverses at y=224/y=240 and leaves after its
            // second aligned visit to the top of the box.
            if (cyan.x == 224 && cyan.y == 240 && cyan.direction == Direction::Down) {
                cyan.direction = Direction::Up;
            }
            if (cyan.x == 224 && cyan.y == 224 && cyan.direction == Direction::Up) {
                if (cyan.releaseStepsRemaining > 0) --cyan.releaseStepsRemaining;
                if (cyan.releaseStepsRemaining == 0) {
                    cyan.direction = Direction::Up;
                    cyan.requestedDirection = Direction::Up;
                    cyan.releaseState = GhostReleaseState::CyanExiting;
                    game->cyanReleaseStarted = true;
                    game->visualDirty = true;
                } else {
                    cyan.direction = Direction::Down;
                }
            }
        }
        move_ghost_one_step(game, &cyan);
        return;
    }

    if (cyan.releaseState == GhostReleaseState::CyanExiting) {
        move_ghost_one_step(game, &cyan);
        // VB6 sets Offset=8 and InGame=True at y=184 before the first normal
        // horizontal step. The shared fixed-step mover reaches that boundary
        // with a different offset, so normalize exactly that one transition.
        if (cyan.y < 184) {
            cyan.y = 184;
            cyan.offset = 8;
            cyan.releaseState = GhostReleaseState::Normal;
            cyan.collisionActive = true;
            cyan.direction = Direction::Left;
            cyan.requestedDirection = Direction::Left;
            game->cyanReleaseCompleted = true;
            game->visualDirty = true;
        }
    }
}

static void update_cyan_ghost(GameState* game) {
    if (!game) return;
    GhostState& cyan = game->ghosts[2];
    if (!cyan.active) return;
    if (cyan.condition == GhostCondition::Eaten || cyan.condition == GhostCondition::Returning ||
        cyan.releaseState == GhostReleaseState::ReturningHouse) {
        update_returning_ghost(game, &cyan);
        return;
    }

    const GhostTarget target = calculate_cyan_target(*game, game->pacman, game->ghosts[0]);
    cyan.targetX = target.x;
    cyan.targetY = target.y;
    if (cyan.releaseState != GhostReleaseState::Normal) {
        update_cyan_house_release(game);
        return;
    }
    cyan.collisionActive = true;
    update_active_ghost(game, &cyan, cyan.targetX, cyan.targetY, true);
}

static void update_orange_house_release(GameState* game) {
    if (!game) return;
    GhostState& orange = game->ghosts[3];

    if (orange.releaseState == GhostReleaseState::OrangeHouseBounce) {
        if (orange.offset == 0) {
            // This is the shared VB6 house routine. Orange starts at the
            // right side facing Up, so the first aligned visit reverses it
            // Down; subsequent visits bounce between y=224 and y=240.
            if ((orange.y == 224 || orange.y == 240) &&
                (orange.direction == Direction::Up || orange.direction == Direction::Down)) {
                orange.direction = orange.direction == Direction::Up
                    ? Direction::Down : Direction::Up;
            }

            // Ghost(4) moves left only after Ghost(2)/Pink has become InGame
            // in the historical source. The native release state is the
            // explicit equivalent of that source boolean.
            if (orange.x == 256 && orange.y == 224 &&
                game->ghosts[1].releaseState == GhostReleaseState::Normal) {
                orange.direction = Direction::Left;
                orange.requestedDirection = Direction::Left;
                orange.releaseState = GhostReleaseState::OrangeToCenter;
                game->orangeReleaseStarted = true;
                game->visualDirty = true;
            }
        }
        move_ghost_one_step(game, &orange);
        return;
    }

    if (orange.releaseState == GhostReleaseState::OrangeToCenter) {
        if (orange.offset == 0 && orange.x == 224 && orange.y == 224) {
            orange.direction = Direction::Up;
            orange.requestedDirection = Direction::Up;
            orange.releaseState = GhostReleaseState::OrangeExiting;
            game->visualDirty = true;
        }
        move_ghost_one_step(game, &orange);
        return;
    }

    if (orange.releaseState == GhostReleaseState::OrangeExiting) {
        if (orange.offset == 0 && orange.x == 224 && orange.y == 184) {
            // VB6 sets Offset=8 and then assigns a random horizontal
            // direction at the outside-house activation point. The source's
            // unseeded Rnd is made deterministic here by choosing Left,
            // matching the existing Red reset policy.
            orange.releaseState = GhostReleaseState::Normal;
            orange.collisionActive = true;
            orange.direction = Direction::Left;
            orange.requestedDirection = Direction::Left;
            orange.offset = 8;
            game->orangeReleaseCompleted = true;
            game->visualDirty = true;
            return;
        }
        move_ghost_one_step(game, &orange);
        if (orange.y < 184) {
            orange.y = 184;
            orange.offset = 8;
            orange.releaseState = GhostReleaseState::Normal;
            orange.collisionActive = true;
            orange.direction = Direction::Left;
            orange.requestedDirection = Direction::Left;
            game->orangeReleaseCompleted = true;
            game->visualDirty = true;
        }
    }
}

static void update_orange_ghost(GameState* game) {
    if (!game) return;
    GhostState& orange = game->ghosts[3];
    if (!orange.active) return;
    if (orange.condition == GhostCondition::Eaten || orange.condition == GhostCondition::Returning ||
        orange.releaseState == GhostReleaseState::ReturningHouse) {
        update_returning_ghost(game, &orange);
        return;
    }

    const GhostTarget target = calculate_orange_target(*game, game->pacman, orange);
    orange.targetX = target.x;
    orange.targetY = target.y;
    if (orange.releaseState != GhostReleaseState::Normal) {
        update_orange_house_release(game);
        return;
    }
    orange.collisionActive = true;
    update_active_ghost(game, &orange, orange.targetX, orange.targetY, true);
}

static void enter_dying(GameState* game) {
    if (!game || game->playState != PlayState::Playing) return;

    bool encounterWasActive = false;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        GhostState& ghost = game->ghosts[index];
        if (ghost.condition != GhostCondition::Normal || ghost.powerPillStepsRemaining != 0) {
            encounterWasActive = true;
        }
        ghost.condition = GhostCondition::Normal;
        ghost.powerPillStepsRemaining = 0;
        ghost.frightenedDelayToggle = false;
        restore_ghost_normal_speed(*game, &ghost);
        if (ghost.releaseState == GhostReleaseState::Normal) ghost.collisionActive = true;
        else ghost.collisionActive = false;
    }
    if (encounterWasActive) game->powerPillEncounterReset = true;

    game->playState = PlayState::Dying;
    game->collisionDetected = true;
    game->deathEntered = true;
    ++game->deathTransitions;
    game->deathStepsRemaining = kPacManDeathDurationSteps;
    game->deathAnimationFrame = 0;
    clear_held(&game->held);
    game->pacman.direction = Direction::None;
    game->pacman.requestedDirection = Direction::None;
    if (game->lives > 0) {
        --game->lives;
        game->lifeDecremented = true;
    }
    game->visualDirty = true;
}

static void update_dying(GameState* game) {
    if (!game) return;
    if (game->deathStepsRemaining > 0) --game->deathStepsRemaining;
    game->deathAnimationFrame = static_cast<uint8_t>((game->deathAnimationFrame + 1u) % 12u);
    game->visualDirty = true;

    if (game->deathStepsRemaining != 0) return;
    if (game->lives > 0) {
        reset_actor_positions(game);
        game->playState = PlayState::ReadyAfterDeath;
        game->readyStepsRemaining = kPacManReadyAfterDeathSteps;
        game->actorReset = true;
        game->visualDirty = true;
        return;
    }

    game->playState = PlayState::GameOver;
    ++game->gameOverTransitions;
    game->gameOverEntered = true;
    game->visualDirty = true;
}

static void update_ready_after_death(GameState* game) {
    if (!game) return;
    if (game->readyStepsRemaining > 0) --game->readyStepsRemaining;
    if (game->readyStepsRemaining == 0) {
        game->playState = PlayState::Playing;
        game->gameplayResumed = true;
        game->visualDirty = true;
    }
}

}

Direction choose_ghost_direction(const GameState& game, const GhostState& ghost,
                                 int targetX, int targetY) {
    return choose_red_direction_impl(game, ghost, targetX, targetY);
}

Direction choose_pink_direction(const GameState& game, const GhostState& ghost,
                                int targetX, int targetY) {
    return choose_pink_direction_impl(game, ghost, targetX, targetY);
}

Direction choose_cyan_direction(const GameState& game, const GhostState& ghost,
                                int targetX, int targetY) {
    // Cyan uses the same historical sign-priority chooser as the generic
    // Ghost(3) branch. Keeping this wrapper separate makes the Cyan rule
    // independently testable without changing Pink's public helper.
    return choose_pink_direction_impl(game, ghost, targetX, targetY);
}

Direction choose_orange_direction(const GameState& game, const GhostState& ghost,
                                  int targetX, int targetY) {
    // Orange is Ghost(4) in the same historical sign-priority branch. Keep a
    // named wrapper so its direction policy is independently testable.
    return choose_pink_direction_impl(game, ghost, targetX, targetY);
}

bool pacman_collides_with_ghost(const PacManState& pacman, const GhostState& ghost) {
    if (!ghost.active || !ghost.collisionActive) return false;
    return absolute_value(pacman.x - ghost.x) < 16 && absolute_value(pacman.y - ghost.y) < 16;
}

const char* ghost_kind_name(GhostKind kind) {
    switch (kind) {
    case GhostKind::Red: return "red";
    case GhostKind::Pink: return "pink";
    case GhostKind::Cyan: return "cyan";
    case GhostKind::Orange: return "orange";
    }
    return "unknown";
}

const char* ghost_condition_name(GhostCondition condition) {
    switch (condition) {
    case GhostCondition::Normal: return "normal";
    case GhostCondition::Frightened: return "frightened";
    case GhostCondition::Eaten: return "eaten";
    case GhostCondition::Returning: return "returning";
    }
    return "unknown";
}

const char* ghost_release_state_name(GhostReleaseState state) {
    switch (state) {
    case GhostReleaseState::Normal: return "normal";
    case GhostReleaseState::ReturningHouse: return "returning-house";
    case GhostReleaseState::PinkHouseBounce: return "house-bounce";
    case GhostReleaseState::PinkToCenter: return "to-center";
    case GhostReleaseState::PinkExiting: return "exiting";
    case GhostReleaseState::CyanHouseBounce: return "cyan-house-bounce";
    case GhostReleaseState::CyanExiting: return "cyan-exiting";
    case GhostReleaseState::OrangeHouseBounce: return "orange-house-bounce";
    case GhostReleaseState::OrangeToCenter: return "orange-to-center";
    case GhostReleaseState::OrangeExiting: return "orange-exiting";
    }
    return "unknown";
}

uint32_t game_frightened_duration_steps(const GameState& game) {
    return frightened_duration_steps(game);
}

uint32_t game_frightened_flash_threshold(const GameState& game) {
    return frightened_flash_threshold(game);
}

GhostTarget calculate_pink_target(const GameState& game, const PacManState& pacman) {
    // basGhostAI.bas stores Px/Py and X/Y as integer tile coordinates and
    // projects Pink four tiles (64 logical pixels) ahead only when the
    // historical Manhattan tile separation is greater than two.
    const int pacmanTileX = historical_tile_coordinate(pacman.x);
    const int pacmanTileY = historical_tile_coordinate(pacman.y);
    const int pinkTileX = historical_tile_coordinate(game.ghosts[1].x);
    const int pinkTileY = historical_tile_coordinate(game.ghosts[1].y);
    const bool farEnough = absolute_value(pacmanTileX - pinkTileX) +
        absolute_value(pacmanTileY - pinkTileY) > 2;

    int targetX = pacman.x;
    int targetY = pacman.y;
    if (farEnough) {
        const int projection = direction_x(pacman.direction) * 4 * kPacManTileSize;
        targetX += projection;
        // Historical coordinate quirk: the VB6 source uses XD(Direction) for
        // Py as well as Px. Preserve this observable result: left/right move
        // both coordinates; up/down project neither coordinate.
        targetY += projection;
    }

    targetY = clamp_int(targetY, 8, kPacManMazeHeight - 8);
    if (is_tunnel_row(level_row_from_position(targetY))) {
        targetX = wrap_tunnel_position(targetX);
    } else {
        targetX = clamp_int(targetX, 16, 431);
    }
    return GhostTarget{targetX, targetY};
}

GhostTarget calculate_cyan_target(const GameState& game, const PacManState& pacman,
                                  const GhostState& red) {
    // basGhostAI.bas identifies Cyan as Ghost(3). It compares integer tile
    // coordinates and, only when the separation is greater than three tiles,
    // adds eight tiles in the Pac-Man direction to BOTH Px and Py. The source
    // uses XD(Direction) for Py as well as Px. There is no Ghost(1)/Red term,
    // vector, or second projection in the authoritative VB6 routine; the Red
    // parameter is explicit for call-site/test isolation and is intentionally
    // unused rather than inventing arcade Inky behavior.
    (void)red;
    const int64_t pacmanTileX = historical_tile_coordinate(pacman.x);
    const int64_t pacmanTileY = historical_tile_coordinate(pacman.y);
    const int64_t cyanTileX = historical_tile_coordinate(game.ghosts[2].x);
    const int64_t cyanTileY = historical_tile_coordinate(game.ghosts[2].y);
    const int64_t tileDeltaX = pacmanTileX - cyanTileX;
    const int64_t tileDeltaY = pacmanTileY - cyanTileY;
    const int64_t tileDistance = (tileDeltaX < 0 ? -tileDeltaX : tileDeltaX) +
        (tileDeltaY < 0 ? -tileDeltaY : tileDeltaY);
    const bool farEnough = tileDistance > 3;

    int64_t targetX = pacman.x;
    int64_t targetY = pacman.y;
    if (farEnough) {
        const int64_t projection = static_cast<int64_t>(direction_x(pacman.direction)) *
            8ll * kPacManTileSize;
        targetX += projection;
        // Compatibility quirk: preserve XD(Direction) on the Y projection.
        targetY += projection;
    }

    const int64_t minimumInt = -2147483648ll;
    const int64_t maximumInt = 2147483647ll;
    if (targetX < minimumInt) targetX = minimumInt;
    if (targetX > maximumInt) targetX = maximumInt;
    if (targetY < minimumInt) targetY = minimumInt;
    if (targetY > maximumInt) targetY = maximumInt;
    return GhostTarget{static_cast<int>(targetX), static_cast<int>(targetY)};
}

int orange_distance_tiles(const PacManState& pacman, const GhostState& orange) {
    const int64_t pacmanTileX = historical_tile_coordinate(pacman.x);
    const int64_t pacmanTileY = historical_tile_coordinate(pacman.y);
    const int64_t orangeTileX = historical_tile_coordinate(orange.x);
    const int64_t orangeTileY = historical_tile_coordinate(orange.y);
    const int64_t deltaX = pacmanTileX - orangeTileX;
    const int64_t deltaY = pacmanTileY - orangeTileY;
    const int64_t magnitudeX = deltaX < 0 ? -deltaX : deltaX;
    const int64_t magnitudeY = deltaY < 0 ? -deltaY : deltaY;
    const int64_t total = magnitudeX + magnitudeY;
    return total > 0x7FFFFFFFll ? 0x7FFFFFFF : static_cast<int>(total);
}

bool orange_uses_far_target(const PacManState& pacman, const GhostState& orange) {
    return orange_distance_tiles(pacman, orange) > kOrangeTargetThresholdTiles;
}

GhostTarget calculate_orange_target(const GameState& game, const PacManState& pacman,
                                    const GhostState& orange) {
    (void)game;
    // basGhostAI.bas stores Pac-Man and Orange positions as integer tile
    // coordinates, and Ghost(4) projects only when their Manhattan tile
    // distance is strictly greater than four. The VB6 far target is twelve
    // tiles in XD(Pacman.Direction) for BOTH axes. For Down, XD(1) is the
    // historical zero-valued array slot, just as for Up; for Left/Right the
    // same sign is applied to X and Y. Near targeting is Pac-Man's current
    // logical coordinate, not a corner, relative/random point, or Red-based
    // target.
    int64_t targetX = pacman.x;
    int64_t targetY = pacman.y;
    if (orange_uses_far_target(pacman, orange)) {
        const int64_t projection = static_cast<int64_t>(direction_x(pacman.direction)) *
            kOrangeProjectionTiles * kPacManTileSize;
        targetX += projection;
        targetY += projection;
    }

    // Keep historical off-maze logical targets (for example, a left-facing
    // target near the left edge), but make conversion back to int defined.
    const int64_t minimumInt = -2147483648ll;
    const int64_t maximumInt = 2147483647ll;
    if (targetX < minimumInt) targetX = minimumInt;
    if (targetX > maximumInt) targetX = maximumInt;
    if (targetY < minimumInt) targetY = minimumInt;
    if (targetY > maximumInt) targetY = maximumInt;
    return GhostTarget{static_cast<int>(targetX), static_cast<int>(targetY)};
}

void game_initialize(GameState* game) {
    if (!game) return;
    game->gameSpeed = 1;
    game->levelNumber = 1;
    reset_actor_positions(game);
    level_initialize(&game->level);
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    game->score = 0;
    game->ghostEatChain = 0;
    game->frightenedFlashPhase = 0;
    game->lives = kPacManInitialLives;
    game->playState = PlayState::Playing;
    game->levelCompleteStepsRemaining = 0;
    game->deathStepsRemaining = 0;
    game->readyStepsRemaining = 0;
    game->deathAnimationFrame = 0;
    game->levelCompleteTransitions = 0;
    game->levelResetCount = 0;
    game->deathTransitions = 0;
    game->gameOverTransitions = 0;
    game->focused = true;
    game->visualDirty = true;
    game->turnAccepted = false;
    game->becameBlocked = false;
    game->tunnelWrapped = false;
    game->redTunnelWrapped = false;
    game->pinkTunnelWrapped = false;
    game->cyanTunnelWrapped = false;
    game->orangeTunnelWrapped = false;
    game->pinkReleaseStarted = false;
    game->pinkReleaseCompleted = false;
    game->cyanReleaseStarted = false;
    game->cyanReleaseCompleted = false;
    game->orangeReleaseStarted = false;
    game->orangeReleaseCompleted = false;
    game->normalPillConsumed = false;
    game->powerPillConsumed = false;
    game->scoreChanged = false;
    game->levelCompleteEntered = false;
    game->levelReset = false;
    game->countUnderflow = false;
    game->collisionDetected = false;
    game->deathEntered = false;
    game->lifeDecremented = false;
    game->actorReset = false;
    game->gameOverEntered = false;
    game->sessionRestarted = false;
    game->readyEntered = false;
    game->gameplayResumed = false;
    game->suppressGhostCollisionsForValidation = false;
    clear_power_event_flags(game);
    game->simulationSteps = 0;
}

void add_score(GameState& game, uint32_t points) {
    const uint32_t maximum = 0xFFFFFFFFu;
    if (maximum - game.score < points) game.score = maximum;
    else game.score += points;
    game.scoreChanged = true;
}

void game_reset_level(GameState* game) {
    if (!game) return;
    const uint32_t oldLevel = normalize_level_number(game->levelNumber);
    game->levelNumber = oldLevel < kPacManHistoricalMaximumLevel
        ? oldLevel + 1u : kPacManHistoricalMaximumLevel;
    level_initialize(&game->level);
    reset_actor_positions(game);
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    const GhostTarget cyanTarget = calculate_cyan_target(*game, game->pacman, game->ghosts[0]);
    game->ghosts[2].targetX = cyanTarget.x;
    game->ghosts[2].targetY = cyanTarget.y;
    game->playState = PlayState::ReadyAfterDeath;
    game->levelCompleteStepsRemaining = 0;
    game->deathStepsRemaining = 0;
    game->readyStepsRemaining = calculate_level_rules(game->levelNumber).readyDurationSteps;
    game->ghostEatChain = 0;
    game->frightenedFlashPhase = 0;
    clear_held(&game->held);
    game->pacman.requestedDirection = Direction::None;
    game->visualDirty = true;
    game->levelReset = true;
    game->actorReset = true;
    game->readyEntered = true;
    game->powerPillEncounterReset = true;
    ++game->levelResetCount;
}

bool game_restart_session(GameState* game) {
    if (!game || game->playState != PlayState::GameOver) return false;
    game->gameSpeed = 1;
    game->levelNumber = 1;
    level_initialize(&game->level);
    reset_actor_positions(game);
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    game->score = 0;
    game->ghostEatChain = 0;
    game->frightenedFlashPhase = 0;
    game->lives = kPacManInitialLives;
    game->playState = PlayState::ReadyAfterDeath;
    game->levelCompleteStepsRemaining = 0;
    game->deathStepsRemaining = 0;
    game->readyStepsRemaining = calculate_level_rules(game->levelNumber).readyDurationSteps;
    game->deathAnimationFrame = 0;
    game->levelCompleteTransitions = 0;
    game->levelResetCount = 0;
    game->deathTransitions = 0;
    game->gameOverTransitions = 0;
    game->visualDirty = true;
    game->sessionRestarted = true;
    game->turnAccepted = false;
    game->becameBlocked = false;
    game->tunnelWrapped = false;
    game->redTunnelWrapped = false;
    game->pinkTunnelWrapped = false;
    game->cyanTunnelWrapped = false;
    game->orangeTunnelWrapped = false;
    game->pinkReleaseStarted = false;
    game->pinkReleaseCompleted = false;
    game->cyanReleaseStarted = false;
    game->cyanReleaseCompleted = false;
    game->orangeReleaseStarted = false;
    game->orangeReleaseCompleted = false;
    game->normalPillConsumed = false;
    game->powerPillConsumed = false;
    game->scoreChanged = false;
    game->levelCompleteEntered = false;
    game->levelReset = false;
    game->countUnderflow = false;
    game->collisionDetected = false;
    game->deathEntered = false;
    game->lifeDecremented = false;
    game->actorReset = false;
    game->gameOverEntered = false;
    game->readyEntered = true;
    game->gameplayResumed = false;
    game->simulationSteps = 0;
    clear_power_event_flags(game);
    return true;
}

Direction game_direction_for_key(int keyCode) {
    switch (keyCode) {
    case GX_KEY_LEFT: return Direction::Left;
    case GX_KEY_RIGHT: return Direction::Right;
    case GX_KEY_UP: return Direction::Up;
    case GX_KEY_DOWN: return Direction::Down;
    default: return Direction::None;
    }
}

void game_press_direction(GameState* game, Direction direction) {
    if (!game || direction == Direction::None || game->playState != PlayState::Playing) return;
    if (game->pacman.direction == Direction::None) game->pacman.direction = game->pacman.facingDirection;
    set_held(&game->held, direction, true);
    game->pacman.requestedDirection = direction;
    game->visualDirty = true;
}

void game_release_direction(GameState* game, Direction direction) {
    if (!game || direction == Direction::None) return;
    set_held(&game->held, direction, false);
}

void game_focus_lost(GameState* game) {
    if (!game) return;
    clear_held(&game->held);
    game->pacman.requestedDirection = Direction::None;
    game->pacman.direction = Direction::None;
    game->focused = false;
    game->visualDirty = true;
}

void game_focus_gained(GameState* game) {
    if (!game) return;
    game->focused = true;
}

void game_update(GameState* game) {
    if (!game) return;

    game->turnAccepted = false;
    game->becameBlocked = false;
    game->tunnelWrapped = false;
    game->redTunnelWrapped = false;
    game->pinkTunnelWrapped = false;
    game->cyanTunnelWrapped = false;
    game->orangeTunnelWrapped = false;
    game->pinkReleaseStarted = false;
    game->pinkReleaseCompleted = false;
    game->cyanReleaseStarted = false;
    game->cyanReleaseCompleted = false;
    game->orangeReleaseStarted = false;
    game->orangeReleaseCompleted = false;
    game->normalPillConsumed = false;
    game->powerPillConsumed = false;
    game->scoreChanged = false;
    game->levelCompleteEntered = false;
    game->levelReset = false;
    game->countUnderflow = false;
    game->collisionDetected = false;
    game->deathEntered = false;
    game->lifeDecremented = false;
    game->actorReset = false;
    game->gameOverEntered = false;
    game->readyEntered = false;
    game->gameplayResumed = false;
    clear_power_event_flags(game);

    if (!game->focused) return;

    if (game->playState == PlayState::Dying) {
        update_dying(game);
        ++game->simulationSteps;
        return;
    }
    if (game->playState == PlayState::ReadyAfterDeath) {
        update_ready_after_death(game);
        ++game->simulationSteps;
        return;
    }
    if (game->playState == PlayState::LevelComplete) {
        if (game->levelCompleteStepsRemaining > 0) --game->levelCompleteStepsRemaining;
        if (game->levelCompleteStepsRemaining == 0) game_reset_level(game);
        ++game->simulationSteps;
        return;
    }
    if (game->playState == PlayState::GameOver) {
        ++game->simulationSteps;
        return;
    }

    PacManState& pacman = game->pacman;
    if (pacman.offset == 0) {
        if (pacman.requestedDirection != Direction::None &&
            can_move(*game, pacman.x, pacman.y, pacman.requestedDirection)) {
            if (pacman.direction != pacman.requestedDirection) game->turnAccepted = true;
            pacman.direction = pacman.requestedDirection;
            pacman.facingDirection = pacman.direction;
            game->visualDirty = true;
        }

        if (pacman.direction != Direction::None && !can_move(*game, pacman.x, pacman.y, pacman.direction)) {
            pacman.direction = Direction::None;
            game->becameBlocked = true;
        }

        // Historical order is retained: consume the next tile before the
        // actor moves; completion therefore wins over a same-step collision.
        if (pacman.direction != Direction::None) consume_target_pill(game);
        if (game->playState == PlayState::LevelComplete) {
            ++game->simulationSteps;
            return;
        }
    }

    if (pacman.direction != Direction::None) {
        pacman.facingDirection = pacman.direction;
        const int oldX = pacman.x;
        const int oldY = pacman.y;
        pacman.x += direction_x(pacman.direction) * pacman.speed;
        pacman.y += direction_y(pacman.direction) * pacman.speed;
        pacman.offset = next_offset(pacman.offset, pacman.direction, pacman.speed);

        if (is_horizontal(pacman.direction) && is_tunnel_row(level_row_from_position(pacman.y))) {
            if (pacman.x > 416) {
                pacman.x -= 416;
                game->tunnelWrapped = true;
            } else if (pacman.x < 16) {
                pacman.x += 416;
                game->tunnelWrapped = true;
            }
        }
        if (oldX != pacman.x || oldY != pacman.y) game->visualDirty = true;
    }

    const int oldMouth = pacman.mouth;
    ++pacman.mouthSpeed;
    if (pacman.mouthSpeed > 4) {
        pacman.mouth += pacman.mouthDirection;
        pacman.mouthSpeed = 0;
    }
    if (pacman.mouth > 2 || pacman.mouth < 1) pacman.mouthDirection = -pacman.mouthDirection;
    if (pacman.mouth != oldMouth) game->visualDirty = true;

    // Update ordering is explicit: Pac-Man input and pill look-ahead, Pac-Man
    // movement/animation, Red, Pink, Cyan, Orange (with each ghost's timer
    // decremented at the historical end of its own AI pass), then one
    // collision sample.
    // Cyan observes Red's post-move position at this point; the source-
    // faithful Cyan target currently does not consume that position. Orange
    // observes Pac-Man after movement and the preceding ghosts after their
    // own updates, matching the VB6 loop order.
    update_red_ghost(game);
    advance_power_pill_timer(game, &game->ghosts[0]);
    update_pink_ghost(game);
    advance_power_pill_timer(game, &game->ghosts[1]);
    update_cyan_ghost(game);
    advance_power_pill_timer(game, &game->ghosts[2]);
    update_orange_ghost(game);
    advance_power_pill_timer(game, &game->ghosts[3]);
    if (!game->suppressGhostCollisionsForValidation) resolve_ghost_collisions(game);
    ++game->frightenedFlashPhase;
    game->frightenedFlashPhase %= 16u;
    ++game->simulationSteps;
}

const char* game_direction_name(Direction direction) {
    switch (direction) {
    case Direction::Up: return "up";
    case Direction::Down: return "down";
    case Direction::Left: return "left";
    case Direction::Right: return "right";
    case Direction::None: return "none";
    }
    return "unknown";
}
