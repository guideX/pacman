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

}

void game_initialize(GameState* game) {
    if (!game) return;
    game->pacman.x = 224;
    game->pacman.y = 376;
    game->pacman.direction = Direction::Right;
    game->pacman.facingDirection = Direction::Right;
    game->pacman.requestedDirection = Direction::Right;
    game->pacman.offset = 8;
    game->pacman.speed = 1;
    game->pacman.mouth = 0;
    game->pacman.mouthDirection = 1;
    game->pacman.mouthSpeed = 0;
    game->held.left = false;
    game->held.right = false;
    game->held.up = false;
    game->held.down = false;
    game->focused = true;
    game->visualDirty = true;
    game->turnAccepted = false;
    game->becameBlocked = false;
    game->tunnelWrapped = false;
    game->simulationSteps = 0;
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
    if (!game || direction == Direction::None) return;
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
    game->held.left = false;
    game->held.right = false;
    game->held.up = false;
    game->held.down = false;
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
    if (!game || !game->focused) return;

    game->turnAccepted = false;
    game->becameBlocked = false;
    game->tunnelWrapped = false;

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
