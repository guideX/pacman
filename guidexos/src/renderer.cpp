#include "renderer.h"

namespace {

static const char* kMaze[kPacManMazeRows] = {
    "WWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    "W............WW............W",
    "W.WWWW.WWWWW.WW.WWWWW.WWWW.W",
    "WoWWWW.WWWWW.WW.WWWWW.WWWWoW",
    "W.WWWW.WWWWW.WW.WWWWW.WWWW.W",
    "W..........................W",
    "W.WWWW.WW.WWWWWWWW.WW.WWWW.W",
    "W.WWWW.WW.WWWWWWWW.WW.WWWW.W",
    "W......WW....WW....WW......W",
    "WWWWWW.WWWWW WW WWWWW.WWWWWW",
    "     W.WWWWW WW WWWWW.W     ",
    "     W.WW          WW.W     ",
    "     W.WW WWWWWWWW WW.W     ",
    "WWWWWW.WW W      W WW.WWWWWW",
    "      .   W      W   .      ",
    "WWWWWW.WW W      W WW.WWWWWW",
    "     W.WW WWWWWWWW WW.W     ",
    "     W.WW          WW.W     ",
    "     W.WW WWWWWWWW WW.W     ",
    "WWWWWW.WW WWWWWWWW WW.WWWWWW",
    "W............WW............W",
    "W.WWWW.WWWWW.WW.WWWWW.WWWW.W",
    "W.WWWW.WWWWW.WW.WWWWW.WWWW.W",
    "Wo..WW.......  .......WW..oW",
    "WWW.WW.WW.WWWWWWWW.WW.WW.WWW",
    "WWW.WW.WW.WWWWWWWW.WW.WW.WWW",
    "W......WW....WW....WW......W",
    "W.WWWWWWWWWW.WW.WWWWWWWWWW.W",
    "W.WWWWWWWWWW.WW.WWWWWWWWWW.W",
    "W..........................W",
    "WWWWWWWWWWWWWWWWWWWWWWWWWWWW"
};

static uint32_t* pixel(uint32_t* frame, int x, int y) {
    if (x < 0 || x >= kPacManWidth || y < 0 || y >= kPacManFrameHeight) return 0;
    return frame + y * kPacManWidth + x;
}

static void fill(uint32_t* frame, uint32_t color) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(kPacManWidth * kPacManFrameHeight); ++i) frame[i] = color;
}

static void copy_level(const PacImage* level, uint32_t* frame) {
    for (uint32_t y = 0; y < level->height && y < kPacManMazeHeight; ++y) {
        const uint32_t* source = level->pixels + y * (level->strideBytes / 4u);
        uint32_t* destination = frame + (y + 32u) * kPacManWidth;
        for (uint32_t x = 0; x < level->width && x < kPacManWidth; ++x) destination[x] = source[x];
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

static void draw_pills(uint32_t* frame) {
    for (int y = 1; y <= 29; ++y) {
        for (int x = 1; x <= 26; ++x) {
            const char tile = kMaze[y][x];
            if (tile == '.') pill(frame, x * 16 + 8, y * 16 + 8, 2, 0x00FFC8A0u);
            if (tile == 'o') pill(frame, x * 16 + 8, y * 16 + 8, 6, 0x00FFFFFFu);
        }
    }
}

static void draw_sprite(const PacImage* sprites, uint32_t* frame, int destinationX, int destinationY, int sourceX, int sourceY, int maskX) {
    for (int y = 0; y < kPacManSpriteSize; ++y) {
        for (int x = 0; x < kPacManSpriteSize; ++x) {
            const int sx = sourceX + x;
            const int sy = sourceY + y;
            const int mx = maskX + x;
            if (sx < 0 || sy < 0 || mx < 0 || sx >= static_cast<int>(sprites->width) || mx >= static_cast<int>(sprites->width) || sy >= static_cast<int>(sprites->height)) continue;
            const uint32_t source = sprites->pixels[sy * (sprites->strideBytes / 4u) + sx];
            const uint32_t mask = sprites->pixels[sy * (sprites->strideBytes / 4u) + mx];
            uint32_t* target = pixel(frame, destinationX + x, 32 + destinationY + y);
            if (target) *target = (*target & mask) | source;
        }
    }
}

static const char* glyph(char c) {
    switch (c) {
    case '0': return "01110 10001 10011 10101 11001 10001 01110";
    case '1': return "00100 01100 00100 00100 00100 00100 01110";
    case '5': return "11111 10000 10000 11110 00001 10001 01110";
    case ' ': return "00000 00000 00000 00000 00000 00000 00000";
    case ':': return "00000 00100 00100 00000 00100 00100 00000";
    case '-': return "00000 00000 00000 11111 00000 00000 00000";
    case 'A': return "01110 10001 10001 11111 10001 10001 10001";
    case 'C': return "01110 10001 10000 10000 10000 10001 01110";
    case 'E': return "11111 10000 10000 11110 10000 10000 11111";
    case 'G': return "01110 10001 10000 10111 10001 10001 01110";
    case 'H': return "10001 10001 10001 11111 10001 10001 10001";
    case 'I': return "11111 00100 00100 00100 00100 00100 11111";
    case 'M': return "10001 11011 10101 10101 10001 10001 10001";
    case 'N': return "10001 11001 10101 10011 10001 10001 10001";
    case 'P': return "11110 10001 10001 11110 10000 10000 10000";
    case 'S': return "01111 10000 10000 01110 00001 00001 11110";
    case 'T': return "11111 00100 00100 00100 00100 00100 00100";
    case 'X': return "10001 10001 01010 00100 01010 10001 10001";
    case 'V': return "10001 10001 10001 10001 01010 01010 00100";
    case 'K': return "10001 10010 10100 11000 10100 10010 10001";
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

}

bool render_static_scene(const PacImage* level, const PacImage* sprites, uint32_t* framePixels, uint32_t framePixelCount) {
    if (!level || !sprites || !framePixels || framePixelCount < static_cast<uint32_t>(kPacManWidth * kPacManFrameHeight) ||
        level->width != kPacManWidth || level->height < kPacManMazeHeight || sprites->width < 256 || sprites->height < 352) return false;

    fill(framePixels, 0x00000000u);
    copy_level(level, framePixels);
    draw_pills(framePixels);

    draw_sprite(sprites, framePixels, 208, 360, 96, 224, 224);
    draw_sprite(sprites, framePixels, 208, 168, 0, 0, 192);
    draw_sprite(sprites, framePixels, 176, 208, 32, 0, 192);
    draw_sprite(sprites, framePixels, 208, 224, 64, 0, 192);
    draw_sprite(sprites, framePixels, 240, 208, 96, 0, 192);

    draw_text(framePixels, 10, 10, "SCORE: 0", 0x00FFFFFFu, 1);
    draw_text(framePixels, 290, 10, "HI SCORE: 10000", 0x00FFFFFFu, 1);
    draw_text(framePixels, 62, 536, "NEXGEN PACMAN - STATIC PREVIEW - ESC TO EXIT", 0x00FFFFFFu, 1);
    return true;
}
