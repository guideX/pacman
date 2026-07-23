#pragma once

#include "game_types.h"

struct GameState;
enum class Direction : int8_t;

static const int kPacManTunnelRow = 14;

char level_tile(int column, int row);
void level_initialize(LevelState* level);
CellType level_cell(const LevelState& level, int column, int row);
bool is_walkable_cell(CellType cell);
bool is_consumable_cell(CellType cell);

enum class ConsumptionResult : uint8_t {
    None,
    NormalPill,
    PowerPill,
    CountUnderflow
};

ConsumptionResult level_consume(LevelState* level, int column, int row);
bool is_walkable_tile(int column, int row);
bool is_tunnel_row(int row);
int level_column_from_position(int x);
int level_row_from_position(int y);
bool can_move(const GameState& game, int x, int y, Direction direction);
