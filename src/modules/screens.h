#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "modules/tiles.h"
#ifdef __cplusplus
}
#endif

namespace pimoroni {
class Badger2040W;
}

void draw_status_bar(pimoroni::Badger2040W &badger, const char *message = nullptr);
void draw_tiles(pimoroni::Badger2040W &badger, const char *selected_name, const char *indicator_icon);
void draw_tile_detail(pimoroni::Badger2040W &badger, TILE *tile);
