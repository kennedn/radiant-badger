#pragma once

#include "tiles.h"

#ifdef __cplusplus
extern "C" {
#endif

void tile_clear_string_field(char **field);
void tile_set_string_field(char **field, const char *value);

void tile_set_boost_value(TILE *tile, int value);
int tile_get_boost_value(TILE *tile);

void tile_set_schedule_index(TILE *tile, int index);
int tile_get_schedule_index(TILE *tile);
int tile_get_schedule_index_from_value(TILE *tile, const char *value);

#ifdef __cplusplus
}
#endif