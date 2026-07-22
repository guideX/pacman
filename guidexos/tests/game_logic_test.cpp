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

}

int main() {
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
    std::cout << (ok ? "PacMan game logic tests PASS\n" : "PacMan game logic tests FAIL\n");
    return ok ? 0 : 1;
}
