#include "renderer.h"

#include "game.h"
#include "level.h"

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
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                uint32_t* target = pixel(frame, centerX + x, 32 + centerY + y);
                if (target) *target = color;
            }
        }
    }
}

static void draw_pills(uint32_t* frame, const LevelState& level) {
    for (int y = 0; y < kPacManMazeRows; ++y) {
        for (int x = 0; x < kPacManMazeColumns; ++x) {
            const CellType cell = level.cells[y][x];
            if (cell == CellType::Pill) {
                pill(frame, x * kPacManTileSize + 8, y * kPacManTileSize + 8, 2, 0x00FFC8A0u);
            } else if (cell == CellType::PowerPill) {
                pill(frame, x * kPacManTileSize + 8, y * kPacManTileSize + 8, 6, 0x00FFFFFFu);
            }
        }
    }
}

static void draw_sprite(const PacImage* sprites, uint32_t* frame, int destinationX, int destinationY,
                        int sourceX, int sourceY, int maskX) {
    if (!sprites || !sprites->pixels || sprites->width > 0xFFFFFFFFu / 4u ||
        sprites->strideBytes < sprites->width * 4u) return;
    const uint32_t sourceStride = sprites->strideBytes / 4u;
    for (int y = 0; y < kPacManSpriteSize; ++y) {
        for (int x = 0; x < kPacManSpriteSize; ++x) {
            const int sx = sourceX + x;
            const int sy = sourceY + y;
            const int mx = maskX + x;
            if (sx < 0 || sy < 0 || mx < 0 || sx >= static_cast<int>(sprites->width) ||
                mx >= static_cast<int>(sprites->width) || sy >= static_cast<int>(sprites->height)) continue;
            const uint32_t source = sprites->pixels[sy * sourceStride + sx];
            const uint32_t mask = sprites->pixels[sy * sourceStride + mx];
            uint32_t* target = pixel(frame, destinationX + x, 32 + destinationY + y);
            if (target) *target = (*target & mask) | source;
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

static void draw_status(uint32_t* frame, const GameState& game) {
    const uint32_t color = 0x00FFFFFFu;
    draw_text(frame, 10, 10, "SCORE:", color, 1);
    draw_number(frame, 52, 10, game.score, color, 1);
    draw_text(frame, 190, 10, "LIVES:", color, 1);
    draw_number(frame, 232, 10, game.lives, color, 1);
    draw_text(frame, 300, 10, "LEVEL:", color, 1);
    draw_number(frame, 342, 10, game.levelNumber, color, 1);
}

static int safe_direction_index(Direction direction) {
    const int value = static_cast<int>(direction);
    return value >= 0 && value < 4 ? value : static_cast<int>(Direction::Right);
}

static void draw_ghosts(const PacImage* sprites, uint32_t* frame, const GameState& game) {
    for (uint32_t index = 0; index < kPacManGhostCount; ++index) {
        const GhostState& ghost = game.ghosts[index];
        if (!ghost.active) continue;
        const int sourceX = static_cast<int>(ghost.kind) * kPacManSpriteSize;
        const int sourceY = safe_direction_index(ghost.direction) * kPacManSpriteSize;
        // Historical ShowSprites presents Pac-Man first and then ghosts. The
        // native scene keeps that ordering so a ghost owns an overlap pixel.
        draw_sprite(sprites, frame, ghost.x - 16, ghost.y - 16,
                    sourceX, sourceY, 192);
    }
}

}

bool build_background_frame(const PacImage* level, uint32_t* backgroundPixels, uint32_t framePixelCount) {
    if (!level || !level->pixels || !valid_frame(backgroundPixels, framePixelCount) ||
        level->width != kPacManWidth || level->height < kPacManMazeHeight ||
        level->width > 0xFFFFFFFFu / 4u || level->strideBytes < level->width * 4u) return false;

    fill(backgroundPixels, 0x00000000u);
    copy_level(level, backgroundPixels);
    draw_text(backgroundPixels, 80, 536, "ARROWS MOVE - ESC TO EXIT", 0x00FFFFFFu, 1);
    return true;
}

bool render_game_scene(const PacImage* sprites, const GameState* game, const uint32_t* backgroundPixels,
                       uint32_t* framePixels, uint32_t framePixelCount) {
    const uint32_t requiredPixels = static_cast<uint32_t>(kPacManWidth * kPacManFrameHeight);
    if (!sprites || !sprites->pixels || !game || !backgroundPixels || !valid_frame(framePixels, framePixelCount) ||
        sprites->width < 256 || sprites->height < 352 || sprites->width > 0xFFFFFFFFu / 4u ||
        sprites->strideBytes < sprites->width * 4u) return false;

    copy_pixels(backgroundPixels, framePixels);
    draw_pills(framePixels, game->level);
    draw_status(framePixels, *game);
    int mouthFrame = game->pacman.mouth;
    if (mouthFrame < 1 || mouthFrame > 3) mouthFrame = 3;
    const int sourceX = static_cast<int>(game->pacman.facingDirection) * kPacManSpriteSize;
    const int sourceY = 128 + mouthFrame * kPacManSpriteSize;
    if (game->pacman.facingDirection != Direction::None) {
        draw_sprite(sprites, framePixels, game->pacman.x - 16, game->pacman.y - 16,
                    sourceX, sourceY, sourceX + 128);
    }
    draw_ghosts(sprites, framePixels, *game);
    if (game->playState == PlayState::Dying) {
        const int deathFrame = static_cast<int>(game->deathAnimationFrame / 4u) % 3;
        const int deathSourceY = 128 + (deathFrame + 1) * kPacManSpriteSize;
        draw_sprite(sprites, framePixels, game->pacman.x - 16, game->pacman.y - 16,
                    sourceX, deathSourceY, sourceX + 128);
        draw_text(framePixels, 190, 270, "DEATH", 0x00FF4040u, 2);
    } else if (game->playState == PlayState::ReadyAfterDeath) {
        draw_text(framePixels, 200, 270, "READY", 0x00FFFF00u, 2);
    } else if (game->playState == PlayState::LevelComplete) {
        draw_text(framePixels, 140, 270, "LEVEL COMPLETE", 0x00FFFF00u, 2);
    } else if (game->playState == PlayState::GameOver) {
        draw_text(framePixels, 170, 270, "GAME OVER", 0x00FF4040u, 2);
    }
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
