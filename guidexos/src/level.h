#pragma once

#include "game_state.h"

static const int kPacManTunnelRow = 14;

char level_tile(int column, int row);
bool is_walkable_tile(int column, int row);
bool is_tunnel_row(int row);
int level_column_from_position(int x);
int level_row_from_position(int y);
bool can_move(const GameState& game, int x, int y, Direction direction);
