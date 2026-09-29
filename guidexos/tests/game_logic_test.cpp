#include "game.h"
#include "level.h"
#include "level_rules.h"
#include "renderer.h"

#include <iostream>
#include <cstring>

namespace {

bool expect(bool condition, const char* label) {
    if (!condition) std::cerr << "FAIL: " << label << "\n";
    return condition;
}

bool reaches_direction(GameState* game, Direction direction, int maxSteps) {
    for (int i = 0; i < maxSteps; ++i) {
        game_update(game);
        if (game->pacman.direction == direction) return true;
    }
    return false;
}

void set_before_target(GameState* game, int targetColumn, int targetRow, Direction direction) {
    const int dx = direction == Direction::Left ? -1 : direction == Direction::Right ? 1 : 0;
    const int dy = direction == Direction::Up ? -1 : direction == Direction::Down ? 1 : 0;
    int currentColumn = targetColumn - dx;
    int currentRow = targetRow - dy;
    if (currentColumn < 0) currentColumn += kPacManMazeColumns;
    if (currentColumn >= kPacManMazeColumns) currentColumn -= kPacManMazeColumns;
    game->pacman.x = currentColumn * kPacManTileSize + 8;
    game->pacman.y = currentRow * kPacManTileSize + 8;
    game->pacman.offset = 0;
    game->pacman.direction = direction;
    game->pacman.facingDirection = direction;
    game->pacman.requestedDirection = direction;
    game->pacman.speed = 1;
}

void keep_only_normal_target(GameState* game, int column, int row) {
    for (int y = 0; y < kPacManMazeRows; ++y) {
        for (int x = 0; x < kPacManMazeColumns; ++x) {
            if (game->level.cells[y][x] == CellType::Pill ||
                game->level.cells[y][x] == CellType::PowerPill) {
                game->level.cells[y][x] = CellType::Empty;
            }
        }
    }
    game->level.cells[row][column] = CellType::Pill;
    game->level.normalPillsRemaining = 1;
    game->level.powerPillsRemaining = 0;
    game->level.totalConsumablesRemaining = 1;
}

Direction opposite_direction(Direction direction) {
    switch (direction) {
    case Direction::Up: return Direction::Down;
    case Direction::Down: return Direction::Up;
    case Direction::Left: return Direction::Right;
    case Direction::Right: return Direction::Left;
    case Direction::None: return Direction::None;
    }
    return Direction::None;
}

int direction_x_for_test(Direction direction) {
    return direction == Direction::Left ? -1 : direction == Direction::Right ? 1 : 0;
}

int direction_y_for_test(Direction direction) {
    return direction == Direction::Up ? -1 : direction == Direction::Down ? 1 : 0;
}

bool is_horizontal_for_test(Direction direction) {
    return direction == Direction::Left || direction == Direction::Right;
}

void disable_non_red_ghosts(GameState* game) {
    if (!game) return;
    for (uint32_t index = 1; index < kPacManGhostCount; ++index) game->ghosts[index].active = false;
}

void disable_non_pink_ghosts(GameState* game) {
    if (!game) return;
    game->ghosts[0].active = false;
    game->ghosts[2].active = false;
    game->ghosts[3].active = false;
}

void disable_non_cyan_ghosts(GameState* game) {
    if (!game) return;
    game->ghosts[0].active = false;
    game->ghosts[1].active = false;
    game->ghosts[3].active = false;
}

void disable_non_orange_ghosts(GameState* game) {
    if (!game) return;
    game->ghosts[0].active = false;
    game->ghosts[1].active = false;
    game->ghosts[2].active = false;
}

void enable_normal_pink(GameState* game) {
    if (!game) return;
    GhostState& pink = game->ghosts[1];
    pink.active = true;
    pink.collisionActive = true;
    pink.releaseState = GhostReleaseState::Normal;
    pink.releaseStepsRemaining = 0;
    pink.speed = 1;
}

void enable_normal_cyan(GameState* game) {
    if (!game) return;
    GhostState& cyan = game->ghosts[2];
    cyan.active = true;
    cyan.collisionActive = true;
    cyan.releaseState = GhostReleaseState::Normal;
    cyan.releaseStepsRemaining = 0;
    cyan.speed = 1;
}

void enable_normal_orange(GameState* game) {
    if (!game) return;
    GhostState& orange = game->ghosts[3];
    orange.active = true;
    orange.collisionActive = true;
    orange.releaseState = GhostReleaseState::Normal;
    orange.releaseStepsRemaining = 0;
    orange.speed = 1;
}

void enable_all_normal_ghosts_for_power(GameState* game) {
    if (!game) return;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        GhostState& ghost = game->ghosts[index];
        ghost.active = true;
        ghost.collisionActive = true;
        ghost.releaseState = GhostReleaseState::Normal;
        ghost.condition = GhostCondition::Normal;
        ghost.powerPillStepsRemaining = 0;
        ghost.direction = Direction::Right;
        ghost.requestedDirection = Direction::Right;
        ghost.offset = 1;
        ghost.speed = 0;
        ghost.x = 120 + static_cast<int>(index) * 40;
        ghost.y = 40;
    }
}

void keep_only_power_target(GameState* game, int column, int row) {
    if (!game) return;
    for (int y = 0; y < kPacManMazeRows; ++y) {
        for (int x = 0; x < kPacManMazeColumns; ++x) {
            if (game->level.cells[y][x] == CellType::Pill ||
                game->level.cells[y][x] == CellType::PowerPill) {
                game->level.cells[y][x] = CellType::Empty;
            }
        }
    }
    game->level.cells[row][column] = CellType::PowerPill;
    game->level.normalPillsRemaining = 0;
    game->level.powerPillsRemaining = 1;
    game->level.totalConsumablesRemaining = 1;
}

void place_stationary_collision(GameState* game, GhostKind kind);

bool find_tie_point(const GameState& game, int* x, int* y, Direction* current,
                    Direction* expected) {
    const Direction directions[] = {
        Direction::Up, Direction::Down, Direction::Left, Direction::Right
    };
    for (int row = 0; row < kPacManMazeRows; ++row) {
        if (row == kPacManTunnelRow) continue;
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            if (!is_walkable_cell(level_cell(game.level, column, row))) continue;
            const int centerX = column * kPacManTileSize + 8;
            const int centerY = row * kPacManTileSize + 8;
            for (uint32_t currentIndex = 0; currentIndex < 4; ++currentIndex) {
                uint32_t legalCount = 0;
                Direction first = Direction::None;
                for (uint32_t index = 0; index < 4; ++index) {
                    if (directions[index] == opposite_direction(directions[currentIndex])) continue;
                    if (!can_move(game, centerX, centerY, directions[index])) continue;
                    if (first == Direction::None) first = directions[index];
                    ++legalCount;
                }
                if (legalCount >= 2) {
                    if (x) *x = centerX;
                    if (y) *y = centerY;
                    if (current) *current = directions[currentIndex];
                    if (expected) *expected = first;
                    return true;
                }
            }
        }
    }
    return false;
}

bool find_perpendicular_approach(const GameState& game, int* x, int* y,
                                 Direction* redDirection, Direction* pacmanDirection) {
    const Direction horizontal[] = {Direction::Left, Direction::Right};
    const Direction vertical[] = {Direction::Up, Direction::Down};
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            if (!is_walkable_cell(level_cell(game.level, column, row))) continue;
            const int centerX = column * kPacManTileSize + 8;
            const int centerY = row * kPacManTileSize + 8;
            for (uint32_t redIndex = 0; redIndex < 2; ++redIndex) {
                if (!can_move(game, centerX, centerY, horizontal[redIndex])) continue;
                for (uint32_t pacmanIndex = 0; pacmanIndex < 2; ++pacmanIndex) {
                    if (x) *x = centerX;
                    if (y) *y = centerY;
                    if (redDirection) *redDirection = horizontal[redIndex];
                    if (pacmanDirection) *pacmanDirection = vertical[pacmanIndex];
                    return true;
                }
            }
        }
    }
    return false;
}

bool test_historical_movement() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    ok &= expect(game.pacman.x == 224 && game.pacman.y == 376, "historical starting coordinates");
    ok &= expect(game.pacman.direction == Direction::Right, "historical starting direction");
    ok &= expect(game.pacman.speed == 1, "normal movement speed");
    ok &= expect(!is_walkable_tile(0, 0) && is_walkable_tile(14, 23), "historical wall semantics");
    ok &= expect(!can_move(game, 232, 376, Direction::Up), "wall blocks upward movement");

    for (int i = 0; i < 8; ++i) game_update(&game);
    ok &= expect(game.pacman.x == 232 && game.pacman.offset == 0, "fixed-step grid alignment");

    game_initialize(&game);
    game_press_direction(&game, Direction::Up);
    game_update(&game);
    ok &= expect(game.pacman.direction == Direction::Right && game.pacman.x == 225,
                 "buffered turn waits before a legal intersection");
    ok &= expect(reaches_direction(&game, Direction::Up, 300), "buffered turn eventually accepted");

    game_initialize(&game);
    game_update(&game);
    game_press_direction(&game, Direction::Left);
    game_update(&game);
    ok &= expect(game.pacman.direction == Direction::Right, "reversal waits for historical alignment");
    ok &= expect(reaches_direction(&game, Direction::Left, 32), "aligned reversal accepted");

    game_initialize(&game);
    game.pacman.x = 8;
    game.pacman.y = 232;
    game.pacman.offset = 0;
    game.pacman.direction = Direction::Left;
    game.pacman.requestedDirection = Direction::Left;
    game_update(&game);
    ok &= expect(game.tunnelWrapped && game.pacman.x == 423, "left-to-right tunnel wrap");

    game_initialize(&game);
    game.pacman.x = 424;
    game.pacman.y = 232;
    game.pacman.offset = 0;
    game.pacman.direction = Direction::Right;
    game.pacman.requestedDirection = Direction::Right;
    game_update(&game);
    ok &= expect(game.tunnelWrapped && game.pacman.x == 9, "right-to-left tunnel wrap");

    game_initialize(&game);
    const int initialX = game.pacman.x;
    game_focus_lost(&game);
    game_update(&game);
    ok &= expect(!game.focused && game.pacman.x == initialX && game.pacman.direction == Direction::None,
                 "focus loss clears held movement");
    game_focus_gained(&game);
    game_update(&game);
    ok &= expect(game.pacman.x == initialX, "focus return waits for new input");
    game_press_direction(&game, Direction::Right);
    for (int i = 0; i < 8; ++i) game_update(&game);
    ok &= expect(game.pacman.x > initialX, "movement resumes after new input");
    ok &= expect(game.pacman.facingDirection == Direction::Right, "animation orientation tracks direction");
    return ok;
}

bool test_pink_target_and_release_state() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    const GhostState& pink = game.ghosts[1];
    ok &= expect(pink.x == 192 && pink.y == 224, "Pink starts at the historical house position");
    ok &= expect(pink.direction == Direction::Up, "Pink starts facing Up");
    ok &= expect(pink.releaseState == GhostReleaseState::PinkHouseBounce &&
                 !pink.collisionActive && pink.speed == 1,
                 "Pink starts inside the house in an inactive bounded release state");

    PacManState pacman = game.pacman;
    pacman.x = 128;
    pacman.y = 128;
    pacman.speed = 0;
    pacman.offset = 1;
    pacman.requestedDirection = Direction::None;
    const Direction directions[] = {
        Direction::Right, Direction::Left, Direction::Down, Direction::Up
    };
    const int expectedX[] = {192, 64, 128, 128};
    const int expectedY[] = {192, 64, 128, 128};
    for (int index = 0; index < 4; ++index) {
        pacman.direction = directions[index];
        pacman.facingDirection = directions[index];
        const GhostTarget target = calculate_pink_target(game, pacman);
        ok &= expect(target.x == expectedX[index] && target.y == expectedY[index],
                     "Pink target projects four tiles with the exact direction formula");
    }
    pacman.direction = Direction::Right;
    const GhostTarget rightTarget = calculate_pink_target(game, pacman);
    pacman.direction = Direction::Up;
    const GhostTarget upTarget = calculate_pink_target(game, pacman);
    ok &= expect(rightTarget.y == pacman.y + 64 && upTarget.y == pacman.y,
                 "Pink preserves the historical XD-for-Y projection quirk");

    pacman.x = 192;
    pacman.y = 224;
    pacman.direction = Direction::Right;
    pacman.facingDirection = Direction::Right;
    const GhostTarget nearTarget = calculate_pink_target(game, pacman);
    ok &= expect(nearTarget.x == pacman.x && nearTarget.y == pacman.y,
                 "Pink does not project when historical tile separation is at most two");
    return ok;
}

bool test_pink_release_and_movement() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    disable_non_pink_ghosts(&game);
    game.pacman.x = 64;
    game.pacman.y = 232;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;

    bool sawStart = false;
    bool sawCenterRoute = false;
    bool sawExitRoute = false;
    bool completed = false;
    for (uint32_t step = 0; step < 260; ++step) {
        game_update(&game);
        const GhostState& pink = game.ghosts[1];
        if (pink.releaseState == GhostReleaseState::PinkHouseBounce) {
            sawStart = true;
            ok &= expect(pink.x == 192 && pink.y >= 224 && pink.y <= 240,
                         "Pink house bounce stays on its historical vertical lane");
        } else if (pink.releaseState == GhostReleaseState::PinkToCenter) {
            sawCenterRoute = true;
            ok &= expect(pink.y == 224 && pink.x >= 192 && pink.x <= 224,
                         "Pink leaves the house only through the deterministic center route");
        } else if (pink.releaseState == GhostReleaseState::PinkExiting) {
            sawExitRoute = true;
            ok &= expect(pink.x == 224 && pink.y >= 184 && pink.y <= 224,
                         "Pink exits upward through the legal house lane");
        } else if (pink.releaseState == GhostReleaseState::Normal) {
            completed = true;
            break;
        }
    }
    ok &= expect(sawStart && sawCenterRoute && sawExitRoute && completed,
                 "Pink release begins, exits once, and activates normal targeting");
    ok &= expect(game.ghosts[1].x == 224 && game.ghosts[1].y == 184 &&
                 game.ghosts[1].collisionActive && game.pinkReleaseCompleted,
                 "Pink becomes collision-active at the historical outside-house position");

    game.pacman.x = 288;
    game.pacman.y = 184;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    const int beforeX = game.ghosts[1].x;
    for (uint32_t step = 0; step < 8; ++step) {
        game_update(&game);
        const int column = level_column_from_position(game.ghosts[1].x);
        const int row = level_row_from_position(game.ghosts[1].y);
        ok &= expect(is_walkable_cell(level_cell(game.level, column, row)),
                     "Moving Pink remains in walkable maze cells");
    }
    ok &= expect(game.ghosts[1].x > beforeX, "Pink continues through a legal corridor at one pixel per step");
    return ok;
}

bool test_pink_direction_selection_and_tunnels() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    GhostState& pink = game.ghosts[1];
    pink.x = 208;
    pink.y = 184;
    pink.direction = Direction::Left;
    pink.requestedDirection = Direction::Left;
    pink.offset = 0;
    ok &= expect(choose_pink_direction(game, pink, 208, 24) == Direction::Up,
                 "Pink chooses a legal target-directed turn without reversing");
    const Direction first = choose_pink_direction(game, pink, 208, 24);
    const Direction second = choose_pink_direction(game, pink, 208, 24);
    ok &= expect(first == second, "Pink direction selection is deterministic");

    bool foundHorizontalPriority = false;
    for (int row = 0; row < kPacManMazeRows && !foundHorizontalPriority; ++row) {
        for (int column = 0; column < kPacManMazeColumns && !foundHorizontalPriority; ++column) {
            const int x = column * kPacManTileSize + 8;
            const int y = row * kPacManTileSize + 8;
            if (!can_move(game, x, y, Direction::Up) ||
                !can_move(game, x, y, Direction::Right)) continue;
            pink.x = x;
            pink.y = y;
            pink.direction = Direction::Up;
            pink.requestedDirection = Direction::Up;
            pink.offset = 0;
            if (choose_pink_direction(game, pink, x + 64, y - 64) == Direction::Right) {
                foundHorizontalPriority = true;
            }
        }
    }
    ok &= expect(foundHorizontalPriority,
                 "Pink preserves historical horizontal-over-vertical sign priority");

    GameState deadEndGame = game;
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            deadEndGame.level.cells[row][column] = CellType::Wall;
        }
    }
    deadEndGame.level.cells[5][5] = CellType::Empty;
    deadEndGame.level.cells[4][5] = CellType::Empty;
    GhostState deadEndPink = deadEndGame.ghosts[1];
    deadEndPink.x = 5 * kPacManTileSize + 8;
    deadEndPink.y = 5 * kPacManTileSize + 8;
    deadEndPink.direction = Direction::Down;
    deadEndPink.requestedDirection = Direction::Down;
    deadEndPink.offset = 0;
    ok &= expect(choose_pink_direction(deadEndGame, deadEndPink, 224, 376) == Direction::Up,
                 "Pink reverses only at a dead end");

    game_initialize(&game);
    disable_non_pink_ghosts(&game);
    enable_normal_pink(&game);
    pink = game.ghosts[1];
    pink.releaseState = GhostReleaseState::Normal;
    pink.collisionActive = true;
    pink.x = 16;
    pink.y = 232;
    pink.direction = Direction::Left;
    pink.requestedDirection = Direction::Left;
    pink.offset = 1;
    game.pacman.x = 208;
    game.pacman.y = 376;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.ghosts[1].x == 431 && game.ghosts[1].y == 232 && game.pinkTunnelWrapped,
                 "Pink wraps left-to-right through the horizontal tunnel");

    pink.x = 424;
    pink.y = 232;
    pink.direction = Direction::Right;
    pink.requestedDirection = Direction::Right;
    pink.offset = 1;
    game_update(&game);
    ok &= expect(pink.x == 9 && pink.y == 232 && game.pinkTunnelWrapped,
                 "Pink wraps right-to-left through the horizontal tunnel");

    pink.x = 16;
    pink.y = 216;
    pink.direction = Direction::Left;
    pink.requestedDirection = Direction::Left;
    pink.offset = 0;
    game_update(&game);
    ok &= expect(pink.x == 16 && pink.y == 216 && !game.pinkTunnelWrapped,
                 "Pink never wraps on a non-tunnel row");
    return ok;
}

bool test_cyan_target_and_release_state() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    const GhostState& initialCyan = game.ghosts[2];
    ok &= expect(initialCyan.x == 224 && initialCyan.y == 240,
                 "Cyan starts at the historical middle-house position");
    ok &= expect(initialCyan.direction == Direction::Down && initialCyan.speed == 1,
                 "Cyan starts facing Down at the historical speed");
    ok &= expect(initialCyan.releaseState == GhostReleaseState::CyanHouseBounce &&
                 !initialCyan.collisionActive && initialCyan.releaseStepsRemaining == 2,
                 "Cyan starts inside the house with a bounded release state");

    PacManState pacman = game.pacman;
    pacman.x = 128;
    pacman.y = 128;
    pacman.speed = 0;
    pacman.offset = 1;
    pacman.requestedDirection = Direction::None;
    const Direction directions[] = {
        Direction::Right, Direction::Left, Direction::Up, Direction::Down
    };
    const int expectedX[] = {256, 0, 128, 128};
    const int expectedY[] = {256, 0, 128, 128};
    for (int index = 0; index < 4; ++index) {
        pacman.direction = directions[index];
        pacman.facingDirection = directions[index];
        const GhostTarget target = calculate_cyan_target(game, pacman, game.ghosts[0]);
        ok &= expect(target.x == expectedX[index] && target.y == expectedY[index],
                     "Cyan projects eight tiles with the historical direction formula");
    }

    pacman.x = 192;
    pacman.y = 224;
    pacman.direction = Direction::Right;
    pacman.facingDirection = Direction::Right;
    const GhostTarget nearTarget = calculate_cyan_target(game, pacman, game.ghosts[0]);
    ok &= expect(nearTarget.x == pacman.x && nearTarget.y == pacman.y,
                 "Cyan does not project when historical tile separation is at most three");

    pacman.x = 128;
    pacman.y = 128;
    pacman.direction = Direction::Right;
    pacman.facingDirection = Direction::Right;
    const GhostTarget rightTarget = calculate_cyan_target(game, pacman, game.ghosts[0]);
    pacman.direction = Direction::Up;
    pacman.facingDirection = Direction::Up;
    const GhostTarget upTarget = calculate_cyan_target(game, pacman, game.ghosts[0]);
    ok &= expect(rightTarget.y == pacman.y + 128 && upTarget.y == pacman.y,
                 "Cyan preserves the historical XD-for-Y projection quirk");
    pacman.x = 16;
    pacman.y = 16;
    pacman.direction = Direction::Left;
    pacman.facingDirection = Direction::Left;
    const GhostTarget leftTarget = calculate_cyan_target(game, pacman, game.ghosts[0]);
    ok &= expect(leftTarget.x == -112 && leftTarget.y == -112,
                 "Cyan keeps projected off-maze coordinates instead of clamping early");

    GhostState red = game.ghosts[0];
    red.x = 0;
    red.y = 0;
    const GhostTarget redUpperLeft = calculate_cyan_target(game, pacman, red);
    red.x = 448;
    red.y = 0;
    const GhostTarget redUpperRight = calculate_cyan_target(game, pacman, red);
    red.x = 0;
    red.y = 448;
    const GhostTarget redLowerLeft = calculate_cyan_target(game, pacman, red);
    red.x = 448;
    red.y = 448;
    const GhostTarget redLowerRight = calculate_cyan_target(game, pacman, red);
    ok &= expect(redUpperLeft.x == redUpperRight.x && redUpperLeft.y == redUpperRight.y &&
                 redUpperLeft.x == redLowerLeft.x && redUpperLeft.y == redLowerLeft.y &&
                 redUpperLeft.x == redLowerRight.x && redUpperLeft.y == redLowerRight.y,
                 "Cyan target is independent of Red because the VB6 Ghost(3) branch has no Red term");

    disable_non_cyan_ghosts(&game);
    game.pacman.x = 64;
    game.pacman.y = 232;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game.suppressGhostCollisionsForValidation = true;
    bool sawHouse = false;
    bool sawExit = false;
    bool sawNormal = false;
    bool sawReleaseStart = false;
    bool sawReleaseComplete = false;
    for (uint32_t step = 0; step < 260; ++step) {
        game_update(&game);
        sawReleaseStart = sawReleaseStart || game.cyanReleaseStarted;
        sawReleaseComplete = sawReleaseComplete || game.cyanReleaseCompleted;
        const GhostState& cyan = game.ghosts[2];
        if (cyan.releaseState == GhostReleaseState::CyanHouseBounce) {
            sawHouse = true;
            ok &= expect(cyan.x == 224 && cyan.y >= 224 && cyan.y <= 240,
                         "Cyan house bounce stays on the historical middle vertical lane");
            ok &= expect(!cyan.collisionActive, "Cyan remains collision-inactive inside the house");
        } else if (cyan.releaseState == GhostReleaseState::CyanExiting) {
            sawExit = true;
            ok &= expect(cyan.x == 224 && cyan.y >= 184 && cyan.y <= 224,
                         "Cyan exits upward through the historical doorway lane");
            ok &= expect(!cyan.collisionActive, "Cyan remains collision-inactive during house exit");
        } else if (cyan.releaseState == GhostReleaseState::Normal) {
            sawNormal = true;
            break;
        }
    }
    ok &= expect(sawHouse && sawExit && sawNormal && sawReleaseStart && sawReleaseComplete,
                 "Cyan release begins once, exits, and activates normal targeting");
    ok &= expect(game.ghosts[2].x == 224 && game.ghosts[2].y == 184 &&
                 game.ghosts[2].collisionActive && game.ghosts[2].offset == 8,
                 "Cyan becomes collision-active at the historical outside-house boundary");

    const int beforeX = game.ghosts[2].x;
    for (uint32_t step = 0; step < 20; ++step) game_update(&game);
    ok &= expect(game.ghosts[2].x != beforeX || game.ghosts[2].y != 184,
                 "Cyan continues through a normal legal corridor after release");
    return ok;
}

bool test_cyan_direction_selection_and_tunnels() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    GhostState& cyan = game.ghosts[2];
    cyan.releaseState = GhostReleaseState::Normal;
    cyan.collisionActive = true;
    cyan.x = 208;
    cyan.y = 184;
    cyan.direction = Direction::Left;
    cyan.requestedDirection = Direction::Left;
    cyan.offset = 0;
    ok &= expect(choose_cyan_direction(game, cyan, 208, 24) == Direction::Up,
                 "Cyan uses the historical sign-priority turn at an intersection");
    ok &= expect(choose_cyan_direction(game, cyan, 208, 24) ==
                 choose_cyan_direction(game, cyan, 208, 24),
                 "Cyan direction selection is deterministic");

    bool foundHorizontalPriority = false;
    for (int row = 0; row < kPacManMazeRows && !foundHorizontalPriority; ++row) {
        for (int column = 0; column < kPacManMazeColumns && !foundHorizontalPriority; ++column) {
            const int x = column * kPacManTileSize + 8;
            const int y = row * kPacManTileSize + 8;
            if (!can_move(game, x, y, Direction::Up) ||
                !can_move(game, x, y, Direction::Right)) continue;
            cyan.x = x;
            cyan.y = y;
            cyan.direction = Direction::Up;
            cyan.requestedDirection = Direction::Up;
            cyan.offset = 0;
            if (choose_cyan_direction(game, cyan, x + 128, y - 128) == Direction::Right) {
                foundHorizontalPriority = true;
            }
        }
    }
    ok &= expect(foundHorizontalPriority,
                 "Cyan preserves historical horizontal-over-vertical sign priority");

    GameState deadEndGame = game;
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            deadEndGame.level.cells[row][column] = CellType::Wall;
        }
    }
    deadEndGame.level.cells[5][5] = CellType::Empty;
    deadEndGame.level.cells[4][5] = CellType::Empty;
    GhostState deadEndCyan = deadEndGame.ghosts[2];
    deadEndCyan.x = 5 * kPacManTileSize + 8;
    deadEndCyan.y = 5 * kPacManTileSize + 8;
    deadEndCyan.direction = Direction::Down;
    deadEndCyan.requestedDirection = Direction::Down;
    deadEndCyan.offset = 0;
    ok &= expect(choose_cyan_direction(deadEndGame, deadEndCyan, 224, 376) == Direction::Up,
                 "Cyan reverses only when a dead end leaves no alternative");

    disable_non_cyan_ghosts(&game);
    game.suppressGhostCollisionsForValidation = true;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    enable_normal_cyan(&game);
    cyan = game.ghosts[2];
    cyan.x = 16;
    cyan.y = 232;
    cyan.direction = Direction::Left;
    cyan.requestedDirection = Direction::Left;
    cyan.offset = 1;
    game_update(&game);
    ok &= expect(cyan.x == 431 && cyan.y == 232 && game.cyanTunnelWrapped,
                 "Cyan wraps left-to-right through the horizontal tunnel");

    cyan.x = 424;
    cyan.y = 232;
    cyan.direction = Direction::Right;
    cyan.requestedDirection = Direction::Right;
    cyan.offset = 1;
    game_update(&game);
    ok &= expect(cyan.x == 9 && cyan.y == 232 && game.cyanTunnelWrapped,
                 "Cyan wraps right-to-left through the horizontal tunnel");

    cyan.x = 16;
    cyan.y = 216;
    cyan.direction = Direction::Left;
    cyan.requestedDirection = Direction::Left;
    cyan.offset = 0;
    ok &= expect(!can_move(game, cyan.x, cyan.y, Direction::Left),
                 "Cyan cannot select a tunnel move off the real tunnel row");
    game_update(&game);
    ok &= expect(!game.cyanTunnelWrapped && cyan.x != 431,
                 "Cyan does not wrap outside the tunnel row");
    return ok;
}

bool test_orange_target_and_release_state() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    const GhostState& initialOrange = game.ghosts[3];
    ok &= expect(initialOrange.x == 256 && initialOrange.y == 224,
                 "Orange starts at the historical right-house position");
    ok &= expect(initialOrange.direction == Direction::Up && initialOrange.speed == 1,
                 "Orange starts facing Up at the historical speed");
    ok &= expect(initialOrange.active &&
                 initialOrange.releaseState == GhostReleaseState::OrangeHouseBounce &&
                 !initialOrange.collisionActive,
                 "Orange starts inside the house with collision inactive");

    GhostState orange = initialOrange;
    orange.x = 160;
    orange.y = 160;
    PacManState pacman = game.pacman;
    pacman.x = 208;
    pacman.y = 160;
    pacman.direction = Direction::Right;
    pacman.facingDirection = Direction::Right;
    ok &= expect(orange_distance_tiles(pacman, orange) == 3 &&
                 !orange_uses_far_target(pacman, orange) &&
                 calculate_orange_target(game, pacman, orange).x == pacman.x &&
                 calculate_orange_target(game, pacman, orange).y == pacman.y,
                 "Orange uses its near target exactly below the strict threshold");

    pacman.x = 224;
    ok &= expect(orange_distance_tiles(pacman, orange) == 4 &&
                 !orange_uses_far_target(pacman, orange) &&
                 calculate_orange_target(game, pacman, orange).x == pacman.x &&
                 calculate_orange_target(game, pacman, orange).y == pacman.y,
                 "Orange remains near at exactly four Manhattan tiles");

    pacman.x = 240;
    ok &= expect(orange_distance_tiles(pacman, orange) == 5 &&
                 orange_uses_far_target(pacman, orange) &&
                 calculate_orange_target(game, pacman, orange).x == pacman.x + 192 &&
                 calculate_orange_target(game, pacman, orange).y == pacman.y + 192,
                 "Orange switches to twelve-tile far targeting exactly above four tiles");

    pacman.x = 160;
    pacman.y = 240;
    ok &= expect(orange_distance_tiles(pacman, orange) == 5 && orange_uses_far_target(pacman, orange),
                 "Orange threshold uses vertical Manhattan separation");
    pacman.x = 208;
    pacman.y = 192;
    ok &= expect(orange_distance_tiles(pacman, orange) == 5 && orange_uses_far_target(pacman, orange),
                 "Orange threshold uses diagonal Manhattan separation");

    pacman.x = 16;
    pacman.y = 232;
    orange.x = 416;
    orange.y = 232;
    pacman.direction = Direction::Left;
    const GhostTarget tunnelTarget = calculate_orange_target(game, pacman, orange);
    ok &= expect(orange_distance_tiles(pacman, orange) == 25 &&
                 tunnelTarget.x == -176 && tunnelTarget.y == 40,
                 "Orange uses raw historical tile distance near the tunnel and keeps off-maze targets");

    orange.x = 160;
    orange.y = 160;
    pacman.x = 240;
    pacman.y = 160;
    const Direction directions[] = {
        Direction::Right, Direction::Left, Direction::Up, Direction::Down
    };
    const int expectedX[] = {432, 48, 240, 240};
    const int expectedY[] = {352, -32, 160, 160};
    for (int index = 0; index < 4; ++index) {
        pacman.direction = directions[index];
        pacman.facingDirection = directions[index];
        const GhostTarget target = calculate_orange_target(game, pacman, orange);
        ok &= expect(target.x == expectedX[index] && target.y == expectedY[index],
                     "Orange preserves the historical XD projection for every Pac-Man direction");
    }
    const GhostState unchangedOrange = orange;
    const GhostTarget unchangedTarget = calculate_orange_target(game, pacman, orange);
    ok &= expect(orange.x == unchangedOrange.x && orange.y == unchangedOrange.y &&
                 orange.direction == unchangedOrange.direction &&
                 unchangedTarget.x == 240 && unchangedTarget.y == 160,
                 "Orange target calculation is deterministic and does not mutate state");
    return ok;
}

bool test_orange_release_and_movement() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    disable_non_orange_ghosts(&game);
    game.ghosts[1].releaseState = GhostReleaseState::PinkHouseBounce;
    game.pacman.x = 64;
    game.pacman.y = 232;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game.suppressGhostCollisionsForValidation = true;

    game_update(&game);
    ok &= expect(game.ghosts[3].x == 256 && game.ghosts[3].y == 225 &&
                 game.ghosts[3].direction == Direction::Down &&
                 game.ghosts[3].releaseState == GhostReleaseState::OrangeHouseBounce &&
                 !game.ghosts[3].collisionActive,
                 "Orange begins with the historical vertical bounce and remains inactive");
    for (uint32_t step = 0; step < 30; ++step) game_update(&game);
    ok &= expect(game.ghosts[3].x == 256 && game.ghosts[3].y >= 224 && game.ghosts[3].y <= 240 &&
                 game.ghosts[3].releaseState == GhostReleaseState::OrangeHouseBounce,
                 "Orange remains in the house until the Pink release condition is met");

    game.ghosts[1].releaseState = GhostReleaseState::Normal;
    game.ghosts[1].active = false;
    bool sawHouse = false;
    bool sawCenter = false;
    bool sawExit = false;
    bool sawNormal = false;
    uint32_t startedCount = 0;
    uint32_t completedCount = 0;
    for (uint32_t step = 0; step < 240; ++step) {
        game_update(&game);
        startedCount += game.orangeReleaseStarted ? 1u : 0u;
        completedCount += game.orangeReleaseCompleted ? 1u : 0u;
        const GhostState& orange = game.ghosts[3];
        if (orange.releaseState == GhostReleaseState::OrangeHouseBounce) {
            sawHouse = true;
            ok &= expect(orange.x == 256 && orange.y >= 224 && orange.y <= 240 &&
                         (orange.direction == Direction::Up || orange.direction == Direction::Down),
                         "Orange house bounce stays on its historical right vertical lane");
        } else if (orange.releaseState == GhostReleaseState::OrangeToCenter) {
            sawCenter = true;
            ok &= expect(orange.y == 224 && orange.x >= 224 && orange.x <= 256,
                         "Orange moves left through the house center lane");
        } else if (orange.releaseState == GhostReleaseState::OrangeExiting) {
            sawExit = true;
            ok &= expect(orange.x == 224 && orange.y >= 184 && orange.y <= 224,
                         "Orange exits upward through the fixed x=224 doorway");
            ok &= expect(!orange.collisionActive, "Orange remains collision-inactive during house exit");
        } else if (orange.releaseState == GhostReleaseState::Normal) {
            sawNormal = true;
            break;
        }
    }
    ok &= expect(sawHouse && sawCenter && sawExit && sawNormal && startedCount == 1 && completedCount == 1,
                 "Orange release starts and completes exactly once");
    ok &= expect(game.ghosts[3].x == 224 && game.ghosts[3].y == 184 &&
                 game.ghosts[3].offset == 8 && game.ghosts[3].collisionActive &&
                 game.ghosts[3].direction == Direction::Left,
                 "Orange becomes collision-active at the outside-house boundary");

    const int beforeX = game.ghosts[3].x;
    const int beforeY = game.ghosts[3].y;
    const uint8_t beforeAnimation = game.ghosts[3].animationFrame;
    game_update(&game);
    ok &= expect(beforeX - game.ghosts[3].x + beforeY - game.ghosts[3].y == 1 &&
                 game.ghosts[3].animationFrame != beforeAnimation,
                 "Orange continues normal movement at one logical pixel per fixed step");
    for (uint32_t step = 0; step < 120; ++step) {
        game_update(&game);
        const int column = level_column_from_position(game.ghosts[3].x);
        const int row = level_row_from_position(game.ghosts[3].y);
        ok &= expect(is_walkable_cell(level_cell(game.level, column, row)),
                     "Moving Orange remains inside legal maze paths");
    }
    return ok;
}

bool test_orange_direction_selection_and_tunnels() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    GhostState& orange = game.ghosts[3];
    orange.x = 208;
    orange.y = 184;
    orange.direction = Direction::Left;
    orange.requestedDirection = Direction::Left;
    orange.offset = 0;
    ok &= expect(choose_orange_direction(game, orange, 208, 24) == Direction::Up,
                 "Orange reuses the historical sign-priority turn at an intersection");
    ok &= expect(choose_orange_direction(game, orange, 208, 24) ==
                 choose_orange_direction(game, orange, 208, 24),
                 "Orange direction selection is deterministic");

    bool foundHorizontalPriority = false;
    for (int row = 0; row < kPacManMazeRows && !foundHorizontalPriority; ++row) {
        for (int column = 0; column < kPacManMazeColumns && !foundHorizontalPriority; ++column) {
            const int x = column * kPacManTileSize + 8;
            const int y = row * kPacManTileSize + 8;
            if (!can_move(game, x, y, Direction::Up) ||
                !can_move(game, x, y, Direction::Right)) continue;
            orange.x = x;
            orange.y = y;
            orange.direction = Direction::Up;
            orange.requestedDirection = Direction::Up;
            orange.offset = 0;
            if (choose_orange_direction(game, orange, x + 192, y - 192) == Direction::Right) {
                foundHorizontalPriority = true;
            }
        }
    }
    ok &= expect(foundHorizontalPriority,
                 "Orange preserves historical horizontal-over-vertical sign priority");

    GameState deadEndGame = game;
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            deadEndGame.level.cells[row][column] = CellType::Wall;
        }
    }
    deadEndGame.level.cells[5][5] = CellType::Empty;
    deadEndGame.level.cells[4][5] = CellType::Empty;
    GhostState deadEndOrange = deadEndGame.ghosts[3];
    deadEndOrange.x = 5 * kPacManTileSize + 8;
    deadEndOrange.y = 5 * kPacManTileSize + 8;
    deadEndOrange.direction = Direction::Down;
    deadEndOrange.requestedDirection = Direction::Down;
    deadEndOrange.offset = 0;
    ok &= expect(choose_orange_direction(deadEndGame, deadEndOrange, 224, 376) == Direction::Up,
                 "Orange reverses only when a dead end leaves no alternative");

    game_initialize(&game);
    disable_non_orange_ghosts(&game);
    enable_normal_orange(&game);
    game.suppressGhostCollisionsForValidation = true;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    orange = game.ghosts[3];
    orange.x = 16;
    orange.y = 232;
    orange.direction = Direction::Left;
    orange.requestedDirection = Direction::Left;
    orange.offset = 1;
    game_update(&game);
    ok &= expect(orange.x == 431 && orange.y == 232 && game.orangeTunnelWrapped,
                 "Orange wraps left-to-right through the horizontal tunnel");

    orange.x = 424;
    orange.y = 232;
    orange.direction = Direction::Right;
    orange.requestedDirection = Direction::Right;
    orange.offset = 1;
    game_update(&game);
    ok &= expect(orange.x == 9 && orange.y == 232 && game.orangeTunnelWrapped,
                 "Orange wraps right-to-left through the horizontal tunnel");

    orange.x = 16;
    orange.y = 216;
    orange.direction = Direction::Left;
    orange.requestedDirection = Direction::Left;
    orange.offset = 0;
    game_update(&game);
    ok &= expect(!game.orangeTunnelWrapped && orange.x != 431 && orange.x != 9,
                 "Orange never wraps outside the historical tunnel row");
    return ok;
}

bool test_orange_collisions_and_state_gates() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    disable_non_orange_ghosts(&game);
    enable_normal_orange(&game);
    GhostState& orange = game.ghosts[3];
    orange.x = 208;
    orange.y = 184;
    orange.direction = Direction::Right;
    orange.requestedDirection = Direction::Right;
    orange.offset = 1;
    orange.speed = 1;
    game.pacman.x = 223;
    game.pacman.y = 184;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.collisionDetected &&
                 game.lives == kPacManInitialLives - 1 && orange.x != 208,
                 "Moving Orange collision enters Dying and deducts one life");

    GameState overlapGame{};
    game_initialize(&overlapGame);
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        overlapGame.ghosts[index].x = 180;
        overlapGame.ghosts[index].y = 200;
        overlapGame.ghosts[index].active = true;
        overlapGame.ghosts[index].collisionActive = true;
        overlapGame.ghosts[index].speed = 0;
        overlapGame.ghosts[index].releaseState = GhostReleaseState::Normal;
    }
    overlapGame.pacman.x = 180;
    overlapGame.pacman.y = 200;
    overlapGame.pacman.direction = Direction::None;
    overlapGame.pacman.facingDirection = Direction::None;
    overlapGame.pacman.requestedDirection = Direction::None;
    overlapGame.pacman.offset = 1;
    overlapGame.pacman.speed = 0;
    game_update(&overlapGame);
    ok &= expect(overlapGame.playState == PlayState::Dying && overlapGame.lives == kPacManInitialLives - 1 &&
                 overlapGame.deathTransitions == 1,
                 "Simultaneous four-ghost overlap deducts one life only");

    const PlayState states[] = {
        PlayState::Dying, PlayState::ReadyAfterDeath, PlayState::LevelComplete, PlayState::GameOver
    };
    for (uint32_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        game_initialize(&game);
        disable_non_orange_ghosts(&game);
        enable_normal_orange(&game);
        game.ghosts[3].x = 208;
        game.ghosts[3].y = 184;
        game.ghosts[3].direction = Direction::Right;
        game.ghosts[3].requestedDirection = Direction::Right;
        game.ghosts[3].offset = 1;
        game.ghosts[3].speed = 1;
        game.playState = states[index];
        game.deathStepsRemaining = 100;
        game.readyStepsRemaining = 100;
        game.levelCompleteStepsRemaining = 100;
        const int oldX = game.ghosts[3].x;
        const int oldY = game.ghosts[3].y;
        game_update(&game);
        ok &= expect(game.ghosts[3].x == oldX && game.ghosts[3].y == oldY,
                     "Orange stops during every non-Playing state");
    }
    return ok;
}

bool test_orange_reset_lifecycle_and_four_ghost_update() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    place_stationary_collision(&game, GhostKind::Orange);
    game_update(&game);
    for (uint32_t step = 0; step < kPacManDeathDurationSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath &&
                 game.ghosts[3].x == 256 && game.ghosts[3].y == 224 &&
                 game.ghosts[3].direction == Direction::Up &&
                 game.ghosts[3].releaseState == GhostReleaseState::OrangeHouseBounce &&
                 !game.ghosts[3].collisionActive && game.ghosts[3].speed == 1,
                 "Orange resets to its house release state after death");

    game_reset_level(&game);
    ok &= expect(game.levelNumber == 2 && game.ghosts[3].x == 256 && game.ghosts[3].y == 224 &&
                 game.ghosts[3].releaseState == GhostReleaseState::OrangeHouseBounce,
                 "Orange resets after level completion");
    game.playState = PlayState::GameOver;
    ok &= expect(game_restart_session(&game) && game.ghosts[3].x == 256 && game.ghosts[3].y == 224 &&
                 game.ghosts[3].releaseState == GhostReleaseState::OrangeHouseBounce &&
                 game.ghosts[3].speed == 1,
                 "Orange resets after Game Over restart");

    game_initialize(&game);
    game.suppressGhostCollisionsForValidation = true;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    const int initialX[] = {208, 224, 240, 256};
    const Direction initialDirection[] = {
        Direction::Right, Direction::Left, Direction::Right, Direction::Left
    };
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        GhostState& ghost = game.ghosts[index];
        ghost.x = initialX[index];
        ghost.y = 184;
        ghost.direction = initialDirection[index];
        ghost.requestedDirection = initialDirection[index];
        ghost.offset = 1;
        ghost.speed = 1;
        ghost.active = true;
        ghost.collisionActive = true;
        ghost.releaseState = GhostReleaseState::Normal;
    }
    game_update(&game);
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        ok &= expect(game.ghosts[index].x != initialX[index] && game.ghosts[index].animationFrame == 1,
                     "All four ghosts move once and animate in the deterministic update order");
    }
    return ok;
}

bool test_pink_collisions_and_state_gates() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    disable_non_pink_ghosts(&game);
    enable_normal_pink(&game);
    GhostState& pink = game.ghosts[1];
    pink.x = 208;
    pink.y = 184;
    pink.direction = Direction::Right;
    pink.requestedDirection = Direction::Right;
    pink.offset = 1;
    pink.speed = 1;
    game.pacman.x = 208;
    game.pacman.y = 169;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    const int beforeX = pink.x;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.collisionDetected &&
                 pink.x != beforeX,
                 "Moving Pink collides at an intersection after both actors update");

    game_initialize(&game);
    game.ghosts[0].active = true;
    game.ghosts[0].collisionActive = true;
    game.ghosts[0].speed = 0;
    enable_normal_pink(&game);
    game.ghosts[1].speed = 0;
    game.ghosts[0].x = 180;
    game.ghosts[0].y = 200;
    game.ghosts[1].x = 180;
    game.ghosts[1].y = 200;
    game.pacman.x = 180;
    game.pacman.y = 200;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.lives == kPacManInitialLives - 1 &&
                 game.deathTransitions == 1,
                 "Simultaneous Red and Pink overlap deducts one life only");

    const PlayState states[] = {
        PlayState::Dying, PlayState::ReadyAfterDeath, PlayState::LevelComplete, PlayState::GameOver
    };
    for (uint32_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        game_initialize(&game);
        enable_normal_pink(&game);
        game.ghosts[1].x = 208;
        game.ghosts[1].y = 184;
        game.ghosts[1].direction = Direction::Left;
        game.ghosts[1].requestedDirection = Direction::Left;
        game.ghosts[1].offset = 1;
        game.ghosts[1].speed = 1;
        game.playState = states[index];
        game.deathStepsRemaining = 100;
        game.readyStepsRemaining = 100;
        game.levelCompleteStepsRemaining = 100;
        const int oldX = game.ghosts[1].x;
        const int oldY = game.ghosts[1].y;
        game_update(&game);
        ok &= expect(game.ghosts[1].x == oldX && game.ghosts[1].y == oldY,
                     "Pink stops during every non-Playing state");
    }
    return ok;
}

bool test_pink_reset_lifecycle_and_stationary_companions() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    disable_non_pink_ghosts(&game);
    enable_normal_pink(&game);
    game.ghosts[1].x = game.pacman.x;
    game.ghosts[1].y = game.pacman.y;
    game.ghosts[1].speed = 0;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    for (uint32_t step = 0; step < kPacManDeathDurationSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath &&
                 game.ghosts[1].x == 192 && game.ghosts[1].y == 224 &&
                 game.ghosts[1].direction == Direction::Up &&
                 game.ghosts[1].releaseState == GhostReleaseState::PinkHouseBounce &&
                 !game.ghosts[1].collisionActive,
                 "Pink resets to the historical house release state after death");

    game_reset_level(&game);
    ok &= expect(game.levelNumber == 2 && game.ghosts[1].releaseState == GhostReleaseState::PinkHouseBounce &&
                 game.ghosts[1].x == 192 && game.ghosts[1].y == 224,
                 "Pink release state resets after level completion");

    game.playState = PlayState::GameOver;
    ok &= expect(game_restart_session(&game) &&
                 game.ghosts[1].releaseState == GhostReleaseState::PinkHouseBounce &&
                 game.ghosts[1].x == 192 && game.ghosts[1].y == 224,
                 "Pink release state resets after Game Over restart");

    game_initialize(&game);
    const int cyanX = game.ghosts[2].x;
    const int cyanY = game.ghosts[2].y;
    const int orangeX = game.ghosts[3].x;
    const int orangeY = game.ghosts[3].y;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game.suppressGhostCollisionsForValidation = true;
    for (uint32_t step = 0; step < 120; ++step) game_update(&game);
    ok &= expect(game.ghosts[2].x != cyanX || game.ghosts[2].y != cyanY ||
                 game.ghosts[2].releaseState != GhostReleaseState::CyanHouseBounce,
                 "Cyan progresses through its house release while Pink remains independent");
    ok &= expect(game.ghosts[3].active && game.ghosts[3].speed == 1 &&
                 (game.ghosts[3].x != orangeX || game.ghosts[3].y != orangeY ||
                  game.ghosts[3].releaseState != GhostReleaseState::OrangeHouseBounce),
                 "Orange follows its own release state independently");
    return ok;
}

bool test_counts_and_layout() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    ok &= expect(game.level.normalPillsRemaining == 240, "initial normal-pill count");
    ok &= expect(game.level.powerPillsRemaining == 4, "initial power-pill count");
    ok &= expect(game.level.totalConsumablesRemaining == 244, "initial total consumable count");
    for (int y = 0; y < kPacManMazeRows; ++y) {
        for (int x = 0; x < kPacManMazeColumns; ++x) {
            const CellType cell = game.level.cells[y][x];
            if (level_tile(x, y) == 'W' &&
                !expect(!is_consumable_cell(cell), "no consumables inside walls")) ok = false;
        }
    }
    ok &= expect(level_cell(game.level, -1, 0) == CellType::Wall, "bounds-safe negative cell access");
    ok &= expect(level_cell(game.level, kPacManMazeColumns, 0) == CellType::Wall,
                 "bounds-safe right cell access");
    ok &= expect(game.level.cells[14][0] == CellType::Tunnel &&
                 game.level.cells[14][11] == CellType::GhostHouse,
                 "historical tunnel and ghost-house classification");
    return ok;
}

bool test_normal_pill_all_directions() {
    bool ok = true;
    const Direction directions[] = {Direction::Right, Direction::Left, Direction::Up, Direction::Down};
    const int targetColumns[] = {2, 1, 1, 1};
    const int targetRows[] = {1, 1, 1, 2};
    for (int i = 0; i < 4; ++i) {
        GameState game{};
        game_initialize(&game);
        const uint32_t before = game.level.totalConsumablesRemaining;
        set_before_target(&game, targetColumns[i], targetRows[i], directions[i]);
        game_update(&game);
        ok &= expect(game.normalPillConsumed, "normal pill consumed in requested direction");
        ok &= expect(game.score == kPacManNormalPillScore, "normal pill awards historical score");
        ok &= expect(game.level.totalConsumablesRemaining == before - 1,
                     "normal pill decrements total exactly once");
        ok &= expect(game.level.cells[targetRows[i]][targetColumns[i]] == CellType::Empty,
                     "normal pill cell becomes empty");
    }
    return ok;
}

bool test_power_pill_and_duplicate_prevention() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    set_before_target(&game, 1, 3, Direction::Up);
    game.pacman.speed = 0;
    const uint32_t before = game.level.totalConsumablesRemaining;
    game_update(&game);
    ok &= expect(game.powerPillConsumed, "power pill consumed");
    ok &= expect(game.score == kPacManPowerPillScore, "power pill awards historical score");
    ok &= expect(game.level.totalConsumablesRemaining == before - 1, "power pill decrements total once");
    game_update(&game);
    ok &= expect(game.score == kPacManPowerPillScore && game.level.totalConsumablesRemaining == before - 1,
                 "repeated update in consumed tile does not rescore");
    return ok;
}

bool test_power_pill_timers_and_reversal() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    const uint32_t duration = game_frightened_duration_steps(game);
    const uint32_t threshold = game_frightened_flash_threshold(game);
    set_before_target(&game, 1, 3, Direction::Up);
    game.pacman.speed = 0;
    game.ghosts[0].offset = 1;
    game.ghosts[0].speed = 0;
    const Direction redDirectionBefore = game.ghosts[0].direction;
    game_update(&game);
    ok &= expect(game.powerPillConsumed && game.score == kPacManPowerPillScore,
                 "power pill keeps its historical base score");
    ok &= expect(duration == 900 && threshold == 200,
                 "level-one frightened duration and flash threshold use 10 ms steps");
    ok &= expect(game.ghosts[0].condition == GhostCondition::Frightened &&
                 game.ghosts[0].powerPillStepsRemaining == duration - 1u,
                 "Red receives an individual timer and decrements after its AI pass");
    ok &= expect(game.ghosts[0].direction == opposite_direction(redDirectionBefore) &&
                 game.ghostReversalRequested[0] && game.ghostReversalApplied[0],
                 "eligible Red reverses once on power-pill activation");
    ok &= expect(game.ghosts[1].powerPillStepsRemaining == 0 &&
                 game.ghosts[2].powerPillStepsRemaining == 0 &&
                 game.ghosts[3].powerPillStepsRemaining == 0,
                 "house-contained ghosts do not receive frightened timers");

    enable_all_normal_ghosts_for_power(&game);
    set_before_target(&game, 1, 23, Direction::Down);
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.score == kPacManPowerPillScore * 2u &&
                 game.ghostEatChain == 0,
                 "a second power pill resets the score chain");
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        ok &= expect(game.ghosts[index].condition == GhostCondition::Frightened &&
                     game.ghosts[index].powerPillStepsRemaining == duration - 1u &&
                     game.ghosts[index].direction == opposite_direction(Direction::Right) &&
                     game.ghostReversalApplied[index],
                     "repeated power pill resets each eligible timer and reverses each ghost again");
    }

    game.suppressGhostCollisionsForValidation = true;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        game.ghosts[index].condition = GhostCondition::Normal;
        game.ghosts[index].powerPillStepsRemaining = 0;
        game.ghosts[index].speed = 0;
    }
    GhostState& red = game.ghosts[0];
    GhostState& pink = game.ghosts[1];
    red.condition = GhostCondition::Frightened;
    red.powerPillStepsRemaining = 3;
    pink.condition = GhostCondition::Frightened;
    pink.powerPillStepsRemaining = 1;
    game.ghostEatChain = 3;
    game_update(&game);
    ok &= expect(red.powerPillStepsRemaining == 2 && red.condition == GhostCondition::Frightened &&
                 pink.condition == GhostCondition::Normal && game.ghostEatChain == 3u,
                 "timer decrements once per fixed update and combo persists while one ghost remains frightened");
    game_update(&game);
    ok &= expect(red.powerPillStepsRemaining == 1,
                 "frightened timer reaches one without underflow");
    game_update(&game);
    ok &= expect(red.powerPillStepsRemaining == 0 && red.condition == GhostCondition::Normal &&
                 game.ghostEatChain == 0u,
                 "last frightened timer expiration restores normal state and resets the combo");
    game_update(&game);
    ok &= expect(red.powerPillStepsRemaining == 0 && red.condition == GhostCondition::Normal,
                 "expired frightened timer remains bounded at zero");

    red.condition = GhostCondition::Frightened;
    red.powerPillStepsRemaining = threshold + 1u;
    game_update(&game);
    ok &= expect(red.powerPillStepsRemaining == threshold && !game.frightenedFlashingBegan[0],
                 "flashing warning stays off at the exact threshold boundary");
    game_update(&game);
    ok &= expect(red.powerPillStepsRemaining == threshold - 1u && game.frightenedFlashingBegan[0],
                 "flashing warning begins deterministically below the remaining-time threshold");
    return ok;
}

bool test_frightened_speed_and_collision_order() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    enable_all_normal_ghosts_for_power(&game);
    set_before_target(&game, 1, 3, Direction::Up);
    game.pacman.speed = 0;
    const int redX = game.pacman.x;
    const int redY = game.pacman.y;
    game.ghosts[0].x = redX;
    game.ghosts[0].y = redY;
    game.ghosts[1].x = redX;
    game.ghosts[1].y = redY;
    game.ghosts[2].x = redX;
    game.ghosts[2].y = redY;
    game.ghosts[3].x = redX;
    game.ghosts[3].y = redY;
    game_update(&game);
    ok &= expect(game.ghostEatChain == 4 && game.score == 10u + 200u + 400u + 800u + 1600u,
                 "four overlapping frightened ghosts use deterministic 200/400/800/1600 scoring");
    ok &= expect(game.ghostEatScore[0] == 200u && game.ghostEatScore[1] == 400u &&
                 game.ghostEatScore[2] == 800u && game.ghostEatScore[3] == 1600u,
                 "each ghost receives its exact successive combo score");
    ok &= expect(game.ghosts[0].condition == GhostCondition::Eaten &&
                 !game.ghosts[0].collisionActive && game.ghostEaten[0],
                 "eaten ghost becomes non-lethal exactly once");
    const uint32_t scoreAfterEat = game.score;
    game_update(&game);
    ok &= expect(game.score == scoreAfterEat,
                 "returning ghosts cannot be eaten or scored repeatedly");

    game_initialize(&game);
    disable_non_red_ghosts(&game);
    GhostState& red = game.ghosts[0];
    red.x = 208;
    red.y = 184;
    red.offset = 1;
    red.speed = 0;
    red.condition = GhostCondition::Frightened;
    red.powerPillStepsRemaining = 10;
    red.collisionActive = true;
    game.pacman.x = red.x;
    game.pacman.y = red.y;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Playing && game.score == 200u &&
                 red.condition == GhostCondition::Eaten,
                 "frightened collision eats rather than kills Pac-Man");

    game_initialize(&game);
    disable_non_red_ghosts(&game);
    red = game.ghosts[0];
    red.x = 180;
    red.y = 200;
    red.offset = 1;
    red.speed = 0;
    red.condition = GhostCondition::Normal;
    red.collisionActive = true;
    game.pacman.x = red.x;
    game.pacman.y = red.y;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.lives == kPacManInitialLives - 1,
                 "normal collision still kills Pac-Man");

    game_initialize(&game);
    disable_non_red_ghosts(&game);
    red = game.ghosts[0];
    red.x = 180;
    red.y = 200;
    red.offset = 1;
    red.speed = 0;
    red.condition = GhostCondition::Normal;
    red.collisionActive = true;
    GhostState& pink = game.ghosts[1];
    enable_normal_pink(&game);
    pink.x = red.x;
    pink.y = red.y;
    pink.offset = 1;
    pink.speed = 0;
    pink.condition = GhostCondition::Frightened;
    pink.powerPillStepsRemaining = 10;
    game.pacman.x = red.x;
    game.pacman.y = red.y;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.score == 0,
                 "lower-index normal ghost wins mixed overlap without later score");

    game_initialize(&game);
    disable_non_red_ghosts(&game);
    red = game.ghosts[0];
    red.x = 180;
    red.y = 200;
    red.offset = 1;
    red.speed = 0;
    red.condition = GhostCondition::Frightened;
    red.powerPillStepsRemaining = 10;
    red.collisionActive = true;
    pink = game.ghosts[1];
    enable_normal_pink(&game);
    pink.x = red.x;
    pink.y = red.y;
    pink.offset = 1;
    pink.speed = 0;
    game.pacman.x = red.x;
    game.pacman.y = red.y;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.score == 200u &&
                 game.ghostEatChain == 0u && red.condition == GhostCondition::Normal,
                 "ghost-index order awards the eaten ghost once, then death clears the chain and ghost state");
    return ok;
}

bool test_eaten_return_and_lifecycle_reset() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    game.ghostEatChain = 4u;
    game_initialize(&game);
    ok &= expect(game.ghostEatChain == 0u,
                 "new game initialization clears a stale ghost-eating combo");
    disable_non_red_ghosts(&game);
    GhostState& red = game.ghosts[0];
    red.x = 208;
    red.y = 184;
    red.offset = 1;
    red.speed = 1;
    red.condition = GhostCondition::Frightened;
    red.powerPillStepsRemaining = 10;
    red.collisionActive = true;
    game.pacman.x = red.x;
    game.pacman.y = red.y;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(red.condition == GhostCondition::Eaten && red.speed == 2 &&
                 red.powerPillStepsRemaining == 0 && !red.collisionActive,
                 "eaten ghost clears its timer, doubles speed, and loses lethality");
    bool returned = false;
    for (uint32_t step = 0; step < 2000u; ++step) {
        game_update(&game);
        if (red.condition == GhostCondition::Normal && red.releaseState == GhostReleaseState::Normal &&
            red.collisionActive && game.ghostReturned[0]) {
            returned = true;
            break;
        }
    }
    ok &= expect(returned, "eaten ghost follows the source gate bounce and returns to normal play");

    game_initialize(&game);
    disable_non_red_ghosts(&game);
    enable_normal_pink(&game);
    GhostState& lethalRed = game.ghosts[0];
    GhostState& frightenedPink = game.ghosts[1];
    lethalRed.x = game.pacman.x;
    lethalRed.y = game.pacman.y;
    lethalRed.speed = 0;
    lethalRed.condition = GhostCondition::Normal;
    lethalRed.collisionActive = true;
    frightenedPink.x = 100;
    frightenedPink.y = 100;
    frightenedPink.speed = 0;
    frightenedPink.condition = GhostCondition::Frightened;
    frightenedPink.powerPillStepsRemaining = 20;
    frightenedPink.collisionActive = true;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.speed = 0;
    game.ghostEatChain = 3u;
    game.frightenedFlashPhase = 9u;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.ghostEatChain == 0u &&
                 game.frightenedFlashPhase == 0u &&
                 frightenedPink.condition == GhostCondition::Normal &&
                 frightenedPink.powerPillStepsRemaining == 0u,
                 "death during another ghost's frightened interval clears timers, combo, and warning phase");

    game_initialize(&game);
    game.ghosts[0].condition = GhostCondition::Frightened;
    game.ghosts[0].powerPillStepsRemaining = 20;
    game.ghostEatChain = 3;
    game.ghosts[1].condition = GhostCondition::Returning;
    game.ghosts[1].powerPillStepsRemaining = 0;
    game.ghosts[1].releaseState = GhostReleaseState::ReturningHouse;
    game.ghosts[1].collisionActive = false;
    game.ghosts[1].speed = 2;
    game.ghosts[0].x = game.pacman.x;
    game.ghosts[0].y = game.pacman.y;
    game.ghosts[0].condition = GhostCondition::Normal;
    game.ghosts[0].collisionActive = true;
    game.ghosts[0].speed = 0;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.ghosts[1].powerPillStepsRemaining == 0 &&
                 game.ghostEatChain == 0u,
                 "death clears active frightened and returning timers and resets the combo");

    for (uint32_t step = 0; step < kPacManDeathDurationSteps + kPacManReadyAfterDeathSteps; ++step) {
        game_update(&game);
    }
    ok &= expect(game.ghosts[0].condition == GhostCondition::Normal &&
                 game.ghosts[0].powerPillStepsRemaining == 0,
                 "death actor reset restores normal ghost conditions");
    game.playState = PlayState::GameOver;
    game.ghostEatChain = 4;
    ok &= expect(game_restart_session(&game) && game.ghostEatChain == 0 &&
                 game.ghosts[0].powerPillStepsRemaining == 0,
                 "Game Over restart clears timers and score chain");
    return ok;
}

bool test_final_power_pill_and_renderer() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    keep_only_power_target(&game, 2, 1);
    set_before_target(&game, 2, 1, Direction::Right);
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.powerPillConsumed && game.playState == PlayState::LevelComplete &&
                 game.score == kPacManPowerPillScore + kPacManLevelCompleteBonus &&
                 game.level.totalConsumablesRemaining == 0,
                 "final power pill preserves completion precedence and historical level bonus");
    for (uint32_t step = 0; step < kPacManLevelCompleteDelaySteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath &&
                 game.ghosts[0].condition == GhostCondition::Normal &&
                 game.ghosts[0].powerPillStepsRemaining == 0,
                 "level reset clears frightened state and enters Ready after final power pill");

    static uint32_t spritePixels[256u * 352u];
    static uint32_t backgroundPixels[kPacManWidth * kPacManFrameHeight];
    static uint32_t framePixels[kPacManWidth * kPacManFrameHeight];
    for (uint32_t i = 0; i < 256u * 352u; ++i) spritePixels[i] = 0;
    for (uint32_t i = 0; i < kPacManWidth * kPacManFrameHeight; ++i) backgroundPixels[i] = 0;
    for (int y = 0; y < kPacManSpriteSize; ++y) {
        for (int x = 0; x < kPacManSpriteSize; ++x) {
            spritePixels[y * 256 + 128 + x] = 0x00112233u;
            spritePixels[y * 256 + 192 + x] = 0x00FFFFFFFFu;
            spritePixels[y * 256 + 160 + x] = 0x00445566u;
            spritePixels[y * 256 + 224 + x] = 0x00FFFFFFFFu;
            spritePixels[y * 256 + x] = 0x00778899u;
        }
    }
    game_initialize(&game);
    game.pacman.facingDirection = Direction::None;
    game.ghosts[0].x = 100;
    game.ghosts[0].y = 100;
    game.ghosts[0].condition = GhostCondition::Frightened;
    game.ghosts[0].powerPillStepsRemaining = 100;
    game.ghosts[0].collisionActive = true;
    game.ghosts[0].direction = Direction::Up;
    PacImage sprites{256u, 352u, 256u * 4u, spritePixels};
    ok &= expect(render_game_scene(&sprites, &game, backgroundPixels, framePixels,
                                   kPacManWidth * kPacManFrameHeight),
                 "renderer accepts historical sprite dimensions");
    const uint32_t centerPixel = framePixels[(32 + 100) * kPacManWidth + 100];
    ok &= expect(centerPixel == 0x00112233u, "frightened ghosts use the historical blue sprite column");
    game.frightenedFlashPhase = 8;
    render_game_scene(&sprites, &game, backgroundPixels, framePixels,
                      kPacManWidth * kPacManFrameHeight);
    ok &= expect(framePixels[(32 + 100) * kPacManWidth + 100] == 0x00778899u,
                 "frightened ghosts flash back to their normal body frame");
    game.ghosts[0].condition = GhostCondition::Eaten;
    game.ghosts[0].powerPillStepsRemaining = 0;
    render_game_scene(&sprites, &game, backgroundPixels, framePixels,
                      kPacManWidth * kPacManFrameHeight);
    ok &= expect(framePixels[(32 + 100) * kPacManWidth + 100] == 0x00445566u,
                 "eaten ghosts use the historical eyes sprite and mask");
    return ok;
}

bool test_buffered_turn_and_tunnel_counts() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    game.pacman.x = 24;
    game.pacman.y = 55;
    game.pacman.offset = 15;
    game.pacman.direction = Direction::Down;
    game.pacman.facingDirection = Direction::Down;
    game.pacman.requestedDirection = Direction::Down;
    game_press_direction(&game, Direction::Up);
    game_update(&game);
    ok &= expect(game.pacman.offset == 0, "buffered turn reaches alignment");
    game_update(&game);
    ok &= expect(game.turnAccepted && game.normalPillConsumed,
                 "pill consumption immediately after buffered reversal");

    game_initialize(&game);
    const uint32_t before = game.level.totalConsumablesRemaining;
    game.pacman.x = 8;
    game.pacman.y = 232;
    game.pacman.offset = 0;
    game.pacman.direction = Direction::Left;
    game.pacman.requestedDirection = Direction::Left;
    game_update(&game);
    ok &= expect(game.tunnelWrapped && game.level.totalConsumablesRemaining == before,
                 "tunnel traversal preserves consumable counts");
    return ok;
}

bool test_completion_reset_and_overflow() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    keep_only_normal_target(&game, 2, 1);
    set_before_target(&game, 2, 1, Direction::Right);
    game.score = 1234;
    game_update(&game);
    ok &= expect(game.playState == PlayState::LevelComplete && game.levelCompleteEntered,
                 "last consumable enters level-complete state");
    ok &= expect(game.levelCompleteTransitions == 1 && game.levelCompleteStepsRemaining == 100,
                 "completion transition and deterministic delay occur once");
    ok &= expect(game.score == 2244 && game.level.totalConsumablesRemaining == 0,
                 "last normal pill updates score, adds the historical level bonus, and reaches zero");
    for (uint32_t i = 0; i < 99; ++i) game_update(&game);
    ok &= expect(game.playState == PlayState::LevelComplete && game.levelResetCount == 0,
                 "completion delay keeps Pac-Man stopped");
    game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath && game.levelResetCount == 1,
                 "completion resets after one second and enters Ready");
    ok &= expect(game.score == 2244, "level reset preserves completed score");
    ok &= expect(game.levelNumber == 2, "level reset increments level");
    ok &= expect(game.level.normalPillsRemaining == 240 && game.level.powerPillsRemaining == 4 &&
                 game.level.totalConsumablesRemaining == 244,
                 "level reset restores exact consumable counts");
    ok &= expect(game.pacman.x == 224 && game.pacman.y == 376 &&
                   game.pacman.direction == Direction::Right && game.pacman.offset == 8 &&
                   game.pacman.requestedDirection == Direction::None,
                   "level reset restores Pac-Man start state");
    ok &= expect(game.ghosts[0].x == 224 && game.ghosts[0].y == 184 &&
                 game.ghosts[0].direction == Direction::Left && game.ghosts[0].targetX == 224 &&
                 game.ghosts[0].targetY == 376,
                 "level completion resets Red position direction and target");
    ok &= expect(!game.held.left && !game.held.right && !game.held.up && !game.held.down,
                 "level reset clears held directions");
    ok &= expect(game.levelCompleteTransitions == 1, "completion does not trigger more than once");

    game_initialize(&game);
    game.score = 0xFFFFFFFEu;
    add_score(game, 10);
    ok &= expect(game.score == 0xFFFFFFFFu, "score saturates at uint32 maximum");
    add_score(game, 1);
    ok &= expect(game.score == 0xFFFFFFFFu, "score remains bounded after saturation");

    game_initialize(&game);
    set_before_target(&game, 2, 1, Direction::Right);
    game.level.normalPillsRemaining = 0;
    game.level.totalConsumablesRemaining = 0;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.countUnderflow && game.score == 0 &&
                 game.level.cells[1][2] == CellType::Pill,
                 "consumable count underflow is prevented");
    return ok;
}

bool test_red_ghost_movement() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    game.pacman.x = 64;
    game.pacman.y = 376;
    game.pacman.direction = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    ok &= expect(game.ghosts[0].x == 224 && game.ghosts[0].y == 184 &&
                 game.ghosts[0].direction == Direction::Left && game.ghosts[0].speed == 1,
                 "Red starts at the historical position, deterministic direction, and speed");
    ok &= expect(game.ghosts[0].targetX == 224 && game.ghosts[0].targetY == 376,
                 "Red reset target starts at the historical Pac-Man spawn");
    const int pinkX = game.ghosts[1].x;
    const int pinkY = game.ghosts[1].y;
    const int cyanX = game.ghosts[2].x;
    const int cyanY = game.ghosts[2].y;
    const int orangeX = game.ghosts[3].x;
    const int orangeY = game.ghosts[3].y;

    game_update(&game);
    ok &= expect(game.ghosts[0].x == 223 && game.ghosts[0].y == 184 &&
                 game.ghosts[0].direction == Direction::Left && game.ghosts[0].animationFrame == 1,
                 "Red ghost advances one pixel per fixed step and animates independently of rendering");
    ok &= expect(game.ghosts[0].targetX == 64 && game.ghosts[0].targetY == 376,
                 "Red target updates to Pac-Man's current logical position");
    for (uint32_t step = 0; step < 15; ++step) game_update(&game);
    ok &= expect(game.ghosts[0].x == 208 && game.ghosts[0].offset == 0,
                 "Red ghost reaches the next maze intersection on grid alignment");

    game.pacman.x = 208;
    game.pacman.y = 24;
    game_update(&game);
    ok &= expect(game.ghosts[0].direction == Direction::Up && game.ghosts[0].y == 183,
                 "Red ghost chooses a target-directed legal turn at an intersection");
    ok &= expect(game.ghosts[0].targetX == 208 && game.ghosts[0].targetY == 24,
                 "Red target follows Pac-Man's current position directly");
    ok &= expect(choose_ghost_direction(game, game.ghosts[0], 208, 24) != Direction::Right,
                 "Red never reverses while another legal direction exists");
    for (uint32_t step = 0; step < 80; ++step) {
        game_update(&game);
        const int column = level_column_from_position(game.ghosts[0].x);
        const int row = level_row_from_position(game.ghosts[0].y);
        ok &= expect(is_walkable_cell(level_cell(game.level, column, row)),
                     "Red ghost remains inside walkable maze cells");
    }

    game.ghosts[0].x = 16;
    game.ghosts[0].y = 232;
    game.ghosts[0].direction = Direction::Left;
    game.ghosts[0].requestedDirection = Direction::Left;
    game.ghosts[0].offset = 0;
    game.ghosts[0].speed = 1;
    game.pacman.x = 208;
    game.pacman.y = 376;
    game_update(&game);
    ok &= expect(game.ghosts[0].x == 431 && game.ghosts[0].y == 232 &&
                  game.redTunnelWrapped,
                  "Red ghost uses the historical tunnel wrap");

    game_initialize(&game);
    disable_non_red_ghosts(&game);
    game.ghosts[0].x = 424;
    game.ghosts[0].y = 232;
    game.ghosts[0].direction = Direction::Right;
    game.ghosts[0].requestedDirection = Direction::Right;
    game.ghosts[0].offset = 0;
    game.ghosts[0].speed = 1;
    game.pacman.x = 208;
    game.pacman.y = 376;
    game_update(&game);
    ok &= expect(game.ghosts[0].x == 9 && game.ghosts[0].y == 232 && game.redTunnelWrapped,
                 "Red ghost wraps right-to-left through the tunnel");

    game_initialize(&game);
    game.ghosts[0].x = 16;
    game.ghosts[0].y = 216;
    game.ghosts[0].direction = Direction::Left;
    game.ghosts[0].requestedDirection = Direction::Left;
    game.ghosts[0].offset = 0;
    game.ghosts[0].speed = 1;
    game.pacman.x = 64;
    game.pacman.y = 376;
    game_update(&game);
    ok &= expect(game.ghosts[0].x == 16 && game.ghosts[0].y == 216 && !game.redTunnelWrapped,
                 "Red never wraps on a non-tunnel row");
    ok &= expect((game.ghosts[1].x != pinkX || game.ghosts[1].y != pinkY) &&
                 (game.ghosts[2].x != cyanX || game.ghosts[2].y != cyanY ||
                  game.ghosts[2].releaseState != GhostReleaseState::CyanHouseBounce) &&
                 game.ghosts[3].x == 256 && game.ghosts[3].y >= 224 && game.ghosts[3].y <= 240 &&
                 game.ghosts[3].speed == 1,
                 "Red, Pink, Cyan, and Orange each retain independent fixed-step movement state");
    return ok;
}

bool test_red_direction_selection() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);

    GhostState& red = game.ghosts[0];
    ok &= expect(red.targetX == game.pacman.x && red.targetY == game.pacman.y,
                 "Red starts with Pac-Man's current logical target");
    red.x = 208;
    red.y = 184;
    red.direction = Direction::Left;
    red.requestedDirection = Direction::Left;
    red.offset = 0;
    ok &= expect(choose_ghost_direction(game, red, 208, 24) == Direction::Up,
                 "Red targets Pac-Man directly at a legal intersection");
    const Direction firstChoice = choose_ghost_direction(game, red, 208, 24);
    const Direction secondChoice = choose_ghost_direction(game, red, 208, 24);
    ok &= expect(firstChoice == secondChoice, "Red direction choice is deterministic");

    int tieX = 0;
    int tieY = 0;
    Direction tieCurrent = Direction::None;
    Direction tieExpected = Direction::None;
    ok &= expect(find_tie_point(game, &tieX, &tieY, &tieCurrent, &tieExpected),
                 "Maze provides a deterministic multi-route tie point");
    if (tieExpected != Direction::None) {
        red.x = tieX;
        red.y = tieY;
        red.direction = tieCurrent;
        red.requestedDirection = tieCurrent;
        red.offset = 0;
        ok &= expect(choose_ghost_direction(game, red, tieX, tieY) == tieExpected,
                     "Equal-distance choices use the historical Up Down Left Right tie order");
    }

    GameState deadEndGame = game;
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            deadEndGame.level.cells[row][column] = CellType::Wall;
        }
    }
    deadEndGame.level.cells[5][5] = CellType::Empty;
    deadEndGame.level.cells[4][5] = CellType::Empty;
    GhostState deadEndRed = deadEndGame.ghosts[0];
    deadEndRed.x = 5 * kPacManTileSize + 8;
    deadEndRed.y = 5 * kPacManTileSize + 8;
    deadEndRed.direction = Direction::Down;
    deadEndRed.requestedDirection = Direction::Down;
    deadEndRed.offset = 0;
    ok &= expect(choose_ghost_direction(deadEndGame, deadEndRed, 224, 376) == Direction::Up,
                 "Red reverses only when a dead end leaves no alternative");

    red.x = 208;
    red.y = 184;
    red.direction = Direction::Left;
    const Direction targetBefore = choose_ghost_direction(game, red, 208, 24);
    const Direction targetAfter = choose_ghost_direction(game, red, 64, 376);
    ok &= expect(targetBefore != targetAfter,
                 "Red reacts predictably when Pac-Man changes target position");
    return ok;
}

bool test_moving_red_collisions() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    disable_non_red_ghosts(&game);

    int centerX = 0;
    int centerY = 0;
    Direction redDirection = Direction::None;
    Direction perpendicularDirection = Direction::None;
    ok &= expect(find_perpendicular_approach(game, &centerX, &centerY, &redDirection,
                                              &perpendicularDirection),
                 "Maze provides a perpendicular Red collision route");
    if (redDirection != Direction::None) {
        GhostState& red = game.ghosts[0];
        red.x = centerX;
        red.y = centerY;
        red.direction = redDirection;
        red.requestedDirection = redDirection;
        red.offset = 1;
        red.speed = 1;
        game.pacman.x = centerX - 15 * direction_x_for_test(perpendicularDirection);
        game.pacman.y = centerY - 15 * direction_y_for_test(perpendicularDirection);
        game.pacman.direction = perpendicularDirection;
        game.pacman.facingDirection = perpendicularDirection;
        game.pacman.requestedDirection = perpendicularDirection;
        game.pacman.offset = 1;
        game.pacman.speed = 1;
        const int redBeforeX = red.x;
        const int redBeforeY = red.y;
        game_update(&game);
        ok &= expect(game.playState == PlayState::Dying && game.collisionDetected &&
                     (red.x != redBeforeX || red.y != redBeforeY),
                     "Moving Red collides at a perpendicular intersection");
        const uint32_t deathTransitions = game.deathTransitions;
        const uint8_t livesAfterCollision = game.lives;
        game_update(&game);
        ok &= expect(game.playState == PlayState::Dying && game.deathTransitions == deathTransitions &&
                     game.lives == livesAfterCollision,
                     "One moving-Red collision triggers one death and suppresses duplicates");
    }

    game_initialize(&game);
    disable_non_red_ghosts(&game);
    ok &= expect(find_perpendicular_approach(game, &centerX, &centerY, &redDirection,
                                              &perpendicularDirection),
                 "Maze provides a horizontal Red collision route");
    if (redDirection != Direction::None) {
        GhostState& red = game.ghosts[0];
        red.x = centerX;
        red.y = centerY;
        red.direction = redDirection;
        red.requestedDirection = redDirection;
        red.offset = 1;
        red.speed = 1;
        const Direction pacmanDirection = opposite_direction(redDirection);
        game.pacman.x = centerX + 15 * direction_x_for_test(redDirection);
        game.pacman.y = centerY + 15 * direction_y_for_test(redDirection);
        game.pacman.direction = pacmanDirection;
        game.pacman.facingDirection = pacmanDirection;
        game.pacman.requestedDirection = pacmanDirection;
        game.pacman.offset = 1;
        game.pacman.speed = 1;
        game_update(&game);
        ok &= expect(game.playState == PlayState::Dying && game.lives == kPacManInitialLives - 1,
                     "Moving Red collides head-on and deducts exactly one life");
    }
    return ok;
}

bool test_red_state_gates() {
    bool ok = true;
    const PlayState states[] = {
        PlayState::Dying, PlayState::ReadyAfterDeath, PlayState::LevelComplete, PlayState::GameOver
    };
    for (uint32_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        GameState game{};
        game_initialize(&game);
        game.ghosts[0].x = 208;
        game.ghosts[0].y = 184;
        game.ghosts[0].direction = Direction::Left;
        game.ghosts[0].offset = 1;
        game.ghosts[0].speed = 1;
        game.playState = states[index];
        game.deathStepsRemaining = 100;
        game.readyStepsRemaining = 100;
        game.levelCompleteStepsRemaining = 100;
        const int beforeX = game.ghosts[0].x;
        const int beforeY = game.ghosts[0].y;
        game_update(&game);
        ok &= expect(game.ghosts[0].x == beforeX && game.ghosts[0].y == beforeY,
                     "Red stops during every non-Playing state");
    }
    return ok;
}

bool test_ghost_layout_and_collision() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    ok &= expect(kPacManGhostCount == 4, "exactly four ghosts exist");
    ok &= expect(game.ghosts[0].kind == GhostKind::Red &&
                 game.ghosts[1].kind == GhostKind::Pink &&
                 game.ghosts[2].kind == GhostKind::Cyan &&
                 game.ghosts[3].kind == GhostKind::Orange,
                 "ghost identities are stable and distinct");
    ok &= expect(game.ghosts[0].x == 224 && game.ghosts[0].y == 184 &&
                 game.ghosts[1].x == 192 && game.ghosts[1].y == 224 &&
                 game.ghosts[2].x == 224 && game.ghosts[2].y == 240 &&
                 game.ghosts[3].x == 256 && game.ghosts[3].y == 224,
                 "historical ghost starting coordinates");
    ok &= expect(game.ghosts[0].direction == Direction::Left &&
                 game.ghosts[1].direction == Direction::Up &&
                 game.ghosts[2].direction == Direction::Down &&
                 game.ghosts[3].direction == Direction::Up,
                 "historical ghost starting directions with deterministic red simplification");
    ok &= expect(game.ghosts[0].active && game.ghosts[1].active &&
                 game.ghosts[2].active && game.ghosts[3].active &&
                 game.ghosts[0].collisionActive && !game.ghosts[1].collisionActive &&
                 !game.ghosts[2].collisionActive && !game.ghosts[3].collisionActive,
                 "all four ghosts are rendered with house ghosts collision-gated");
    ok &= expect(level_cell(game.level, 11, 13) == CellType::GhostHouse &&
                 level_cell(game.level, 14, 14) == CellType::GhostHouse,
                 "ghost-house positions are walkable classified cells");

    const int initialX[] = {224, 192, 224, 256};
    const int initialY[] = {184, 224, 240, 224};
    for (uint32_t i = 0; i < kPacManGhostCount; ++i) {
        game.ghosts[i].x += 7;
        game.ghosts[i].y -= 5;
    }
    game_reset_level(&game);
    for (uint32_t i = 0; i < kPacManGhostCount; ++i) {
        ok &= expect(game.ghosts[i].x == initialX[i] && game.ghosts[i].y == initialY[i],
                     "ghost reset restores exact historical positions");
    }

    PacManState pacman = game.pacman;
    GhostState ghost = game.ghosts[0];
    pacman.y = ghost.y;
    pacman.x = ghost.x - 15;
    ok &= expect(pacman_collides_with_ghost(pacman, ghost), "collision from the left");
    pacman.x = ghost.x + 15;
    ok &= expect(pacman_collides_with_ghost(pacman, ghost), "collision from the right");
    pacman.x = ghost.x;
    pacman.y = ghost.y - 15;
    ok &= expect(pacman_collides_with_ghost(pacman, ghost), "collision from above");
    pacman.y = ghost.y + 15;
    ok &= expect(pacman_collides_with_ghost(pacman, ghost), "collision from below");
    pacman.x = ghost.x + 16;
    pacman.y = ghost.y;
    ok &= expect(!pacman_collides_with_ghost(pacman, ghost), "no collision at or outside threshold");
    ghost.active = false;
    pacman.x = ghost.x;
    ok &= expect(!pacman_collides_with_ghost(pacman, ghost), "inactive ghost does not collide");
    return ok;
}

void place_stationary_collision(GameState* game, GhostKind kind) {
    if (!game) return;
    const uint32_t index = static_cast<uint32_t>(kind);
    game->ghosts[index].x = 180;
    game->ghosts[index].y = 200;
    game->ghosts[index].active = true;
    game->ghosts[index].collisionActive = true;
    game->pacman.x = game->ghosts[index].x;
    game->pacman.y = game->ghosts[index].y;
    game->pacman.offset = 1;
    game->pacman.direction = Direction::None;
    game->pacman.requestedDirection = Direction::None;
    game->pacman.speed = 1;
    game->ghosts[index].speed = 0;
}

bool test_cyan_collisions_and_state_gates() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    disable_non_cyan_ghosts(&game);
    enable_normal_cyan(&game);
    GhostState& cyan = game.ghosts[2];

    int centerX = 0;
    int centerY = 0;
    Direction cyanDirection = Direction::None;
    Direction perpendicularDirection = Direction::None;
    ok &= expect(find_perpendicular_approach(game, &centerX, &centerY, &cyanDirection,
                                              &perpendicularDirection),
                 "Maze provides a perpendicular Cyan collision route");
    if (cyanDirection != Direction::None) {
        cyan.x = centerX;
        cyan.y = centerY;
        cyan.direction = cyanDirection;
        cyan.requestedDirection = cyanDirection;
        cyan.offset = 1;
        cyan.speed = 1;
        game.pacman.x = centerX - 15 * direction_x_for_test(perpendicularDirection);
        game.pacman.y = centerY - 15 * direction_y_for_test(perpendicularDirection);
        game.pacman.direction = Direction::None;
        game.pacman.facingDirection = Direction::None;
        game.pacman.requestedDirection = Direction::None;
        game.pacman.offset = 1;
        game.pacman.speed = 0;
        const int beforeX = cyan.x;
        const int beforeY = cyan.y;
        game_update(&game);
        ok &= expect(game.playState == PlayState::Dying && game.collisionDetected &&
                     (cyan.x != beforeX || cyan.y != beforeY),
                     "Moving Cyan collides at a perpendicular intersection");
    }

    game_initialize(&game);
    disable_non_cyan_ghosts(&game);
    enable_normal_cyan(&game);
    cyan = game.ghosts[2];
    cyan.x = 208;
    cyan.y = 184;
    cyan.direction = Direction::Right;
    cyan.requestedDirection = Direction::Right;
    cyan.offset = 1;
    cyan.speed = 1;
    game.pacman.x = 223;
    game.pacman.y = 184;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.lives == kPacManInitialLives - 1,
                 "Moving Cyan collides head-on and deducts one life");

    game_initialize(&game);
    disable_non_cyan_ghosts(&game);
    enable_normal_cyan(&game);
    cyan = game.ghosts[2];
    cyan.x = 16;
    cyan.y = 232;
    cyan.direction = Direction::Left;
    cyan.requestedDirection = Direction::Left;
    cyan.offset = 1;
    cyan.speed = 1;
    game.pacman.x = 431;
    game.pacman.y = 232;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.cyanTunnelWrapped,
                 "Moving Cyan collision at a tunnel edge is deterministic");

    game_initialize(&game);
    enable_normal_cyan(&game);
    enable_normal_pink(&game);
    game.ghosts[0].x = 180;
    game.ghosts[0].y = 200;
    game.ghosts[0].speed = 0;
    game.ghosts[1].x = 180;
    game.ghosts[1].y = 200;
    game.ghosts[1].speed = 0;
    game.ghosts[2].x = 180;
    game.ghosts[2].y = 200;
    game.ghosts[2].speed = 0;
    game.pacman.x = 180;
    game.pacman.y = 200;
    game.pacman.direction = Direction::None;
    game.pacman.facingDirection = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.offset = 1;
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.lives == kPacManInitialLives - 1 &&
                 game.deathTransitions == 1,
                 "Simultaneous Red, Pink, and Cyan overlap deducts one life only");

    const PlayState states[] = {
        PlayState::Dying, PlayState::ReadyAfterDeath, PlayState::LevelComplete, PlayState::GameOver
    };
    for (uint32_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        game_initialize(&game);
        enable_normal_cyan(&game);
        game.ghosts[2].x = 208;
        game.ghosts[2].y = 184;
        game.ghosts[2].direction = Direction::Right;
        game.ghosts[2].requestedDirection = Direction::Right;
        game.ghosts[2].offset = 1;
        game.ghosts[2].speed = 1;
        game.playState = states[index];
        game.deathStepsRemaining = 100;
        game.readyStepsRemaining = 100;
        game.levelCompleteStepsRemaining = 100;
        const int oldX = game.ghosts[2].x;
        const int oldY = game.ghosts[2].y;
        game_update(&game);
        ok &= expect(game.ghosts[2].x == oldX && game.ghosts[2].y == oldY,
                     "Cyan stops during every non-Playing state");
    }
    return ok;
}

bool test_cyan_reset_lifecycle() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    place_stationary_collision(&game, GhostKind::Cyan);
    game_update(&game);
    for (uint32_t step = 0; step < kPacManDeathDurationSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath &&
                 game.ghosts[2].x == 224 && game.ghosts[2].y == 240 &&
                 game.ghosts[2].direction == Direction::Down &&
                 game.ghosts[2].releaseState == GhostReleaseState::CyanHouseBounce &&
                 !game.ghosts[2].collisionActive,
                 "Cyan resets to the historical house release state after death");
    ok &= expect(game.ghosts[3].x == 256 && game.ghosts[3].y == 224 &&
                 game.ghosts[3].direction == Direction::Up &&
                 game.ghosts[3].releaseState == GhostReleaseState::OrangeHouseBounce &&
                 game.ghosts[3].speed == 1 && !game.ghosts[3].collisionActive,
                 "Orange resets to its historical house release state after death");

    game_reset_level(&game);
    ok &= expect(game.levelNumber == 2 && game.ghosts[2].releaseState == GhostReleaseState::CyanHouseBounce &&
                 game.ghosts[2].x == 224 && game.ghosts[2].y == 240 &&
                 !game.ghosts[2].collisionActive,
                 "Cyan release state resets after level completion");
    game.playState = PlayState::GameOver;
    ok &= expect(game_restart_session(&game) && game.ghosts[2].releaseState == GhostReleaseState::CyanHouseBounce &&
                 game.ghosts[2].x == 224 && game.ghosts[2].y == 240 &&
                 game.ghosts[2].direction == Direction::Down,
                 "Cyan release state resets after Game Over restart");
    ok &= expect(game.ghosts[3].x == 256 && game.ghosts[3].y == 224 &&
                  game.ghosts[3].direction == Direction::Up &&
                  game.ghosts[3].releaseState == GhostReleaseState::OrangeHouseBounce &&
                  game.ghosts[3].speed == 1 && !game.ghosts[3].collisionActive,
                  "Orange release state resets across level and session resets");
    return ok;
}

bool test_death_lives_and_reset() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    game.score = 120;
    game.level.cells[1][1] = CellType::Empty;
    game.level.normalPillsRemaining = 239;
    game.level.totalConsumablesRemaining = 243;
    const uint32_t scoreBefore = game.score;
    const uint32_t remainingBefore = game.level.totalConsumablesRemaining;
    game_press_direction(&game, Direction::Right);
    place_stationary_collision(&game, GhostKind::Red);
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.deathEntered && game.collisionDetected,
                 "collision enters bounded death state");
    ok &= expect(game.lives == kPacManInitialLives - 1 && game.lifeDecremented,
                 "one overlap consumes exactly one life");
    ok &= expect(game.score == scoreBefore && game.level.totalConsumablesRemaining == remainingBefore,
                 "score and remaining count are unchanged by death");
    ok &= expect(!game.held.left && game.held.right && !game.held.up && !game.held.down &&
                 game.pacman.direction == Direction::None &&
                 game.pacman.requestedDirection == Direction::Right,
                 "death stops movement but preserves a still-held direction through Ready");

    const int dyingX = game.pacman.x;
    const int dyingY = game.pacman.y;
    for (uint32_t i = 0; i < 20; ++i) game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.pacman.x == dyingX && game.pacman.y == dyingY &&
                 game.lives == kPacManInitialLives - 1,
                 "dying state blocks movement and duplicate life loss");
    const uint32_t scoreDuringDeath = game.score;
    const uint32_t remainingDuringDeath = game.level.totalConsumablesRemaining;
    game_update(&game);
    ok &= expect(game.score == scoreDuringDeath &&
                 game.level.totalConsumablesRemaining == remainingDuringDeath,
                 "dying state blocks pill consumption");

    for (uint32_t i = 0; i < kPacManDeathDurationSteps - 21u; ++i) game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath && game.actorReset,
                 "non-final death resets actors into ready pause");
    ok &= expect(game.pacman.x == 224 && game.pacman.y == 376 &&
                 game.pacman.direction == Direction::Right && game.pacman.offset == 8 &&
                 game.pacman.requestedDirection == Direction::Right && game.held.right,
                 "Pac-Man reset restores spawn state while retaining the held Ready direction");
    ok &= expect(game.ghosts[0].x == 224 && game.ghosts[0].y == 184 &&
                 game.ghosts[1].x == 192 && game.ghosts[1].y == 224 &&
                 game.ghosts[2].x == 224 && game.ghosts[2].y == 240 &&
                 game.ghosts[3].x == 256 && game.ghosts[3].y == 224,
                 "actor reset restores all ghost positions");
    ok &= expect(game.score == scoreBefore &&
                 game.level.totalConsumablesRemaining == remainingBefore && game.levelNumber == 1,
                 "death preserves score, pills, remaining count, and level");

    for (uint32_t i = 0; i < kPacManReadyAfterDeathSteps; ++i) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing, "ready-after-death pause is bounded");
    return ok;
}

bool test_game_over_and_restart() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    game.lives = 1;
    game.score = 9876;
    game.levelNumber = 3;
    game.level.cells[1][1] = CellType::Empty;
    game.level.normalPillsRemaining = 239;
    game.level.totalConsumablesRemaining = 243;
    place_stationary_collision(&game, GhostKind::Cyan);
    game_update(&game);
    for (uint32_t i = 0; i < kPacManDeathDurationSteps; ++i) game_update(&game);
    ok &= expect(game.playState == PlayState::GameOver && game.gameOverEntered,
                 "final death enters Game Over");
    ok &= expect(game.lives == 0, "lives cannot underflow on final death");
    ok &= expect(game.score == 9876 && game.levelNumber == 3,
                 "Game Over preserves final score and level display state");
    const int gameOverX = game.pacman.x;
    game_press_direction(&game, Direction::Right);
    game_update(&game);
    ok &= expect(game.pacman.x == gameOverX && game.playState == PlayState::GameOver,
                 "Game Over blocks movement");

    ok &= expect(game_restart_session(&game), "restart is accepted from Game Over");
    ok &= expect(game.score == 0 && game.levelNumber == 1 && game.lives == kPacManInitialLives,
                 "restart resets score, level, and historical lives");
    ok &= expect(game.level.normalPillsRemaining == 240 && game.level.powerPillsRemaining == 4 &&
                 game.level.totalConsumablesRemaining == 244,
                 "restart restores all 244 consumables");
    ok &= expect(game.pacman.x == 224 && game.pacman.y == 376 &&
                  game.ghosts[0].x == 224 && game.ghosts[0].y == 184 &&
                 game.ghosts[1].x == 192 && game.ghosts[1].y == 224 &&
                 game.ghosts[2].x == 224 && game.ghosts[2].y == 240 &&
                  game.ghosts[3].x == 256 && game.ghosts[3].y == 224,
                  "restart resets actor positions");
    ok &= expect(game.ghosts[0].direction == Direction::Left && game.ghosts[0].targetX == 224 &&
                 game.ghosts[0].targetY == 376,
                 "restart restores Red's deterministic initial direction and target");
    ok &= expect(!game.held.left && !game.held.right && !game.held.up && !game.held.down &&
                 game.playState == PlayState::ReadyAfterDeath,
                 "restart clears input and enters ready state");
    for (uint32_t i = 0; i < kPacManReadyAfterDeathSteps; ++i) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing, "restart ready state resumes play");
    ok &= expect(game_direction_for_key(27) == Direction::None &&
                 !game_restart_session(&game),
                 "Escape handling remains separate from restart");
    return ok;
}

bool test_level_rules_and_consecutive_progression() {
    bool ok = true;

    const LevelRules levelOne = calculate_level_rules(1u);
    const LevelRules levelTwo = calculate_level_rules(2u);
    const LevelRules midLevel = calculate_level_rules(4u);
    const LevelRules highestLevel = calculate_level_rules(8u);
    const LevelRules beyondTable = calculate_level_rules(9u);
    const LevelRules veryHigh = calculate_level_rules(0xFFFFFFFFu);
    const LevelRules zero = calculate_level_rules(0u);
    ok &= expect(levelOne.level == 1u && levelOne.pacmanMovePixelsPerStep == 1u &&
                 levelOne.normalGhostMovePixelsPerStep == 1u &&
                 levelOne.frightenedGhostMoveIntervalSteps == 2u &&
                 levelOne.frightenedDurationSteps == 900u &&
                 levelOne.frightenedFlashStartSteps == 200u,
                 "Level 1 uses fixed movement and 900/200 frightened rules");
    ok &= expect(levelTwo.level == 2u && levelTwo.frightenedDurationSteps == 800u &&
                 levelTwo.frightenedFlashStartSteps == 200u,
                 "Level 2 reduces only the historical frightened duration");
    ok &= expect(midLevel.level == 4u && midLevel.frightenedDurationSteps == 600u,
                 "mid-level rule calculation is deterministic");
    ok &= expect(highestLevel.level == 8u && highestLevel.frightenedDurationSteps == 200u,
                 "highest explicit historical level uses 200 frightened ticks");
    ok &= expect(beyondTable.level == 8u && beyondTable.frightenedDurationSteps == 200u &&
                 veryHigh.level == 8u && veryHigh.frightenedDurationSteps == 200u &&
                 zero.level == 1u,
                 "levels beyond the historical table saturate without underflow");
    ok &= expect(levelOne.frightenedDurationSteps != 0u && highestLevel.frightenedDurationSteps != 0u,
                 "historical level clamp makes a zero-duration level unreachable");

    GameState speedGame{};
    game_initialize(&speedGame);
    ok &= expect(speedGame.levelNumber == 1u && speedGame.pacman.speed == 1 &&
                 speedGame.ghosts[0].speed == 1,
                 "new sessions begin at level 1 with one-pixel movement");
    speedGame.gameSpeed = 2u;
    game_reset_level(&speedGame);
    ok &= expect(speedGame.levelNumber == 2u && speedGame.pacman.speed == 2 &&
                 speedGame.ghosts[0].speed == 2 &&
                 game_frightened_duration_steps(speedGame) == 400u &&
                 game_frightened_flash_threshold(speedGame) == 100u,
                 "shared historical Game.Speed scales actor pixels and timer division");
    speedGame.gameSpeed = 0u;
    ok &= expect(game_frightened_duration_steps(speedGame) == 800u,
                 "zero Game.Speed is normalized before division");
    speedGame.gameSpeed = 0xFFFFFFFFu;
    ok &= expect(game_frightened_duration_steps(speedGame) == 200u &&
                 game_frightened_flash_threshold(speedGame) == 50u,
                 "very high Game.Speed remains bounded by the historical maximum");
    speedGame.levelNumber = 0xFFFFFFFFu;
    game_reset_level(&speedGame);
    ok &= expect(speedGame.levelNumber == kPacManHistoricalMaximumLevel,
                 "level reset saturates a very high counter instead of wrapping");

    GameState game{};
    game_initialize(&game);
    game.score = 50u;
    game.lives = 2u;
    game.ghostEatChain = 4u;
    game.ghosts[0].condition = GhostCondition::Eaten;
    game.ghosts[0].releaseState = GhostReleaseState::ReturningHouse;
    game.ghosts[0].powerPillStepsRemaining = 17u;
    game_press_direction(&game, Direction::Left);

    for (uint32_t transition = 0; transition < 10u; ++transition) {
        keep_only_normal_target(&game, 2, 1);
        set_before_target(&game, 2, 1, Direction::Right);
        game.pacman.speed = 0;
        game_update(&game);
        ok &= expect(game.playState == PlayState::LevelComplete,
                     "each final consumable enters exactly one completion state");

        for (uint32_t step = 0; step < kPacManLevelCompleteDelaySteps; ++step) game_update(&game);
        const uint32_t expectedLevel = transition < 7u ? transition + 2u : 8u;
        ok &= expect(game.playState == PlayState::ReadyAfterDeath &&
                     game.levelNumber == expectedLevel &&
                     game.level.totalConsumablesRemaining == 244u &&
                     game.readyStepsRemaining == calculate_level_rules(expectedLevel).readyDurationSteps,
                     "completion resets the maze once and enters the next-level Ready state");
        ok &= expect(game.score == 50u + (transition + 1u) *
                     (kPacManNormalPillScore + kPacManLevelCompleteBonus) &&
                     game.lives == (transition == 9u ? 3u : 2u),
                     "score and lives survive repeated level transitions");
        ok &= expect(game.pacman.x == 224 && game.pacman.y == 376 &&
                     game.pacman.direction == Direction::Right &&
                     game.pacman.requestedDirection == Direction::None &&
                     !game.held.left && !game.held.right && !game.held.up && !game.held.down,
                     "next-level reset clears input and restores Pac-Man");
        for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
            ok &= expect(game.ghosts[index].condition == GhostCondition::Normal &&
                         game.ghosts[index].powerPillStepsRemaining == 0u &&
                         game.ghosts[index].condition != GhostCondition::Returning &&
                         game.ghosts[index].releaseState != GhostReleaseState::ReturningHouse,
                         "next-level reset clears frightened and returning ghost state");
        }
        ok &= expect(game.ghostEatChain == 0u,
                     "next-level reset clears the ghost-eating chain");
        for (uint32_t step = 0; step < calculate_level_rules(expectedLevel).readyDurationSteps; ++step) {
            game_update(&game);
        }
        ok &= expect(game.playState == PlayState::Playing,
                     "Ready resumes gameplay on every consecutive level");
    }

    ok &= expect(game.levelResetCount == 10u && game.levelNumber == 8u &&
                 game_frightened_duration_steps(game) == 200u,
                 "ten consecutive simulated transitions remain bounded at level 8");
    return ok;
}

void prepare_fruit_test_game(GameState* game) {
    game_initialize(game);
    game->pacman.x = 64;
    game->pacman.y = 264;
    game->pacman.direction = Direction::None;
    game->pacman.facingDirection = Direction::Right;
    game->pacman.requestedDirection = Direction::None;
    game->pacman.offset = 0;
    game->pacman.speed = 0;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        game->ghosts[index].speed = 0;
        game->ghosts[index].collisionActive = false;
    }
    game->suppressGhostCollisionsForValidation = true;
}

bool test_fruit_and_extra_life() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    const FruitRules levelOne = calculate_fruit_rules(1u);
    const FruitRules levelTwo = calculate_fruit_rules(2u);
    const FruitRules levelFour = calculate_fruit_rules(4u);
    const FruitRules levelFive = calculate_fruit_rules(5u);
    const FruitRules levelEight = calculate_fruit_rules(8u);
    const FruitRules beyond = calculate_fruit_rules(0xFFFFFFFFu);

    ok &= expect(game.fruit.phase == FruitPhase::Inactive &&
                 game.fruit.appearancesTriggered == 0u,
                 "fruit begins inactive with no appearance trigger consumed");
    ok &= expect(levelOne.type == 0u && levelOne.score == 500u &&
                 levelOne.sprite.x == 0 && levelOne.sprite.y == 256 &&
                 levelOne.mask.x == 128 && levelOne.mask.y == 256,
                 "Level 1 fruit is the Cherry cell and mask");
    ok &= expect(levelTwo.type == 1u && levelTwo.score == 1000u &&
                 levelTwo.sprite.x == 32 && levelTwo.mask.x == 160,
                 "Level 2 fruit is the Strawberry cell and mask");
    ok &= expect(levelFour.type == 3u && levelFour.score == 2000u &&
                 levelFour.sprite.x == 96 && levelFour.sprite.y == 256,
                 "Level 4 fruit is the Apple cell");
    ok &= expect(levelFive.type == 4u && levelFive.score == 2500u &&
                 levelFive.sprite.x == 0 && levelFive.sprite.y == 288 &&
                 levelFive.mask.x == 128 && levelFive.mask.y == 288,
                 "Level 5 fruit is the Melon cell and mask");
    ok &= expect(levelEight.type == 7u && levelEight.score == 4000u &&
                 levelEight.sprite.x == 96 && levelEight.sprite.y == 288 &&
                 levelEight.mask.x == 224 && levelEight.mask.y == 288,
                 "Level 8 fruit is the Key cell and mask");
    ok &= expect(beyond.type == levelEight.type && beyond.score == levelEight.score &&
                 beyond.sprite.x == levelEight.sprite.x && beyond.mask.x == levelEight.mask.x,
                 "fruit mapping safely saturates above Level 8");
    for (uint32_t level = 1u; level <= 8u; ++level) {
        const FruitRules mapping = calculate_fruit_rules(level);
        const int expectedX = static_cast<int>((level - 1u) % 4u) * 32;
        const int expectedY = static_cast<int>((level - 1u) / 4u) * 32 + 256;
        ok &= expect(mapping.type == level - 1u && mapping.score == level * 500u &&
                     mapping.sprite.x == expectedX && mapping.sprite.y == expectedY &&
                     mapping.mask.x == expectedX + 128 && mapping.mask.y == expectedY,
                     "each historical level selects its fruit value, sprite cell, and mask");
    }
    ok &= expect(levelOne.x == 232 && levelOne.y == 280 &&
                 levelOne.triggerTimeSteps == 4000u &&
                 levelOne.expirationTimeSteps == 5000u &&
                 levelOne.visibleDurationSteps == 1000u,
                 "fruit uses the historical center and 4000/5000 timer boundaries");
    ok &= expect(std::strcmp(fruit_type_name(0u), "cherry") == 0 &&
                 std::strcmp(fruit_type_name(7u), "key") == 0,
                 "fruit names match the PacPics identities");

    prepare_fruit_test_game(&game);
    game.fruit.timeCountSteps = levelOne.triggerTimeSteps - 2u;
    game_update(&game);
    ok &= expect(game.fruit.phase == FruitPhase::Inactive &&
                 !game.fruitTriggerReached && game.fruit.timeCountSteps == 3999u,
                 "fruit does not appear before the exact first trigger boundary");
    game_update(&game);
    ok &= expect(game.fruitTriggerReached && game.fruitSpawned &&
                 game.fruit.phase == FruitPhase::Visible &&
                 game.fruit.appearancesTriggered == 1u &&
                 game.fruit.fruitType == 0u && game.fruit.scoreValue == 500u &&
                 game.fruit.visibleStepsRemaining == 1000u,
                 "fruit appears exactly once at TimeCount 4000");
    for (uint32_t step = 0; step < 1000u; ++step) game_update(&game);
    ok &= expect(game.fruit.phase == FruitPhase::Visible &&
                 game.fruit.timeCountSteps == 5000u &&
                 game.fruit.visibleStepsRemaining == 0u,
                 "fruit remains visible through the exact expiration count");
    game_update(&game);
    ok &= expect(game.fruitExpired && game.fruit.phase == FruitPhase::Inactive &&
                 game.fruit.timeCountSteps == 5000u,
                 "fruit expires at the bounded expiration boundary");
    game_update(&game);
    ok &= expect(game.fruit.timeCountSteps == 5000u && !game.fruitExpired,
                 "expired fruit timer cannot underflow or repeat the expiration event");
    game.pacman.x = kPacManFruitCenterX;
    game.pacman.y = kPacManFruitCenterY;
    game_update(&game);
    ok &= expect(!game.fruitConsumed && game.fruit.phase == FruitPhase::Inactive && game.score == 0u,
                 "expired fruit cannot be collected by a later overlap");

    prepare_fruit_test_game(&game);
    game.pacman.x = kPacManFruitCenterX;
    game.pacman.y = kPacManFruitCenterY;
    game_update(&game);
    ok &= expect(!game.fruitConsumed && game.fruit.phase == FruitPhase::Inactive && game.score == 0u,
                 "fruit cannot be collected before its level trigger has spawned it");

    FruitState collisionFruit = game.fruit;
    collisionFruit.phase = FruitPhase::Visible;
    ok &= expect(pacman_collides_with_fruit(PacManState{217, 280, Direction::Right,
                    Direction::Right, Direction::None, 0, 0, 0, 1, 0}, collisionFruit) &&
                 pacman_collides_with_fruit(PacManState{247, 280, Direction::Left,
                    Direction::Left, Direction::None, 0, 0, 0, 1, 0}, collisionFruit) &&
                 pacman_collides_with_fruit(PacManState{232, 280, Direction::Up,
                    Direction::Up, Direction::None, 0, 0, 0, 1, 0}, collisionFruit) &&
                 !pacman_collides_with_fruit(PacManState{232, 279, Direction::Down,
                    Direction::Down, Direction::None, 0, 0, 0, 1, 0}, collisionFruit),
                 "fruit collision preserves the source strict-X/exact-Y rule");

    prepare_fruit_test_game(&game);
    game.fruit.phase = FruitPhase::Visible;
    game.fruit.appearancesTriggered = 1u;
    game.fruit.scoreValue = 500u;
    game.fruit.x = 232;
    game.fruit.y = 280;
    game.pacman.x = 232;
    game.pacman.y = 280;
    game.score = 0u;
    game_update(&game);
    ok &= expect(game.fruitConsumed && game.fruitScoreAwarded &&
                 game.fruit.phase == FruitPhase::ScorePopup &&
                 game.fruit.scoreValue == 500u &&
                 game.fruit.popupStepsRemaining == kPacManFruitScorePopupDurationSteps &&
                 game.score == 500u,
                 "fruit collision awards its score once and opens exact-value feedback");
    game_update(&game);
    ok &= expect(!game.fruitConsumed && game.fruit.phase == FruitPhase::ScorePopup &&
                 game.fruit.popupStepsRemaining == kPacManFruitScorePopupDurationSteps - 1u &&
                 game.score == 500u,
                 "continued overlap cannot rescore fruit and popup time advances once per game step");
    for (uint32_t step = 0; step < kPacManFruitScorePopupDurationSteps - 2u; ++step) game_update(&game);
    ok &= expect(game.fruit.phase == FruitPhase::ScorePopup && game.fruit.popupStepsRemaining == 1u,
                 "fruit score popup remains visible through its final simulation step");
    game_update(&game);
    ok &= expect(game.fruit.phase == FruitPhase::Inactive && game.fruit.popupStepsRemaining == 0u &&
                 game.score == 500u,
                 "fruit score popup expires at exactly 100 gameplay steps without changing score");

    prepare_fruit_test_game(&game);
    game.fruit.phase = FruitPhase::Visible;
    game.fruit.appearancesTriggered = 1u;
    game.fruit.scoreValue = 500u;
    game.fruit.x = 232;
    game.fruit.y = 280;
    game.pacman.x = 232;
    game.pacman.y = 280;
    game.score = 9500u;
    game.lives = kPacManInitialLives;
    game.lifeAward.thresholdsAwardedMask = 0u;
    game.lifeAward.awardsGranted = 0u;
    game.lifeAward.nextThreshold = 10000u;
    game_update(&game);
    ok &= expect(game.fruitConsumed && game.score == 10000u && game.lives == 4u &&
                 game.lifeAward.thresholdsAwardedMask == 1u &&
                 game.lifeAward.awardsGranted == 1u && game.extraLifeAwarded,
                 "fruit score crossing 10000 grants the historical extra life");

    prepare_fruit_test_game(&game);
    game.fruit.phase = FruitPhase::Visible;
    game.fruit.appearancesTriggered = 1u;
    game.fruit.timeCountSteps = 100u;
    game.pacman.x = 64;
    game.pacman.y = 280;
    game.ghosts[0].x = 64;
    game.ghosts[0].y = 280;
    game.ghosts[0].collisionActive = true;
    game.suppressGhostCollisionsForValidation = false;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.fruit.phase == FruitPhase::Visible &&
                 game.fruit.timeCountSteps == 101u,
                 "ordinary death preserves visible fruit through the collision step");
    game_update(&game);
    ok &= expect(game.fruit.timeCountSteps == 101u,
                 "ordinary death pauses the fruit timer after the collision step");

    prepare_fruit_test_game(&game);
    game.fruit.phase = FruitPhase::ScorePopup;
    game.fruit.appearancesTriggered = 1u;
    game.fruit.popupStepsRemaining = 40u;
    game.pacman.x = 64;
    game.pacman.y = 280;
    game.ghosts[0].x = 64;
    game.ghosts[0].y = 280;
    game.ghosts[0].collisionActive = true;
    game.suppressGhostCollisionsForValidation = false;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.fruit.phase == FruitPhase::Inactive &&
                 game.fruit.popupStepsRemaining == 0u && game.fruit.appearancesTriggered == 1u,
                 "death clears transient fruit score feedback without reopening the spawn opportunity");

    game.fruit.phase = FruitPhase::ScorePopup;
    game.fruit.popupStepsRemaining = 60u;
    game.fruit.timeCountSteps = 1200u;
    game_reset_level(&game);
    ok &= expect(game.fruit.phase == FruitPhase::Inactive &&
                 game.fruit.appearancesTriggered == 0u && game.fruit.fruitType == 1u &&
                 game.fruit.scoreValue == 1000u && game.fruit.timeCountSteps == 0u &&
                 game.fruit.popupStepsRemaining == 0u,
                 "level transition clears fruit timers/feedback and selects Level 2");

    game.score = 9999u;
    game.lives = kPacManInitialLives;
    game.lifeAward.thresholdsAwardedMask = 0u;
    game.lifeAward.awardsGranted = 0u;
    game.lifeAward.nextThreshold = 10000u;
    ScoreAwardResult noAward = award_score(game, 0u);
    ok &= expect(noAward.extraLivesAwarded == 0u && game.lives == kPacManInitialLives,
                 "zero-point scoring does not award an extra life");
    ScoreAwardResult exact = award_score(game, 1u);
    ok &= expect(exact.extraLivesAwarded == 1u && game.lives == kPacManInitialLives + 1u &&
                 game.lifeAward.nextThreshold == 50000u,
                 "exact 10000 threshold grants one extra life");
    ScoreAwardResult repeat = award_score(game, 1u);
    ok &= expect(repeat.extraLivesAwarded == 0u && game.lives == kPacManInitialLives + 1u,
                 "a crossed threshold cannot award twice");
    game.score = 0u;
    game.lives = kPacManInitialLives;
    game.lifeAward.thresholdsAwardedMask = 0u;
    game.lifeAward.awardsGranted = 0u;
    game.lifeAward.nextThreshold = 10000u;
    ScoreAwardResult jump = award_score(game, 100000u);
    ok &= expect(jump.extraLivesAwarded == 3u && game.lives == kPacManInitialLives + 3u &&
                 game.lifeAward.awardsGranted == 3u && game.lifeAward.nextThreshold == 0xFFFFFFFFu,
                 "one score addition crosses all three historical life thresholds");
    game.score = 9999u;
    game.lives = kPacManMaximumLives;
    game.lifeAward.thresholdsAwardedMask = 0u;
    game.lifeAward.awardsGranted = 0u;
    game.lifeAward.nextThreshold = 10000u;
    award_score(game, 1u);
    ok &= expect(game.lives == kPacManMaximumLives && game.extraLifeSuppressed,
                 "life award is bounded at the native maximum without overflow");
    game.lives = 4u;
    game.score = 10000u;
    game.playState = PlayState::Dying;
    game_update(&game);
    ok &= expect(game.lives == 4u && game.lifeAward.awardsGranted == 0u,
                 "death does not clear or duplicate extra-life state");
    game.playState = PlayState::Dying;
    game.lives = 0u;
    game.deathStepsRemaining = 1u;
    game.fruit.phase = FruitPhase::Visible;
    game.fruit.appearancesTriggered = 1u;
    game.fruit.timeCountSteps = 4567u;
    game.fruit.visibleStepsRemaining = 433u;
    game.fruit.popupStepsRemaining = 0u;
    game_update(&game);
    ok &= expect(game.playState == PlayState::GameOver && game.fruit.phase == FruitPhase::Inactive &&
                 game.fruit.appearancesTriggered == 0u && game.fruit.timeCountSteps == 0u &&
                 game.fruit.visibleStepsRemaining == 0u && game.fruit.popupStepsRemaining == 0u &&
                 game.fruitReset,
                 "Game Over clears active fruit, lifetime, and per-level spawn state");
    ok &= expect(game_restart_session(&game) && game.score == 0u &&
                 game.lives == kPacManInitialLives && game.levelNumber == 1u &&
                 game.lifeAward.awardsGranted == 0u &&
                 game.lifeAward.thresholdsAwardedMask == 0u &&
                 game.fruit.phase == FruitPhase::Inactive &&
                 game.fruit.appearancesTriggered == 0u && game.fruit.timeCountSteps == 0u &&
                 game.fruit.popupStepsRemaining == 0u,
                 "new game restart clears fruit and extra-life state");
    return ok;
}

bool test_session_hud_and_power_pill_blink() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    ok &= expect(game.score == 0u && game.lives == kPacManInitialLives &&
                 game.levelNumber == 1u && game.highScore == kPacManInitialHighScore,
                 "session starts with historical score, lives, level, and high score");

    game_begin_initial_ready(&game);
    const int initialX = game.pacman.x;
    ok &= expect(game.playState == PlayState::InitialReady && game.readyTextVisible &&
                 game.readyStepsRemaining == kPacManInitialReadyDurationSteps,
                 "initial session enters an explicit Ready presentation");
    game_press_direction(&game, Direction::Left);
    game_update(&game);
    ok &= expect(game.pacman.x == initialX && game.playState == PlayState::InitialReady,
                 "movement input is blocked before initial Ready completes");
    for (uint32_t step = 1u; step < kPacManInitialReadyDurationSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing && !game.readyTextVisible,
                 "initial Ready exits deterministically into Playing");

    game.score = 10000u;
    game.highScore = 10000u;
    game.lifeAward.thresholdsAwardedMask = 1u;
    award_score(game, 2u);
    ok &= expect(game.highScore == 10002u && game.highScoreChanged,
                 "high score updates immediately when score exceeds it");
    game.playState = PlayState::GameOver;
    ok &= expect(game_restart_session(&game) && game.score == 0u &&
                 game.highScore == 10002u,
                 "in-memory high score survives Game Over restart");

    game_initialize(&game);
    game.pacman.direction = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.pacman.facingDirection = Direction::Right;
    game.pacman.speed = 0;
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) game.ghosts[index].active = false;
    ok &= expect(game.powerPillVisible &&
                 game.powerPillBlinkStepsRemaining == kPacManPowerPillBlinkIntervalSteps,
                 "power pills begin in the visible phase");
    for (uint32_t step = 0; step + 1u < kPacManPowerPillBlinkIntervalSteps; ++step) game_update(&game);
    ok &= expect(game.powerPillVisible && game.powerPillBlinkStepsRemaining == 1u,
                 "power-pill blink does not toggle before its exact boundary");
    game_update(&game);
    ok &= expect(!game.powerPillVisible && game.powerPillBlinkStepsRemaining ==
                 kPacManPowerPillBlinkIntervalSteps,
                 "power-pill blink toggles at the fixed-step boundary");
    for (uint32_t step = 0; step < kPacManPowerPillBlinkIntervalSteps; ++step) game_update(&game);
    ok &= expect(game.powerPillVisible, "power-pill blink repeats deterministically");

    game.powerPillVisible = false;
    set_before_target(&game, 1, 23, Direction::Left);
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.powerPillConsumed && game.score == kPacManPowerPillScore &&
                 game.level.powerPillsRemaining == 3u &&
                 game.level.cells[23][1] == CellType::Empty,
                 "hidden power pills remain logical and consumable");
    game.powerPillVisible = true;
    game_update(&game);
    ok &= expect(game.level.cells[23][1] == CellType::Empty,
                 "consumed power pills never reappear in a later visible phase");

    game_initialize(&game);
    keep_only_power_target(&game, 2, 1);
    game.powerPillVisible = false;
    set_before_target(&game, 2, 1, Direction::Right);
    game.pacman.speed = 0;
    game_update(&game);
    ok &= expect(game.powerPillConsumed && game.playState == PlayState::LevelComplete &&
                 game.level.totalConsumablesRemaining == 0u,
                 "hidden final power pill remains consumable and completes the level");

    game.playState = PlayState::GameOver;
    ok &= expect(game_restart_session(&game) && game.powerPillVisible &&
                 game.level.powerPillsRemaining == 4u,
                 "new game resets the shared blink phase and four power pills");
    game_reset_level(&game);
    ok &= expect(game.powerPillVisible && game.level.powerPillsRemaining == 4u,
                 "new level resets the shared blink phase");
    return ok;
}

bool test_ready_input_buffering() {
    bool ok = true;
    GameState game{};
    game_initialize(&game);
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        game.ghosts[index].active = false;
        game.ghosts[index].collisionActive = false;
    }
    game_begin_initial_ready(&game);
    const int initialX = game.pacman.x;
    game_press_direction(&game, Direction::Right);
    game_update(&game);
    ok &= expect(game.playState == PlayState::InitialReady && game.pacman.x == initialX &&
                 game.pacman.direction == Direction::None &&
                 game.pacman.requestedDirection == Direction::Right,
                 "direction pressed during initial Ready is buffered without moving Pac-Man");
    for (uint32_t step = 1u; step < kPacManInitialReadyDurationSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing && game.pacman.direction == Direction::Right &&
                 game.pacman.requestedDirection == Direction::Right,
                 "held Ready direction resumes at the first Playing step");
    game_update(&game);
    ok &= expect(game.pacman.x == initialX + 1,
                 "Pac-Man begins moving after initial Ready when a direction was held");

    game_initialize(&game);
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        game.ghosts[index].active = false;
        game.ghosts[index].collisionActive = false;
    }
    game_begin_initial_ready(&game);
    game_press_direction(&game, Direction::Up);
    game_release_direction(&game, Direction::Up);
    for (uint32_t step = 0; step < kPacManInitialReadyDurationSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing &&
                 game.pacman.requestedDirection == Direction::None &&
                 game.pacman.direction == Direction::None,
                 "a direction released before Ready ends is not treated as held input");

    game_initialize(&game);
    for (uint32_t index = 1; index < kPacManGhostCount; ++index) {
        game.ghosts[index].active = false;
        game.ghosts[index].collisionActive = false;
    }
    game.pacman.direction = Direction::None;
    game.pacman.offset = 0;
    game.ghosts[0].x = game.pacman.x;
    game.ghosts[0].y = game.pacman.y;
    game.ghosts[0].speed = 0;
    game_update(&game);
    ok &= expect(game.playState == PlayState::Dying && game.lifeDecremented,
                 "test enters the ordinary death and respawn path");
    game_press_direction(&game, Direction::Up);
    for (uint32_t step = 0; step < kPacManDeathDurationSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath &&
                 game.pacman.requestedDirection == Direction::Up && game.held.up,
                 "direction held through the death animation survives actor reset");
    game_press_direction(&game, Direction::Right);
    game_release_direction(&game, Direction::Right);
    ok &= expect(game.pacman.requestedDirection == Direction::Up && game.held.up,
                 "releasing a newer Ready direction restores another still-held key");
    for (uint32_t step = 0; step < kPacManReadyAfterDeathSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing &&
                 game.pacman.requestedDirection == Direction::Up,
                 "direction held after losing a life is carried into gameplay");

    game_release_direction(&game, Direction::Up);
    game.playState = PlayState::GameOver;
    game.pacman.requestedDirection = Direction::None;
    game_press_direction(&game, Direction::Up);
    ok &= expect(game.pacman.requestedDirection == Direction::None && !game.held.up,
                 "direction input remains ignored on the Game Over screen");
    ok &= expect(game_restart_session(&game) && game.playState == PlayState::ReadyAfterDeath,
                 "Game Over restart begins a clean Ready interval");
    game_press_direction(&game, Direction::Right);
    for (uint32_t step = 0; step < kPacManReadyAfterDeathSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing &&
                 game.pacman.requestedDirection == Direction::Right,
                 "direction held during restart Ready starts the new session");

    game_initialize(&game);
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        game.ghosts[index].active = false;
        game.ghosts[index].collisionActive = false;
    }
    game.pacman.direction = Direction::None;
    game.pacman.requestedDirection = Direction::None;
    game.playState = PlayState::LevelComplete;
    game.levelCompleteStepsRemaining = 1u;
    game_press_direction(&game, Direction::Down);
    game_update(&game);
    ok &= expect(game.playState == PlayState::ReadyAfterDeath &&
                 game.pacman.requestedDirection == Direction::Down,
                 "held direction survives the level reset into Ready");
    for (uint32_t step = 0; step < kPacManReadyAfterDeathSteps; ++step) game_update(&game);
    ok &= expect(game.playState == PlayState::Playing &&
                 game.pacman.requestedDirection == Direction::Down,
                 "level-transition Ready resumes with the held direction");
    return ok;
}

}

int main() {
    bool ok = true;
    ok &= test_historical_movement();
    ok &= test_pink_target_and_release_state();
    ok &= test_pink_release_and_movement();
    ok &= test_pink_direction_selection_and_tunnels();
    ok &= test_cyan_target_and_release_state();
    ok &= test_cyan_direction_selection_and_tunnels();
    ok &= test_orange_target_and_release_state();
    ok &= test_orange_release_and_movement();
    ok &= test_orange_direction_selection_and_tunnels();
    ok &= test_orange_collisions_and_state_gates();
    ok &= test_orange_reset_lifecycle_and_four_ghost_update();
    ok &= test_pink_collisions_and_state_gates();
    ok &= test_pink_reset_lifecycle_and_stationary_companions();
    ok &= test_counts_and_layout();
    ok &= test_normal_pill_all_directions();
    ok &= test_power_pill_and_duplicate_prevention();
    ok &= test_power_pill_timers_and_reversal();
    ok &= test_frightened_speed_and_collision_order();
    ok &= test_eaten_return_and_lifecycle_reset();
    ok &= test_final_power_pill_and_renderer();
    ok &= test_buffered_turn_and_tunnel_counts();
    ok &= test_completion_reset_and_overflow();
    ok &= test_red_ghost_movement();
    ok &= test_red_direction_selection();
    ok &= test_moving_red_collisions();
    ok &= test_red_state_gates();
    ok &= test_ghost_layout_and_collision();
    ok &= test_cyan_collisions_and_state_gates();
    ok &= test_cyan_reset_lifecycle();
    ok &= test_death_lives_and_reset();
    ok &= test_game_over_and_restart();
    ok &= test_level_rules_and_consecutive_progression();
    ok &= test_fruit_and_extra_life();
    ok &= test_session_hud_and_power_pill_blink();
    ok &= test_ready_input_buffering();
    std::cout << (ok ? "PacMan game logic tests PASS\n" : "PacMan game logic tests FAIL\n");
    return ok ? 0 : 1;
}
