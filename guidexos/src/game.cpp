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
    return value < 0 ? -value : value;
}

// Count aligned house bounces, not render or simulation ticks. Two direction
// reversals reproduce the short historical vertical wait before Pink heads to
// the center of the box.
static const uint32_t kPinkHouseBounceSteps = 2u;

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int historical_tile_coordinate(int position) {
    if (position >= 0) return position / kPacManTileSize;
    return -(((-position) + kPacManTileSize - 1) / kPacManTileSize);
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

static void reset_pacman(PacManState* pacman) {
    if (!pacman) return;
    pacman->x = 224;
    pacman->y = 376;
    pacman->direction = Direction::Right;
    pacman->facingDirection = Direction::Right;
    pacman->requestedDirection = Direction::Right;
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
    ghosts[2].speed = 0;
    ghosts[2].targetX = 0;
    ghosts[2].targetY = 0;
    ghosts[2].animationFrame = 0;
    ghosts[2].active = true;
    ghosts[2].collisionActive = true;
    ghosts[2].releaseState = GhostReleaseState::Normal;
    ghosts[2].releaseStepsRemaining = 0;

    ghosts[3].kind = GhostKind::Orange;
    ghosts[3].x = 256;
    ghosts[3].y = 224;
    ghosts[3].direction = Direction::Up;
    ghosts[3].requestedDirection = Direction::Up;
    ghosts[3].offset = 0;
    ghosts[3].speed = 0;
    ghosts[3].targetX = 0;
    ghosts[3].targetY = 0;
    ghosts[3].animationFrame = 0;
    ghosts[3].active = true;
    ghosts[3].collisionActive = true;
    ghosts[3].releaseState = GhostReleaseState::Normal;
    ghosts[3].releaseStepsRemaining = 0;
}

static void reset_actor_positions(GameState* game) {
    if (!game) return;
    reset_pacman(&game->pacman);
    reset_ghosts(game->ghosts);
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    clear_held(&game->held);
    game->deathAnimationFrame = 0;
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
    }

    if (game->level.totalConsumablesRemaining == 0) {
        game->playState = PlayState::LevelComplete;
        game->levelCompleteStepsRemaining = kPacManLevelCompleteDelaySteps;
        ++game->levelCompleteTransitions;
        game->levelCompleteEntered = true;
    }
}

static bool pacman_hits_any_ghost(const GameState& game) {
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        if (pacman_collides_with_ghost(game.pacman, game.ghosts[index])) return true;
    }
    return false;
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
    const int deltaX = targetX - ghost.x;
    const int deltaY = targetY - ghost.y;
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

static void move_ghost_one_step(GameState* game, GhostState* ghost) {
    if (!game || !ghost || ghost->direction == Direction::None || ghost->speed <= 0) return;
    // The historical level data represents the house door as part of the
    // sprite/box convention rather than a normal maze cell. Permit only
    // Pink's fixed x=224 upward door lane during its release; normal maze
    // targeting still uses can_move with no house exception.
    const bool pinkHouseDoor = ghost->kind == GhostKind::Pink &&
        ghost->releaseState == GhostReleaseState::PinkExiting &&
        ghost->x == 224 && ghost->direction == Direction::Up &&
        ghost->y >= 184 && ghost->y <= 224;
    if (ghost->offset == 0 && !can_move(*game, ghost->x, ghost->y, ghost->direction) &&
        !pinkHouseDoor) {
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
        } else if (ghost->x < 16) {
            ghost->x += 416;
            if (ghost->kind == GhostKind::Red) game->redTunnelWrapped = true;
            if (ghost->kind == GhostKind::Pink) game->pinkTunnelWrapped = true;
        }
    }
    ghost->animationFrame = static_cast<uint8_t>((ghost->animationFrame + 1u) % 2u);
    if (oldX != ghost->x || oldY != ghost->y) game->visualDirty = true;
}

static void update_moving_ghost(GameState* game, GhostState* ghost,
                                int targetX, int targetY, bool pinkPolicy) {
    if (!game || !ghost || !ghost->active || ghost->speed <= 0) return;
    if (ghost->offset == 0) {
        const Direction selected = pinkPolicy
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

    // Direction is selected only at an aligned tile center. The committed
    // direction then carries the ghost through the next 15 logical pixels.
    move_ghost_one_step(game, ghost);
}

static void update_red_ghost(GameState* game) {
    if (!game) return;
    GhostState& red = game->ghosts[0];
    red.targetX = game->pacman.x;
    red.targetY = game->pacman.y;
    update_moving_ghost(game, &red, red.targetX, red.targetY, false);
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
    const GhostTarget target = calculate_pink_target(*game, game->pacman);
    pink.targetX = target.x;
    pink.targetY = target.y;
    if (pink.releaseState != GhostReleaseState::Normal) {
        update_pink_house_release(game);
        return;
    }
    pink.collisionActive = true;
    update_moving_ghost(game, &pink, pink.targetX, pink.targetY, true);
}

static void enter_dying(GameState* game) {
    if (!game || game->playState != PlayState::Playing) return;

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

const char* ghost_release_state_name(GhostReleaseState state) {
    switch (state) {
    case GhostReleaseState::Normal: return "normal";
    case GhostReleaseState::PinkHouseBounce: return "house-bounce";
    case GhostReleaseState::PinkToCenter: return "to-center";
    case GhostReleaseState::PinkExiting: return "exiting";
    }
    return "unknown";
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

void game_initialize(GameState* game) {
    if (!game) return;
    reset_actor_positions(game);
    level_initialize(&game->level);
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    game->score = 0;
    game->levelNumber = 1;
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
    game->pinkReleaseStarted = false;
    game->pinkReleaseCompleted = false;
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
    game->suppressGhostCollisionsForValidation = false;
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
    level_initialize(&game->level);
    if (game->levelNumber != 0xFFFFFFFFu) ++game->levelNumber;
    reset_actor_positions(game);
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    game->playState = PlayState::Playing;
    game->levelCompleteStepsRemaining = 0;
    game->deathStepsRemaining = 0;
    game->readyStepsRemaining = 0;
    game->visualDirty = true;
    game->levelReset = true;
    ++game->levelResetCount;
}

bool game_restart_session(GameState* game) {
    if (!game || game->playState != PlayState::GameOver) return false;
    level_initialize(&game->level);
    reset_actor_positions(game);
    game->ghosts[0].targetX = game->pacman.x;
    game->ghosts[0].targetY = game->pacman.y;
    const GhostTarget pinkTarget = calculate_pink_target(*game, game->pacman);
    game->ghosts[1].targetX = pinkTarget.x;
    game->ghosts[1].targetY = pinkTarget.y;
    game->score = 0;
    game->levelNumber = 1;
    game->lives = kPacManInitialLives;
    game->playState = PlayState::ReadyAfterDeath;
    game->levelCompleteStepsRemaining = 0;
    game->deathStepsRemaining = 0;
    game->readyStepsRemaining = kPacManReadyAfterDeathSteps;
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
    game->pinkReleaseStarted = false;
    game->pinkReleaseCompleted = false;
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
    game->simulationSteps = 0;
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
    game->pinkReleaseStarted = false;
    game->pinkReleaseCompleted = false;
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

    // Update ordering is explicit: Pac-Man, mouth animation, Red, Pink, then
    // one collision sample. Cyan and Orange remain stationary. Pink targets
    // the Pac-Man position produced by this step, not the previous frame.
    update_red_ghost(game);
    update_pink_ghost(game);
    if (!game->suppressGhostCollisionsForValidation && pacman_hits_any_ghost(*game)) {
        enter_dying(game);
    }
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
