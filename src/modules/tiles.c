#include "tiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "badger.h"
#include "images.h"
#include "restful.h"
#include "pico/time.h"

#define TILES_MAX 255

TILES *tile_array;
HEADING **heading_array;
uint8_t heading_count;
uint8_t tile_column;
static const char tiles_data[] = {
    TILES_DATA};

void tiles_init(const char *base_url, char size) {
    tile_array = (TILES *)malloc(sizeof(TILES));
    tile_array->base_url = (char *)malloc(strlen(base_url) + 1 * sizeof(char));
    strcpy(tile_array->base_url, base_url);
    tile_array->tiles = malloc(size * sizeof(TILE *));
    tile_array->used = 0;
    tile_array->size = size;
}

void tiles_free() {
    uint64_t sw_time = to_us_since_boot(get_absolute_time());
    if (!tile_array) {
        return;
    }

    for (int i = 0; i < tile_array->used; i++) {
        free(tile_array->tiles[i]->current_value);
        free(tile_array->tiles[i]->target_value);
        free(tile_array->tiles[i]->battery_value);
        free(tile_array->tiles[i]->boost_status_value);
        free(tile_array->tiles[i]->boost_value);
        if (tile_array->tiles[i]->schedules) {
            for (int s = 0; s < tile_array->tiles[i]->schedule_count; s++) {
                free(tile_array->tiles[i]->schedules[s]);
            }
            free(tile_array->tiles[i]->schedules);
        }
        restful_free_request_data(tile_array->tiles[i]->mode_request);
        restful_free_request_data(tile_array->tiles[i]->status_request);
        restful_free_request_data(tile_array->tiles[i]->battery_request);
        restful_free_request_data(tile_array->tiles[i]->target_request);
        restful_free_request_data(tile_array->tiles[i]->boost_request);
        restful_free_request_data(tile_array->tiles[i]->schedule_request);
        restful_free_request_data(tile_array->tiles[i]->schedule_status_request);
        free(tile_array->tiles[i]->name);
        free(tile_array->tiles[i]);
    }
    free(tile_array->tiles);
    free(tile_array);
    tile_array = NULL;

    if (!heading_array) {
        return;
    }

    for (int i = 0; i < heading_count; i++) {
        free(heading_array[i]->heading);
        free(heading_array[i]);
    }
    free(heading_array);
    heading_array = NULL;
    DEBUG_PRINTF("tiles_free: %ld us\n", (long)(to_us_since_boot(get_absolute_time()) - sw_time));
}

void tiles_add_tile(char *name, uint8_t image_idx, RESTFUL_REQUEST_DATA *mode_request, RESTFUL_REQUEST_DATA *status_request, RESTFUL_REQUEST_DATA *battery_request, RESTFUL_REQUEST_DATA *target_request, RESTFUL_REQUEST_DATA *boost_request, uint8_t type) {
    TILE *tile = (TILE *)malloc(sizeof(TILE));

    tile->name = name;

    tile->image = (char *)image_tiles[image_idx];
    tile->mode_request = mode_request;
    tile->status_request = status_request;
    tile->battery_request = battery_request;
    tile->target_request = target_request;
    tile->boost_request = boost_request;
    tile->schedule_request = NULL;
    tile->schedule_status_request = NULL;
    tile->schedules = NULL;
    tile->schedule_count = 0;
    tile->schedule_status_value = NULL;
    tile->schedule_value = NULL;
    tile->type = type;
    // Use 0xFF to indicate "mode not set" (valid modes are 0-4)
    tile->mode = 0xFF;
    tile->target_temp = 0xFFFF;
    tile->battery_value = NULL;
    tile->boost_status_value = NULL;
    tile->boost_value = NULL;
    tile->current_value = NULL;
    tile->target_value = NULL;

    if (!tile_array) {
        free(tile);
        return;
    }

    if (tile_array->used >= TILES_MAX) {
        DEBUG_PRINTF("Hit TILES_MAX(%d), skipping tile\n", TILES_MAX);
        free(tile);
        return;
    }
    if (tile_array->size < TILES_MAX && tile_array->used == tile_array->size) {
        tile_array->size = (tile_array->size * 2 > TILES_MAX) ? TILES_MAX : tile_array->size * 2;
        tile_array->tiles = realloc(tile_array->tiles, tile_array->size * sizeof(TILE *));
    }

    tile_array->tiles[tile_array->used++] = tile;
}

char tiles_max_column() {
    if (tile_array->used % 3 != 0) {
        return (tile_array->used + 3) / 3;
    } else {
        return tile_array->used / 3;
    }
}

void tiles_previous_column(char count) {
    tile_column = (tile_column + tiles_max_column() - count) % tiles_max_column();
}

void tiles_next_column(char count) {
    tile_column = (tile_column + count) % tiles_max_column();
}

char tiles_get_column() {
    return tile_column;
}

HEADING *tiles_get_heading() {
    return heading_array[tile_column];
}

void tiles_set_column(char column) {
    tile_column = 0;
    if (column < tiles_max_column()) {
        tile_column = column;
    }
}

char tiles_get_base_idx() {
    return (tile_column * 3 > tile_array->used) ? tile_array->used : tile_column * 3;
}

char tiles_idx_in_bounds(char idx) {
    return idx < tile_array->used;
}

char tiles_make_str(char **dest, const char *src) {
    uint8_t str_size = (uint8_t)*src++;
    *dest = (char *)malloc(str_size * sizeof(char) + 1);
    memcpy(*dest, src, str_size);
    (*dest)[str_size] = '\0';  // Null terminate
    return str_size + 1;
}

void tiles_make_tiles() {
    uint64_t sw_time = to_us_since_boot(get_absolute_time());
    tiles_init(API_SERVER, 8);

    RESTFUL_REQUEST_DATA *status_request;
    RESTFUL_REQUEST_DATA *battery_request;
    RESTFUL_REQUEST_DATA *target_request;
    HEADING *heading;
    char *name;
    char *method;
    char *endpoint;
    char *json_body;
    char *mode_method;
    char *mode_endpoint;
    char *mode_json_body;
    char *battery_method;
    char *battery_endpoint;
    char *battery_json_body;
    char *target_method;
    char *target_endpoint;
    char *target_json_body;
    char *boost_method;
    char *boost_endpoint;
    char *boost_json_body;
    char *schedule_method;
    char *schedule_endpoint;
    char *schedule_json_body;
    char *schedule_status_method;
    char *schedule_status_endpoint;
    char *schedule_status_json_body;
    uint8_t schedule_count;
    char **schedules_arr = NULL;
    uint8_t type;
    int ptr = 0;

    heading_count = tiles_data[ptr++];
    heading_array = (HEADING **)malloc(heading_count * sizeof(HEADING *));
    for (uint8_t i = 0; i < heading_count; i++) {
        heading = (HEADING *)malloc(sizeof(HEADING));

        if (tiles_data[ptr] == 0) {
            ptr++;
            // This is a padding tile
            heading->heading = NULL;
            heading->icon = 0;
            heading_array[i] = heading;
            continue;
        }

        ptr += tiles_make_str(&(heading->heading), (char *)&tiles_data[ptr]);
        heading->icon = (char *)image_icons[(uint8_t)tiles_data[ptr++]];
        heading_array[i] = heading;
    }

    char tile_count = tiles_data[ptr++];
    for (char i = 0; i < tile_count; i++) {
        if (tiles_data[ptr] == 0) {
            ptr++;
            // This is a padding tile
            tiles_add_tile(NULL, 0, NULL, NULL, NULL, NULL, NULL, 0);
            continue;
        }
        ptr += tiles_make_str(&name, (char *)&tiles_data[ptr]);

        char image_idx = tiles_data[ptr++];
        type = tiles_data[ptr++];
        ptr += tiles_make_str(&method, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&endpoint, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&json_body, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&mode_method, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&mode_endpoint, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&mode_json_body, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&battery_method, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&battery_endpoint, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&battery_json_body, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&target_method, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&target_endpoint, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&target_json_body, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&boost_method, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&boost_endpoint, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&boost_json_body, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&schedule_method, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&schedule_endpoint, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&schedule_json_body, (char *)&tiles_data[ptr]);
        schedule_count = (uint8_t)tiles_data[ptr++];
        // Initialize per-tile schedules array
        schedules_arr = NULL;
        if (schedule_count > 0) {
            schedules_arr = (char **)malloc(schedule_count * sizeof(char *));
            for (uint8_t s = 0; s < schedule_count; s++) {
                ptr += tiles_make_str(&schedules_arr[s], (char *)&tiles_data[ptr]);
            }
        }
        ptr += tiles_make_str(&schedule_status_method, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&schedule_status_endpoint, (char *)&tiles_data[ptr]);
        ptr += tiles_make_str(&schedule_status_json_body, (char *)&tiles_data[ptr]);

        status_request = restful_make_request_data(method, endpoint, json_body);
        RESTFUL_REQUEST_DATA *mode_request = restful_make_request_data(mode_method, mode_endpoint, mode_json_body);
        battery_request = restful_make_request_data(battery_method, battery_endpoint, battery_json_body);
        target_request = restful_make_request_data(target_method, target_endpoint, target_json_body);
        RESTFUL_REQUEST_DATA *boost_request = restful_make_request_data(boost_method, boost_endpoint, boost_json_body);
        RESTFUL_REQUEST_DATA *schedule_request = restful_make_request_data(schedule_method, schedule_endpoint, schedule_json_body);
        RESTFUL_REQUEST_DATA *schedule_status_request = restful_make_request_data(schedule_status_method, schedule_status_endpoint, schedule_status_json_body);

        tiles_add_tile(name, image_idx, mode_request, status_request, battery_request, target_request, boost_request, type);
        // Attach schedule_request, schedule_status_request and schedules to the last added tile
        TILE *last_tile = tile_array->tiles[tile_array->used - 1];
        last_tile->schedule_request = schedule_request;
        last_tile->schedule_status_request = schedule_status_request;
        last_tile->schedules = schedules_arr;
        last_tile->schedule_count = schedule_count;
    }
    DEBUG_PRINTF("tiles_make_tiles: %ld us\n", (long)(to_us_since_boot(get_absolute_time()) - sw_time));
}