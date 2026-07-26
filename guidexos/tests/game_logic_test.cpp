#include "game.h"
#include "level.h"

#include <iostream>

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
    ok &= expect(game.score == 1244 && game.level.totalConsumablesRemaining == 0,
                 "last normal pill updates score and reaches zero");
    for (uint32_t i = 0; i < 99; ++i) game_update(&game);
    ok &= expect(game.playState == PlayState::LevelComplete && game.levelResetCount == 0,
                 "completion delay keeps Pac-Man stopped");
    game_update(&game);
    ok &= expect(game.playState == PlayState::Playing && game.levelResetCount == 1,
                 "completion resets after one second");
    ok &= expect(game.score == 1244, "level reset preserves score");
    ok &= expect(game.levelNumber == 2, "level reset increments level");
    ok &= expect(game.level.normalPillsRemaining == 240 && game.level.powerPillsRemaining == 4 &&
                 game.level.totalConsumablesRemaining == 244,
                 "level reset restores exact consumable counts");
    ok &= expect(game.pacman.x == 224 && game.pacman.y == 376 &&
                  game.pacman.direction == Direction::Right && game.pacman.offset == 8 &&
                  game.pacman.requestedDirection == Direction::Right,
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
    ok &= expect(!game.held.left && !game.held.right && !game.held.up && !game.held.down &&
                 game.pacman.direction == Direction::None &&
                 game.pacman.requestedDirection == Direction::None,
                 "death clears directional input");

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
                 game.pacman.direction == Direction::Right && game.pacman.offset == 8,
                 "Pac-Man reset restores historical start state");
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
    std::cout << (ok ? "PacMan game logic tests PASS\n" : "PacMan game logic tests FAIL\n");
    return ok ? 0 : 1;
}
