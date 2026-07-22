#include "level.h"

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

static int floor_divide_by_tile(int value) {
    if (value >= 0) return value / kPacManTileSize;
    return -(((-value) + kPacManTileSize - 1) / kPacManTileSize);
}

}

char level_tile(int column, int row) {
    if (column < 0 || column >= kPacManMazeColumns || row < 0 || row >= kPacManMazeRows) return 'W';
    return kMaze[row][column];
}

bool is_walkable_tile(int column, int row) {
    return level_tile(column, row) != 'W';
}

bool is_tunnel_row(int row) {
    return row == kPacManTunnelRow;
}

int level_column_from_position(int x) {
    return floor_divide_by_tile(x - 8);
}

int level_row_from_position(int y) {
    return floor_divide_by_tile(y - 8);
}

bool can_move(const GameState& game, int x, int y, Direction direction) {
    (void)game;
    int column = level_column_from_position(x);
    int row = level_row_from_position(y);
    if (row < 0 || row >= kPacManMazeRows || column < 0 || column >= kPacManMazeColumns ||
        !is_walkable_tile(column, row)) return false;

    int nextColumn = column;
    int nextRow = row;
    switch (direction) {
    case Direction::Up: --nextRow; break;
    case Direction::Down: ++nextRow; break;
    case Direction::Left: --nextColumn; break;
    case Direction::Right: ++nextColumn; break;
    case Direction::None: return false;
    }

    if (nextColumn >= 0 && nextColumn < kPacManMazeColumns &&
        nextRow >= 0 && nextRow < kPacManMazeRows) {
        return is_walkable_tile(nextColumn, nextRow);
    }

    return is_tunnel_row(row) && nextRow == row &&
        (direction == Direction::Left || direction == Direction::Right);
}
