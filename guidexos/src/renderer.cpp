#include "renderer.h"

#include "game.h"
#include "level.h"
#include "level_rules.h"

#ifndef PACMAN_HOSTED_FRUIT_TEST
#define PACMAN_HOSTED_FRUIT_TEST 0
#endif
#ifndef PACMAN_BAREMETAL_FRUIT_VALIDATION
#define PACMAN_BAREMETAL_FRUIT_VALIDATION 0
#endif

namespace {

static uint32_t* pixel(uint32_t* frame, int x, int y) {
    if (!frame || x < 0 || x >= kPacManWidth || y < 0 || y >= kPacManFrameHeight) return 0;
    return frame + y * kPacManWidth + x;
}

static void fill(uint32_t* frame, uint32_t color) {
    if (!frame) return;
    for (uint32_t i = 0; i < static_cast<uint32_t>(kPacManWidth * kPacManFrameHeight); ++i) frame[i] = color;
}

static void copy_pixels(const uint32_t* source, uint32_t* destination) {
    if (!source || !destination) return;
    for (uint32_t i = 0; i < static_cast<uint32_t>(kPacManWidth * kPacManFrameHeight); ++i) destination[i] = source[i];
}

static void copy_level(const PacImage* level, uint32_t* frame) {
    for (uint32_t y = 0; y < kPacManMazeHeight; ++y) {
        const uint32_t* source = level->pixels + y * (level->strideBytes / 4u);
        uint32_t* destination = frame + (y + 32u) * kPacManWidth;
        for (uint32_t x = 0; x < kPacManWidth; ++x) destination[x] = source[x];
    }
}

static void pill(uint32_t* frame, int centerX, int centerY, int radius, uint32_t color) {
    if (!frame || radius < 0) return;
    int firstX = -radius;
    int lastX = radius + 1;
    int firstY = -radius;
    int lastY = radius + 1;
    if (firstX < -centerX) firstX = -centerX;
    if (lastX > kPacManWidth - centerX) lastX = kPacManWidth - centerX;
    if (firstY < -(32 + centerY)) firstY = -(32 + centerY);
    if (lastY > kPacManFrameHeight - (32 + centerY)) {
        lastY = kPacManFrameHeight - (32 + centerY);
    }
    if (firstX >= lastX || firstY >= lastY) return;
    const int radiusSquared = radius * radius;
    for (int y = firstY; y < lastY; ++y) {
        uint32_t* row = frame + (32 + centerY + y) * kPacManWidth + centerX + firstX;
        for (int x = firstX; x < lastX; ++x) {
            if (x * x + y * y <= radiusSquared) {
                row[x - firstX] = color;
            }
        }
    }
}

static void draw_pills(uint32_t* frame, const GameState& game) {
    for (int y = 0; y < kPacManMazeRows; ++y) {
        for (int x = 0; x < kPacManMazeColumns; ++x) {
            const CellType cell = game.level.cells[y][x];
            if (cell == CellType::Pill) {
                pill(frame, x * kPacManTileSize + 8, y * kPacManTileSize + 8, 2, 0x00FFC8A0u);
            } else if (cell == CellType::PowerPill && game.powerPillVisible) {
                pill(frame, x * kPacManTileSize + 8, y * kPacManTileSize + 8, 6, 0x00FFFFFFu);
            }
        }
    }
}

static void draw_sprite(const PacImage* sprites, uint32_t* frame, int destinationX, int destinationY,
                        int sourceX, int sourceY, int maskX);

static void draw_fruit(const PacImage* sprites, uint32_t* frame, const GameState& game) {
    if (!sprites || game.fruit.phase != FruitPhase::Visible ||
        game.playState == PlayState::GameOver) return;
    const FruitRules rules = calculate_fruit_rules(game.levelNumber);
    draw_sprite(sprites, frame, game.fruit.x - 16, game.fruit.y - 16,
                rules.sprite.x, rules.sprite.y, rules.mask.x);
}

static void draw_sprite(const PacImage* sprites, uint32_t* frame, int destinationX, int destinationY,
                        int sourceX, int sourceY, int maskX) {
    if (!sprites || !sprites->pixels || sprites->width > 0xFFFFFFFFu / 4u ||
        sprites->strideBytes < sprites->width * 4u) return;

    // Clip the sprite and both source/mask rectangles once. The old inner
    // loop called pixel() for every one of the 1,024 sprite pixels, even when
    // the sprite was fully inside the frame.
    int firstX = 0;
    int lastX = kPacManSpriteSize;
    const int destinationTop = 32 + destinationY;
    int firstY = 0;
    int lastY = kPacManSpriteSize;
    if (firstX < -destinationX) firstX = -destinationX;
    if (firstX < -sourceX) firstX = -sourceX;
    if (firstX < -maskX) firstX = -maskX;
    if (lastX > kPacManWidth - destinationX) lastX = kPacManWidth - destinationX;
    if (lastX > static_cast<int>(sprites->width) - sourceX) {
        lastX = static_cast<int>(sprites->width) - sourceX;
    }
    if (lastX > static_cast<int>(sprites->width) - maskX) {
        lastX = static_cast<int>(sprites->width) - maskX;
    }
    if (firstY < -destinationTop) firstY = -destinationTop;
    if (firstY < -sourceY) firstY = -sourceY;
    if (lastY > kPacManFrameHeight - destinationTop) {
        lastY = kPacManFrameHeight - destinationTop;
    }
    if (lastY > static_cast<int>(sprites->height) - sourceY) {
        lastY = static_cast<int>(sprites->height) - sourceY;
    }
    if (firstX >= lastX || firstY >= lastY) return;

    const uint32_t sourceStride = sprites->strideBytes / 4u;
    for (int y = firstY; y < lastY; ++y) {
        const int sourceRow = (sourceY + y) * static_cast<int>(sourceStride);
        const int maskRow = (sourceY + y) * static_cast<int>(sourceStride);
        const int destinationRow = (destinationTop + y) * kPacManWidth;
        const uint32_t* sourcePixels = sprites->pixels + sourceRow + sourceX + firstX;
        const uint32_t* maskPixels = sprites->pixels + maskRow + maskX + firstX;
        uint32_t* destinationPixels = frame + destinationRow + destinationX + firstX;
        for (int x = firstX; x < lastX; ++x) {
            const int column = x - firstX;
            destinationPixels[column] = (destinationPixels[column] & maskPixels[column]) |
                                        sourcePixels[column];
        }
    }
}

static const char* glyph(char c) {
    switch (c) {
    case '0': return "01110 10001 10011 10101 11001 10001 01110";
    case '1': return "00100 01100 00100 00100 00100 00100 01110";
    case '2': return "01110 10001 00001 00010 00100 01000 11111";
    case '3': return "11110 00001 00001 01110 00001 00001 11110";
    case '4': return "00010 00110 01010 10010 11111 00010 00010";
    case '5': return "11111 10000 10000 11110 00001 10001 01110";
    case '6': return "00110 01000 10000 11110 10001 10001 01110";
    case '7': return "11111 00001 00010 00100 01000 01000 01000";
    case '8': return "01110 10001 10001 01110 10001 10001 01110";
    case '9': return "01110 10001 10001 01111 00001 00010 11100";
    case ' ': return "00000 00000 00000 00000 00000 00000 00000";
    case ':': return "00000 00100 00100 00000 00100 00100 00000";
    case '-': return "00000 00000 00000 11111 00000 00000 00000";
    case 'A': return "01110 10001 10001 11111 10001 10001 10001";
    case 'C': return "01110 10001 10000 10000 10000 10001 01110";
    case 'D': return "11110 10001 10001 10001 10001 10001 11110";
    case 'E': return "11111 10000 10000 11110 10000 10000 11111";
    case 'F': return "11111 10000 10000 11110 10000 10000 10000";
    case 'G': return "01110 10001 10000 10111 10001 10001 01110";
    case 'H': return "10001 10001 10001 11111 10001 10001 10001";
    case 'I': return "11111 00100 00100 00100 00100 00100 11111";
    case 'L': return "10000 10000 10000 10000 10000 10000 11111";
    case 'M': return "10001 11011 10101 10101 10001 10001 10001";
    case 'N': return "10001 11001 10101 10011 10001 10001 10001";
    case 'O': return "01110 10001 10001 10001 10001 10001 01110";
    case 'P': return "11110 10001 10001 11110 10000 10000 10000";
    case 'R': return "11110 10001 10001 11110 10100 10010 10001";
    case 'S': return "01111 10000 10000 01110 00001 00001 11110";
    case 'T': return "11111 00100 00100 00100 00100 00100 00100";
    case 'V': return "10001 10001 10001 10001 01010 01010 00100";
    case 'W': return "10001 10001 10001 10101 10101 11011 10001";
    case 'X': return "10001 10001 01010 00100 01010 10001 10001";
    case 'Y': return "10001 10001 01010 00100 00100 00100 00100";
    default: return "00000 00000 00000 00000 00000 00000 00000";
    }
}

static void draw_text(uint32_t* frame, int x, int y, const char* text, uint32_t color, int scale) {
    for (const char* current = text; current && *current; ++current) {
        const char* pattern = glyph(*current);
        int row = 0;
        for (const char* bit = pattern; *bit; ++bit) {
            if (*bit == ' ') continue;
            if (*bit == '0' || *bit == '1') {
                if (*bit == '1') {
                    const int col = (bit - pattern) % 6;
                    for (int sy = 0; sy < scale; ++sy) for (int sx = 0; sx < scale; ++sx) {
                        uint32_t* target = pixel(frame, x + col * scale + sx, y + row * scale + sy);
                        if (target) *target = color;
                    }
                }
                if ((bit - pattern) % 6 == 4) ++row;
            }
        }
        x += 6 * scale;
    }
}

static bool valid_frame(uint32_t* framePixels, uint32_t framePixelCount) {
    const uint32_t requiredPixels = static_cast<uint32_t>(kPacManWidth * kPacManFrameHeight);
    return framePixels && framePixelCount >= requiredPixels;
}

static void draw_number(uint32_t* frame, int x, int y, uint32_t value, uint32_t color, int scale) {
    char digits[10];
    int length = 0;
    do {
        digits[length++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && length < static_cast<int>(sizeof(digits)));
    for (int i = length - 1; i >= 0; --i) {
        char glyphText[2] = {digits[i], '\0'};
        draw_text(frame, x, y, glyphText, color, scale);
        x += 6 * scale;
    }
}

static int number_digit_count(uint32_t value) {
    int length = 1;
    while (value >= 10u) {
        value /= 10u;
        ++length;
    }
    return length;
}

static void draw_number_right(uint32_t* frame, int right, int y, uint32_t value,
                              uint32_t color, int scale) {
    draw_number(frame, right - number_digit_count(value) * 6 * scale, y, value, color, scale);
}

static void draw_fruit_score_popup(uint32_t* frame, const GameState& game) {
    if (game.fruit.phase != FruitPhase::ScorePopup || game.fruit.popupStepsRemaining == 0u ||
        game.playState == PlayState::GameOver) return;
    const int width = number_digit_count(game.fruit.scoreValue) * 6;
    draw_number(frame, game.fruit.x - width / 2, 32 + game.fruit.y - 24,
                game.fruit.scoreValue, 0x00FFFF00u, 1);
}

static void draw_status(uint32_t* frame, const GameState& game) {
    const uint32_t color = 0x00FFFFFFu;
    draw_text(frame, 4, 2, "SCORE", color, 1);
    draw_number(frame, 96, 2, game.score, color, 1);
    draw_number_right(frame, 329, 2, game.highScore, color, 1);
    draw_text(frame, 332, 2, "HI SCORE", color, 1);
}

static void draw_bottom_hud(const PacImage* sprites, uint32_t* frame, const GameState& game) {
    if (!sprites) return;
    const uint32_t spareLives = game.lives > 0u ? game.lives - 1u : 0u;
    const uint32_t visibleLives = spareLives > 14u ? 14u : spareLives;
    for (uint32_t index = 0; index < visibleLives; ++index) {
        draw_sprite(sprites, frame, static_cast<int>(index * 32u), 488, 96, 224, 224);
    }

    const uint32_t historyCount = game.levelNumber > kPacManHistoricalMaximumLevel
        ? kPacManHistoricalMaximumLevel : game.levelNumber;
    for (uint32_t index = 0; index < historyCount; ++index) {
        const FruitRules rules = calculate_fruit_rules(index + 1u);
        draw_sprite(sprites, frame, 416 - static_cast<int>(index * 32u), 488,
                    rules.sprite.x, rules.sprite.y, rules.mask.x);
    }
}

static int safe_direction_index(Direction direction) {
    const int value = static_cast<int>(direction);
    return value >= 0 && value < 4 ? value : static_cast<int>(Direction::Right);
}

static void draw_ghosts(const PacImage* sprites, uint32_t* frame, const GameState& game) {
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        const GhostState& ghost = game.ghosts[index];
        if (!ghost.active) continue;
        int sourceX = static_cast<int>(ghost.kind) * kPacManSpriteSize;
        int maskX = 192;
        if (ghost.condition == GhostCondition::Frightened &&
            ghost.powerPillStepsRemaining > 0) {
            const bool flashing = ghost.powerPillStepsRemaining < game_frightened_flash_threshold(game) &&
                game.frightenedFlashPhase > 7u;
            if (!flashing) sourceX = 128;
        } else if (ghost.condition == GhostCondition::Eaten ||
                   ghost.condition == GhostCondition::Returning) {
            sourceX = 160;
            maskX = 224;
        }
        const int sourceY = safe_direction_index(ghost.direction) * kPacManSpriteSize;
        // Historical ShowSprites presents Pac-Man first and then ghosts. The
        // native scene keeps that ordering so a ghost owns an overlap pixel.
        draw_sprite(sprites, frame, ghost.x - 16, ghost.y - 16,
                    sourceX, sourceY, maskX);
    }
}

#if PACMAN_HOSTED_DANGER_TEST || PACMAN_HOSTED_FRUIT_TEST || PACMAN_BAREMETAL_FRUIT_VALIDATION
static char validation_condition_code(GhostCondition condition) {
    switch (condition) {
    case GhostCondition::Normal: return 'N';
    case GhostCondition::Frightened: return 'F';
    case GhostCondition::Eaten: return 'E';
    case GhostCondition::Returning: return 'R';
    }
    return '?';
}

static void draw_validation_power_pill_indicator(uint32_t* frame, const GameState& game) {
    // Validation-only and deliberately confined to the two-line status strip.
    // It is absent from the production ELF and contains no gameplay controls.
    draw_text(frame, 8, 544, "PP R:", 0x0000FFFFu, 1);
    draw_number(frame, 38, 544, game.ghosts[0].powerPillStepsRemaining, 0x0000FFFFu, 1);
    char code[2] = {validation_condition_code(game.ghosts[0].condition), '\0'};
    draw_text(frame, 68, 544, code, 0x0000FFFFu, 1);
    draw_text(frame, 80, 544, "P:", 0x0000FFFFu, 1);
    draw_number(frame, 92, 544, game.ghosts[1].powerPillStepsRemaining, 0x0000FFFFu, 1);
    code[0] = validation_condition_code(game.ghosts[1].condition);
    draw_text(frame, 122, 544, code, 0x0000FFFFu, 1);
    draw_text(frame, 134, 544, "C:", 0x0000FFFFu, 1);
    draw_number(frame, 146, 544, game.ghosts[2].powerPillStepsRemaining, 0x0000FFFFu, 1);
    code[0] = validation_condition_code(game.ghosts[2].condition);
    draw_text(frame, 176, 544, code, 0x0000FFFFu, 1);
    draw_text(frame, 188, 544, "O:", 0x0000FFFFu, 1);
    draw_number(frame, 200, 544, game.ghosts[3].powerPillStepsRemaining, 0x0000FFFFu, 1);
    code[0] = validation_condition_code(game.ghosts[3].condition);
    draw_text(frame, 230, 544, code, 0x0000FFFFu, 1);
    draw_text(frame, 242, 544, "CH:", 0x0000FFFFu, 1);
    draw_number(frame, 266, 544, game.ghostEatChain, 0x0000FFFFu, 1);
}

static void draw_validation_level_rules(uint32_t* frame, const GameState& game) {
    // Validation-only compact rule readout.  It is confined to the existing
    // bottom strip and is absent from production builds.
    const uint32_t color = 0x0000FFFFu;
    const LevelRules rules = calculate_level_rules(game.levelNumber);
    const uint32_t speed = sanitize_game_speed(game.gameSpeed);
    draw_text(frame, 280, 544, "LVL:", color, 1);
    draw_number(frame, 304, 544, rules.level, color, 1);
    draw_text(frame, 316, 544, "PM:", color, 1);
    draw_number(frame, 334, 544, rules.pacmanMovePixelsPerStep * speed, color, 1);
    draw_text(frame, 346, 544, "G:", color, 1);
    draw_number(frame, 358, 544, rules.normalGhostMovePixelsPerStep * speed, color, 1);
    draw_text(frame, 370, 544, "FG:", color, 1);
    draw_number(frame, 388, 544, rules.frightenedGhostMoveIntervalSteps, color, 1);
    draw_text(frame, 400, 544, "PP:", color, 1);
    draw_number(frame, 418, 544, game_frightened_duration_steps(game), color, 1);
}

static char validation_fruit_phase_code(FruitPhase phase) {
    switch (phase) {
    case FruitPhase::Inactive: return 'I';
    case FruitPhase::Visible: return 'V';
    case FruitPhase::ScorePopup: return 'P';
    }
    return '?';
}

static void draw_validation_fruit_indicator(uint32_t* frame, const GameState& game) {
    const uint32_t color = 0x0000FFFFu;
    draw_text(frame, 8, 536, "FR:", color, 1);
    char phase[2] = {validation_fruit_phase_code(game.fruit.phase), '\0'};
    draw_text(frame, 26, 536, phase, color, 1);
    draw_text(frame, 38, 536, "T:", color, 1);
    draw_number(frame, 50, 536, game.fruit.visibleStepsRemaining, color, 1);
    draw_text(frame, 86, 536, "A:", color, 1);
    draw_number(frame, 98, 536, game.fruit.appearancesTriggered, color, 1);
    draw_text(frame, 116, 536, "TY:", color, 1);
    draw_number(frame, 140, 536, game.fruit.fruitType, color, 1);
    draw_text(frame, 158, 536, "LA:", color, 1);
    draw_number(frame, 182, 536, game.lifeAward.awardsGranted, color, 1);
}
#endif

}

bool build_background_frame(const PacImage* level, uint32_t* backgroundPixels, uint32_t framePixelCount) {
    if (!level || !level->pixels || !valid_frame(backgroundPixels, framePixelCount) ||
        level->width != kPacManWidth || level->height < kPacManMazeHeight ||
        level->width > 0xFFFFFFFFu / 4u || level->strideBytes < level->width * 4u) return false;

    fill(backgroundPixels, 0x00000000u);
    copy_level(level, backgroundPixels);
    return true;
}

bool render_game_scene(const PacImage* sprites, const GameState* game, const uint32_t* backgroundPixels,
                       uint32_t* framePixels, uint32_t framePixelCount, uint64_t validationFrameSequence) {
    const uint32_t requiredPixels = static_cast<uint32_t>(kPacManWidth * kPacManFrameHeight);
    if (!sprites || !sprites->pixels || !game || !backgroundPixels || !valid_frame(framePixels, framePixelCount) ||
        sprites->width < 256 || sprites->height < 352 || sprites->width > 0xFFFFFFFFu / 4u ||
        sprites->strideBytes < sprites->width * 4u) return false;

    copy_pixels(backgroundPixels, framePixels);
    draw_pills(framePixels, *game);
    draw_status(framePixels, *game);
    draw_bottom_hud(sprites, framePixels, *game);
    draw_fruit(sprites, framePixels, *game);
    int mouthFrame = game->pacman.mouth;
    if (mouthFrame < 1 || mouthFrame > 3) mouthFrame = 3;
    const int sourceX = static_cast<int>(game->pacman.facingDirection) * kPacManSpriteSize;
    const int sourceY = 128 + mouthFrame * kPacManSpriteSize;
    if (game->pacman.facingDirection != Direction::None) {
        draw_sprite(sprites, framePixels, game->pacman.x - 16, game->pacman.y - 16,
                    sourceX, sourceY, sourceX + 128);
    }
    draw_ghosts(sprites, framePixels, *game);
    draw_fruit_score_popup(framePixels, *game);
    if (game->playState == PlayState::Dying) {
        const int deathFrame = static_cast<int>(game->deathAnimationFrame / 4u) % 3;
        const int deathSourceY = 128 + (deathFrame + 1) * kPacManSpriteSize;
        draw_sprite(sprites, framePixels, game->pacman.x - 16, game->pacman.y - 16,
                    sourceX, deathSourceY, sourceX + 128);
        draw_text(framePixels, 190, 270, "DEATH", 0x00FF4040u, 2);
    } else if ((game->playState == PlayState::InitialReady ||
                game->playState == PlayState::ReadyAfterDeath) && game->readyTextVisible) {
        draw_text(framePixels, 200, 270, "READY", 0x00FFFF00u, 2);
    } else if (game->playState == PlayState::LevelComplete) {
        draw_text(framePixels, 140, 270, "LEVEL COMPLETE", 0x00FFFF00u, 2);
    } else if (game->playState == PlayState::GameOver) {
        draw_text(framePixels, 170, 270, "GAME OVER", 0x00FF4040u, 2);
    }
#if PACMAN_HOSTED_DANGER_TEST || PACMAN_HOSTED_FRUIT_TEST || PACMAN_BAREMETAL_FRUIT_VALIDATION
    // Test-only identity marker. It is drawn into the same XRGB8888 frame as
    // the game and is intentionally kept in the otherwise unused right side
    // of the status strip. Production builds do not compile this path.
    char markerState[2] = {'P', '\0'};
    if (game->playState == PlayState::Dying) markerState[0] = 'D';
    else if (game->playState == PlayState::ReadyAfterDeath) markerState[0] = 'R';
    else if (game->playState == PlayState::GameOver) markerState[0] = 'G';
    draw_text(framePixels, 394, 10, markerState, 0x0000FFFFu, 1);
    draw_number(framePixels, 406, 10, static_cast<uint32_t>(validationFrameSequence), 0x0000FFFFu, 1);
    draw_validation_power_pill_indicator(framePixels, *game);
    draw_validation_level_rules(framePixels, *game);
    draw_validation_fruit_indicator(framePixels, *game);
#else
    (void)validationFrameSequence;
#endif
    (void)requiredPixels;
    return true;
}

bool render_static_scene(const PacImage* level, const PacImage* sprites, uint32_t* framePixels, uint32_t framePixelCount) {
    if (!level || !sprites || !valid_frame(framePixels, framePixelCount)) return false;
    static uint32_t backgroundPixels[kPacManWidth * kPacManFrameHeight];
    GameState game{};
    game_initialize(&game);
    return build_background_frame(level, backgroundPixels, framePixelCount) &&
        render_game_scene(sprites, &game, backgroundPixels, framePixels, framePixelCount);
}
