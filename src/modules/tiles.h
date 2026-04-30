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
    RESTFUL_REQUEST_DATA *mode_request;
    RESTFUL_REQUEST_DATA *status_request;
    uint8_t type;
    uint8_t mode;
    uint8_t has_mode;
    char *display_value;
} TILE;

enum TILE_TYPE {
    TILE_TYPE_BOILER = 0,
    TILE_TYPE_RADIATOR = 1,
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