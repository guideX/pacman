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
                 game.ghosts[2].active && game.ghosts[3].active,
                 "all four ghosts are rendered and collision eligible");
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
    game->pacman.x = game->ghosts[index].x;
    game->pacman.y = game->ghosts[index].y;
    game->pacman.offset = 1;
    game->pacman.direction = Direction::None;
    game->pacman.requestedDirection = Direction::None;
    game->pacman.speed = 1;
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
    ok &= test_counts_and_layout();
    ok &= test_normal_pill_all_directions();
    ok &= test_power_pill_and_duplicate_prevention();
    ok &= test_buffered_turn_and_tunnel_counts();
    ok &= test_completion_reset_and_overflow();
    ok &= test_ghost_layout_and_collision();
    ok &= test_death_lives_and_reset();
    ok &= test_game_over_and_restart();
    std::cout << (ok ? "PacMan game logic tests PASS\n" : "PacMan game logic tests FAIL\n");
    return ok ? 0 : 1;
}
