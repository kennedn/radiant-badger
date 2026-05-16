#include "modules/app_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "modules/http.h"
#include "modules/power.h"
#include "modules/restful.h"
#include "modules/tile_state.h"
#include "modules/tiles.h"
}

#include "badger.h"
#include "modules/images.h"
#include "modules/screens.h"
#include "libraries/badger2040w/badger2040w.hpp"

extern pimoroni::Badger2040W badger;
void wifi_wait(void);
bool wifi_up(void);
void deinit(const char *message);

static volatile bool visible_refresh_active = false;
static volatile uint8_t visible_refresh_pending = 0;
static volatile uint16_t visible_refresh_generation = 0;
static volatile bool initialised = false;
static APP_SCREEN current_screen = APP_SCREEN_TILES;
static TILE *active_tile = NULL;

static void refresh_tile_status(TILE *tile);
static void clear_stage_fields(RESTFUL_REQUEST *request);
static void finalize_restful_request(RESTFUL_REQUEST *request);

void app_runtime_reset(void) {
    current_screen = APP_SCREEN_TILES;
    active_tile = NULL;
    visible_refresh_active = false;
    visible_refresh_pending = 0;
    visible_refresh_generation = 0;
}

bool app_is_initialised(void) { return initialised; }
void app_set_initialised(bool value) { initialised = value; }
APP_SCREEN app_get_current_screen(void) { return current_screen; }
void app_set_current_screen(APP_SCREEN screen) { current_screen = screen; }
TILE *app_get_active_tile(void) { return active_tile; }
void app_set_active_tile(TILE *tile) { active_tile = tile; }
bool app_is_refresh_pending(void) { return visible_refresh_pending > 0; }

void app_render_current_screen(const char *message, bool sleeping) {
    badger.graphics->set_pen(15);
    badger.graphics->clear();

    if (current_screen == APP_SCREEN_DETAIL) {
        draw_tile_detail(badger, active_tile);
    } else if (current_screen == APP_SCREEN_BOOST) {
        draw_tile_boost(badger, active_tile);
    } else if (current_screen == APP_SCREEN_SCHEDULE) {
        draw_tile_schedule(badger, active_tile);
    } else {
        draw_tiles(badger, NULL, NULL);
    }

    draw_status_bar(badger, message, sleeping);
    badger.update();
    badger.uc8151->busy_wait();
}

static void refresh_tile_status(TILE *tile) {
    if (!tile || !tile->status_request) {
        return;
    }

    RESTFUL_REQUEST *request = restful_make_request(
        tile,
        tile_array->base_url,
        NULL,
        tile->status_request,
        NULL,
        -1,
        app_restful_callback);
    if (!request) {
        return;
    }

    request->visible_refresh_batch = true;
    request->visible_refresh_generation = visible_refresh_generation;
    restful_request(request);
}

void app_refresh_visible_tiles(bool visible_refresh) {
    visible_refresh_active = visible_refresh;
    visible_refresh_pending = 0;
    visible_refresh_generation++;

    char tiles_base_idx = tiles_get_base_idx();
    for (char i = 0; i < 3; i++) {
        if (!tiles_idx_in_bounds(tiles_base_idx + i)) {
            break;
        }
        TILE *tile = tile_array->tiles[tiles_base_idx + i];
        if (!tile || !tile->status_request) {
            continue;
        }
        visible_refresh_pending++;
        refresh_tile_status(tile);
    }

    // Action-only columns have no status refresh callbacks, so render them immediately.
    if (visible_refresh_pending == 0 && current_screen == APP_SCREEN_TILES) {
        visible_refresh_active = false;
        app_render_current_screen(NULL, false);
    }
}

void app_refresh_current_screen(void) {
    if (!wifi_up()) {
        wifi_wait();
    }
    if (current_screen == APP_SCREEN_TILES) {
        app_refresh_visible_tiles(true);
    } else if (current_screen == APP_SCREEN_DETAIL && active_tile) {
        app_refresh_tile_detail(active_tile);
    }
}

void app_restore_tiles_screen(bool visible_refresh) {
    current_screen = APP_SCREEN_TILES;
    active_tile = NULL;
    badger.graphics->set_pen(15);
    badger.graphics->clear();

    if (!wifi_up()) {
        wifi_wait();
    }
    app_refresh_visible_tiles(visible_refresh);
}

void app_request_boiler_target_toggle(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_BOILER || !tile->target_request) {
        return;
    }

    uint16_t next_target = (tile->target_temp == 50) ? 350 : 50;
    restful_request(restful_make_request(
        tile,
        tile_array->base_url,
        tile->target_request,
        tile->schedule_status_request,
        tile->status_request,
        next_target,
        app_restful_callback));
}

void app_request_tile_boost_edit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_RADIATOR || !tile->boost_request) {
        return;
    }

    tile_set_boost_value(tile, 0);
    current_screen = APP_SCREEN_BOOST;
    app_render_current_screen(NULL, false);
}

void app_request_tile_schedule_edit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_BOILER || !tile->schedule_request) {
        return;
    }

    int schedule_index = tile_get_schedule_index_from_value(tile, tile->schedule_status_value);
    tile_set_schedule_index(tile, schedule_index);
    current_screen = APP_SCREEN_SCHEDULE;
    app_render_current_screen(NULL, false);
}

void app_request_tile_boost_submit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_RADIATOR || !tile->boost_request) {
        return;
    }

    int boost_value = tile_get_boost_value(tile);
    restful_request(restful_make_request(
        tile,
        tile_array->base_url,
        tile->boost_request,
        tile->battery_request,
        tile->status_request,
        boost_value,
        app_restful_callback));
}

void app_request_tile_schedule_submit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_BOILER || !tile->schedule_request) {
        return;
    }

    int idx = tile_get_schedule_index(tile);
    const char *selected = "";
    if (tile->schedules && tile->schedule_count > 0 && idx >= 0 && idx < tile->schedule_count) {
        selected = tile->schedules[idx];
    }

    char resolved[128];
    if (tile->schedule_request && tile->schedule_request->json_body) {
        snprintf(resolved, sizeof(resolved), tile->schedule_request->json_body, selected);
    } else {
        resolved[0] = '\0';
    }

    char *method = NULL;
    char *endpoint = NULL;
    char *body = NULL;
    if (tile->schedule_request && tile->schedule_request->method) {
        method = (char *)malloc(strlen(tile->schedule_request->method) + 1);
        strcpy(method, tile->schedule_request->method);
    }
    if (tile->schedule_request && tile->schedule_request->endpoint) {
        endpoint = (char *)malloc(strlen(tile->schedule_request->endpoint) + 1);
        strcpy(endpoint, tile->schedule_request->endpoint);
    }
    body = (char *)malloc(strlen(resolved) + 1);
    strcpy(body, resolved);

    RESTFUL_REQUEST_DATA *tmp = restful_make_request_data(method, endpoint, body);
    RESTFUL_REQUEST *req = restful_make_request(tile, tile_array->base_url, tmp, tile->schedule_status_request, tile->status_request, -1, app_restful_callback);
    if (req) {
        restful_request(req);
    } else {
        restful_free_request_data(tmp);
    }
}

void app_refresh_tile_detail(TILE *tile) {
    if (!tile) {
        return;
    }

    if (tile->type == TILE_TYPE_RESTFUL) {
        restful_request(restful_make_request(
            tile,
            tile_array->base_url,
            NULL,
            tile->status_request,
            NULL,
            -1,
            app_restful_callback));
        return;
    }

    if (tile->type == TILE_TYPE_BOILER) {
        restful_request(restful_make_request(
            tile,
            tile_array->base_url,
            NULL,
            tile->schedule_status_request,
            tile->status_request,
            -1,
            app_restful_callback));
        return;
    }

    if (tile->type == TILE_TYPE_RADIATOR) {
        restful_request(restful_make_request(
            tile,
            tile_array->base_url,
            NULL,
            tile->battery_request,
            tile->status_request,
            -1,
            app_restful_callback));
    }
}

static void clear_stage_fields(RESTFUL_REQUEST *request) {
    if (!request || !request->tile || !request->active_request || request->keys_count == 0) {
        return;
    }

    TILE *tile = (TILE *)request->tile;
    for (size_t i = 0; i < request->keys_count; i++) {
        const char *key = request->active_request->keys[i];
        if (!key) {
            continue;
        }

        if (tile->type == TILE_TYPE_RESTFUL && tile->status_key && strcmp(key, tile->status_key) == 0) {
            tile_clear_string_field(&tile->status_value);
        } else if (strcmp(key, "current") == 0) {
            tile_clear_string_field(&tile->current_value);
        } else if (strcmp(key, "target") == 0) {
            tile_clear_string_field(&tile->target_value);
        } else if (strcmp(key, "boost") == 0) {
            tile_clear_string_field(&tile->boost_status_value);
        } else if (strcmp(key, "value") == 0) {
            tile_clear_string_field(&tile->battery_value);
        } else if (strcmp(key, "schedule") == 0) {
            tile_clear_string_field(&tile->schedule_status_value);
        }
    }
}

static void finalize_restful_request(RESTFUL_REQUEST *request) {
    if (!request || !request->tile) {
        restful_free_request(request);
        return;
    }

    if (request->visible_refresh_batch && request->visible_refresh_generation != visible_refresh_generation) {
        restful_free_request(request);
        return;
    }

    TILE *tile = (TILE *)request->tile;

    if ((current_screen == APP_SCREEN_BOOST || current_screen == APP_SCREEN_SCHEDULE) && active_tile == tile) {
        current_screen = APP_SCREEN_DETAIL;
    }

    if (visible_refresh_pending > 0) {
        visible_refresh_pending--;
        DEBUG_PRINTF("restful_callback: visible_refresh_pending=%d\n", visible_refresh_pending);
    }

    if (visible_refresh_pending == 0 && visible_refresh_active && current_screen == APP_SCREEN_TILES) {
        visible_refresh_active = false;
        app_render_current_screen(NULL, false);
    } else if (current_screen == APP_SCREEN_DETAIL && active_tile == tile) {
        app_render_current_screen(NULL, false);
    } else if (current_screen == APP_SCREEN_BOOST && active_tile == tile) {
        app_render_current_screen(NULL, false);
    } else if (current_screen == APP_SCREEN_SCHEDULE && active_tile == tile) {
        app_render_current_screen(NULL, false);
    } else if (tile->type == TILE_TYPE_RESTFUL && current_screen == APP_SCREEN_TILES && active_tile == tile) {
        app_render_current_screen(NULL, false);
    }

    restful_free_request(request);
}

void app_restful_callback(char *key, char *value, int status_code, void *arg) {
    if (arg == NULL) {
        return;
    }

    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    TILE *tile = (TILE *)request->tile;
    if (!request || !tile) {
        restful_free_request(request);
        return;
    }

    tile->http_status_code = status_code;

    if (request->keys_count > 0 && request->keys_remaining == request->keys_count) {
        clear_stage_fields(request);
    }

    if (status_code == 200 && key && value) {
        if (tile->type == TILE_TYPE_RESTFUL && tile->status_key && strcmp(key, tile->status_key) == 0) {
            tile_set_string_field(&tile->status_value, value);
        } else if (strcmp(key, "current") == 0) {
            char buffer[32];
            float current = strtof(value, NULL) / 10.0f;
            snprintf(buffer, sizeof(buffer), "%.1f", current);
            tile_set_string_field(&tile->current_value, buffer);
        } else if (strcmp(key, "target") == 0) {
            char buffer[32];
            float target = strtof(value, NULL) / 10.0f;
            snprintf(buffer, sizeof(buffer), "%.1f", target);
            tile_set_string_field(&tile->target_value, buffer);
            tile->target_temp = (uint16_t)atoi(value);
        } else if (strcmp(key, "boost") == 0) {
            tile_set_string_field(&tile->boost_status_value, value);
        } else if (strcmp(key, "value") == 0) {
            tile_set_string_field(&tile->battery_value, value);
        } else if (strcmp(key, "schedule") == 0) {
            tile_set_string_field(&tile->schedule_status_value, value);
        }
    }

    if (request->keys_count == 0) {
        if (restful_request_continue(request)) {
            return;
        }
        finalize_restful_request(request);
        return;
    }

    if (request->keys_remaining > 0) {
        request->keys_remaining--;
    }

    if (request->keys_remaining == 0) {
        if (restful_request_continue(request)) {
            return;
        }
        finalize_restful_request(request);
    }
}

bool app_handle_tiles_screen(int click_count, int bootsel_refresh) {
    (void)click_count;
    (void)bootsel_refresh;
    return false;
}

bool app_handle_detail_screen(int click_count) { (void)click_count; return false; }
bool app_handle_boost_screen(int click_count) { (void)click_count; return false; }
bool app_handle_schedule_screen(int click_count) { (void)click_count; return false; }