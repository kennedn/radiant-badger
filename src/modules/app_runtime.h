#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "modules/tiles.h"

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum APP_SCREEN {
    APP_SCREEN_TILES = 0,
    APP_SCREEN_DETAIL = 1,
    APP_SCREEN_BOOST = 2,
    APP_SCREEN_SCHEDULE = 3,
} APP_SCREEN;

void app_runtime_reset(void);
bool app_is_initialised(void);
void app_set_initialised(bool value);

APP_SCREEN app_get_current_screen(void);
void app_set_current_screen(APP_SCREEN screen);
TILE *app_get_active_tile(void);
void app_set_active_tile(TILE *tile);
bool app_is_refresh_pending(void);

void app_render_current_screen(const char *message, bool sleeping);
void app_refresh_visible_tiles(bool visible_refresh);
void app_refresh_current_screen(void);
void app_restore_tiles_screen(bool visible_refresh);
void app_refresh_tile_detail(TILE *tile);

void app_request_boiler_target_toggle(TILE *tile);
void app_request_tile_boost_edit(TILE *tile);
void app_request_tile_boost_submit(TILE *tile);
void app_request_tile_schedule_edit(TILE *tile);
void app_request_tile_schedule_submit(TILE *tile);

void app_restful_callback(char *key, char *value, int status_code, void *arg);

bool app_handle_tiles_screen(int click_count, int bootsel_refresh);
bool app_handle_detail_screen(int click_count);
bool app_handle_boost_screen(int click_count);
bool app_handle_schedule_screen(int click_count);

#ifdef __cplusplus
}
#endif