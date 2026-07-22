#pragma once

#include "game_state.h"

void game_initialize(GameState* game);
Direction game_direction_for_key(int keyCode);
void game_press_direction(GameState* game, Direction direction);
void game_release_direction(GameState* game, Direction direction);
void game_focus_lost(GameState* game);
void game_focus_gained(GameState* game);
void game_update(GameState* game);
const char* game_direction_name(Direction direction);
