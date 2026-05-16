#include "tile_state.h"

#include <stdlib.h>
#include <string.h>

void tile_clear_string_field(char **field) {
    if (!field) {
        return;
    }

    free(*field);
    *field = NULL;
}

void tile_set_string_field(char **field, const char *value) {
    if (!field) {
        return;
    }

    tile_clear_string_field(field);
    if (!value) {
        return;
    }

    *field = (char *)malloc(strlen(value) + 1);
    if (*field) {
        strcpy(*field, value);
    }
}

void tile_set_boost_value(TILE *tile, int value) {
    if (!tile) {
        return;
    }

    if (value < 0) {
        value = 0;
    } else if (value > 23) {
        value = 23;
    }

    char value_str[8];
    snprintf(value_str, sizeof(value_str), "%d", value);
    tile_set_string_field(&tile->boost_value, value_str);
}

int tile_get_boost_value(TILE *tile) {
    if (!tile || !tile->boost_value || !tile->boost_value[0]) {
        return 0;
    }

    int value = atoi(tile->boost_value);
    if (value < 0) {
        return 0;
    }
    if (value > 23) {
        return 23;
    }
    return value;
}

void tile_set_schedule_index(TILE *tile, int index) {
    if (!tile || tile->schedule_count == 0) {
        return;
    }

    index %= tile->schedule_count;
    if (index < 0) {
        index += tile->schedule_count;
    }

    char value_str[16];
    snprintf(value_str, sizeof(value_str), "%d", index);
    tile_set_string_field(&tile->schedule_value, value_str);
}

int tile_get_schedule_index(TILE *tile) {
    if (!tile || !tile->schedule_value || !tile->schedule_value[0]) {
        return 0;
    }

    int value = atoi(tile->schedule_value);
    if (value < 0) {
        return 0;
    }
    if (tile->schedule_count == 0) {
        return 0;
    }
    if (value >= tile->schedule_count) {
        return tile->schedule_count - 1;
    }
    return value;
}

int tile_get_schedule_index_from_value(TILE *tile, const char *value) {
    if (!tile || !value || !value[0] || !tile->schedules || tile->schedule_count == 0) {
        return 0;
    }

    for (uint8_t i = 0; i < tile->schedule_count; i++) {
        if (tile->schedules[i] && strcmp(tile->schedules[i], value) == 0) {
            return i;
        }
    }

    return 0;
}