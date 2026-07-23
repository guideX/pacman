#include "level.h"

#include "game_state.h"

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

static bool is_ghost_house_cell(int column, int row) {
    return row >= 13 && row <= 15 && column >= 11 && column <= 16;
}

static bool is_tunnel_cell(int column, int row) {
    return row == kPacManTunnelRow && (column <= 5 || column >= 22);
}

}

char level_tile(int column, int row) {
    if (column < 0 || column >= kPacManMazeColumns || row < 0 || row >= kPacManMazeRows) return 'W';
    return kMaze[row][column];
}

void level_initialize(LevelState* level) {
    if (!level) return;
    level->normalPillsRemaining = 0;
    level->powerPillsRemaining = 0;
    level->totalConsumablesRemaining = 0;
    for (int row = 0; row < kPacManMazeRows; ++row) {
        for (int column = 0; column < kPacManMazeColumns; ++column) {
            const char tile = level_tile(column, row);
            CellType cell = CellType::Empty;
            if (tile == 'W') {
                cell = CellType::Wall;
            } else if (tile == '.') {
                cell = CellType::Pill;
                ++level->normalPillsRemaining;
                ++level->totalConsumablesRemaining;
            } else if (tile == 'o') {
                cell = CellType::PowerPill;
                ++level->powerPillsRemaining;
                ++level->totalConsumablesRemaining;
            } else if (is_ghost_house_cell(column, row)) {
                cell = CellType::GhostHouse;
            } else if (is_tunnel_cell(column, row)) {
                cell = CellType::Tunnel;
            }
            level->cells[row][column] = cell;
        }
    }
}

CellType level_cell(const LevelState& level, int column, int row) {
    if (column < 0 || column >= kPacManMazeColumns || row < 0 || row >= kPacManMazeRows) {
        return CellType::Wall;
    }
    return level.cells[row][column];
}

bool is_walkable_cell(CellType cell) {
    return cell != CellType::Wall;
}

bool is_consumable_cell(CellType cell) {
    return cell == CellType::Pill || cell == CellType::PowerPill;
}

ConsumptionResult level_consume(LevelState* level, int column, int row) {
    if (!level || column < 0 || column >= kPacManMazeColumns || row < 0 || row >= kPacManMazeRows) {
        return ConsumptionResult::None;
    }
    const CellType cell = level->cells[row][column];
    if (cell == CellType::Pill) {
        if (level->normalPillsRemaining == 0 || level->totalConsumablesRemaining == 0) {
            return ConsumptionResult::CountUnderflow;
        }
        level->cells[row][column] = CellType::Empty;
        --level->normalPillsRemaining;
        --level->totalConsumablesRemaining;
        return ConsumptionResult::NormalPill;
    }
    if (cell == CellType::PowerPill) {
        if (level->powerPillsRemaining == 0 || level->totalConsumablesRemaining == 0) {
            return ConsumptionResult::CountUnderflow;
        }
        level->cells[row][column] = CellType::Empty;
        --level->powerPillsRemaining;
        --level->totalConsumablesRemaining;
        return ConsumptionResult::PowerPill;
    }
    return ConsumptionResult::None;
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
    int column = level_column_from_position(x);
    int row = level_row_from_position(y);
    if (row < 0 || row >= kPacManMazeRows || column < 0 || column >= kPacManMazeColumns ||
        !is_walkable_cell(level_cell(game.level, column, row))) return false;

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
        return is_walkable_cell(level_cell(game.level, nextColumn, nextRow));
    }

    return is_tunnel_row(row) && nextRow == row &&
        (direction == Direction::Left || direction == Direction::Right);
}
