#pragma once

#include <stdint.h>

#include "restful.h"

typedef struct HEADING_ {
    char *heading;
    char *icon;
} HEADING;

typedef struct TILE_ {
    char *name;
    char *image;
    RESTFUL_REQUEST_DATA *action_request;
    RESTFUL_REQUEST_DATA *status_request;
    RESTFUL_REQUEST_DATA *battery_request;
    RESTFUL_REQUEST_DATA *target_request;
    RESTFUL_REQUEST_DATA *boost_request;
    RESTFUL_REQUEST_DATA *schedule_request;
    RESTFUL_REQUEST_DATA *schedule_status_request;
    // RESTFUL tile response extraction
    char *status_key;
    char *status_on_value;
    char *status_off_value;
    uint8_t type;
    uint16_t target_temp;
    char *battery_value;
    char *boost_status_value;
    char *boost_value;
    char *status_value;
    // Schedule support for boiler tiles
    char **schedules;
    uint8_t schedule_count;
    char *schedule_status_value;
    char *schedule_value;
    char *current_value;
    char *target_value;
    int http_status_code;
} TILE;

enum TILE_TYPE {
    TILE_TYPE_BOILER = 0,
    TILE_TYPE_RADIATOR = 1,
    TILE_TYPE_RESTFUL = 2,
};

typedef struct TILES_ {
    char *base_url;
    TILE **tiles;
    int used;
    int size;
} TILES;

extern TILES *tile_array;
extern HEADING **heading_array;

void tiles_init(const char *base_url, char size);
void tiles_free();
char tiles_max_column();
void tiles_previous_column(char count);
void tiles_next_column(char count);
char tiles_get_column();
void tiles_set_column(char column);
HEADING *tiles_get_heading();
char tiles_get_base_idx();
char tiles_idx_in_bounds(char idx);
void tiles_make_tiles();