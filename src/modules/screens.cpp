#include "modules/screens.h"

#include <stdio.h>
#include <string.h>

#include "badger.h"

extern "C" {
#include "modules/images.h"
#include "modules/power.h"
}

#include "pico/util/datetime.h"
#include "pico/stdlib.h"
#include "libraries/badger2040w/badger2040w.hpp"

using namespace pimoroni;

#define WIDTH 296
#define HEIGHT 128

static void draw_header(pimoroni::Badger2040W &badger, const char *title, bool show_heading) {
    datetime_t datetime;
    float voltage;
    char powerPercent = 0;
    char percentStr[5] = {0};
    char clock_str[24] = {0};
    char status_x_offset = 0;
    char x_pad = 4;
    int32_t clock_x_offset;
    uint8_t *wifi_image = (uint8_t *)image_status_wifi_on;
    uint8_t *battery_image = (uint8_t *)image_status_battery_charging;
    uint8_t *heading_icon = NULL;

    if (!power_is_charging()) {
        battery_image = (uint8_t *)image_status_battery_discharging;
        status_x_offset = 22;

        power_voltage(&voltage);
        powerPercent = power_percent(&voltage);
        sprintf(percentStr, "%d%%", powerPercent);
    }

    if (title) {
        snprintf(clock_str, sizeof(clock_str), "%s", title);
    } else {
        if(!wifi_up()) {
            wifi_image = (uint8_t *)image_status_wifi_off;
        }
        rtc_get_datetime(&datetime);
        snprintf(clock_str, sizeof(clock_str), "%02d:%02d\n", datetime.hour, datetime.min);
        if (show_heading) {
            heading_icon = (uint8_t *)tiles_get_heading()->icon;
        }
    }
    clock_x_offset = badger.graphics->measure_text(clock_str, 2.0f) / 2;

    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(0, 0, WIDTH, 20));

    badger.graphics->set_pen(15);
    badger.graphics->text(clock_str, Point(WIDTH / 2 - clock_x_offset, 3), WIDTH, 2.0f);

    if (heading_icon) {
        badger.graphics->text(tiles_get_heading()->heading, Point(image_status_size + x_pad * 2, 2), WIDTH, 2.0f);
        badger.image(heading_icon, Rect(x_pad, 2, image_status_size, image_status_size));
    } else if (!title) {
        badger.graphics->text("restfulBadger", Point(x_pad, 6), WIDTH, 1.0f);
    }
    badger.image(battery_image, Rect(WIDTH - status_x_offset - (image_status_size + x_pad), 2, image_status_size, image_status_size));
    badger.image(wifi_image, Rect(WIDTH - status_x_offset - (image_status_size + x_pad) * 2, 2, image_status_size, image_status_size));
    if (!power_is_charging()) {
        badger.graphics->set_pen(15);
        badger.graphics->text(percentStr, Point(WIDTH - status_x_offset, 6), WIDTH, 1.0);
        badger.graphics->rectangle(Rect(WIDTH - status_x_offset - (image_status_size + x_pad) + 2, 7, powerPercent / 10 + 1, 6));
    }
}

void draw_status_bar(pimoroni::Badger2040W &badger, const char *message) {
    draw_header(badger, message, true);
}

void draw_tiles(pimoroni::Badger2040W &badger, const char *selected_name, const char *indicator_icon) {
    char tiles_base_idx = tiles_get_base_idx();
    char tile_pad_x = WIDTH / 3;
    char tile_pad_y = 25;
    char tile_offset = 18;
    char column_pad_y = 10;
    char indicator_offset = 44;
    float scale = 2.0f;
    for(char i=0; i< 3; i++) {
        if (!tiles_idx_in_bounds(tiles_base_idx + i)) {
            break;
        }
        TILE *tile = tile_array->tiles[tiles_base_idx + i];
        if (!tile->image || !tile->name) {
            continue;
        }

        // Image
        int32_t name_y_offset = badger.graphics->bitmap_font->height * scale * 0.75f;
        badger.image((const uint8_t *)tile->image, Rect(tile_offset + (tile_pad_x*i), tile_pad_y + name_y_offset, image_tile_size, image_tile_size));

        // Header
        badger.graphics->set_pen(0);
        badger.graphics->set_font("bitmap8");
        int32_t name_size = badger.graphics->measure_text(tile->name, scale);
        int32_t name_x_offset = (tile_pad_x - name_size) /2 ;
        badger.graphics->text(tile->name, Point((tile_pad_x*i) + name_x_offset, tile_pad_y), tile_pad_x, scale);
        badger.graphics->set_font("bitmap8");

        // Footer
        if (tile->display_value && tile->display_value[0]) {
            name_size = badger.graphics->measure_text(tile->display_value, scale);
            name_x_offset = (tile_pad_x - name_size) /2 ;
            badger.graphics->text(tile->display_value, Point((tile_pad_x*i) + name_x_offset, tile_pad_y + name_y_offset + image_tile_size), tile_pad_x, scale);
        }
        if(selected_name && !strcmp(selected_name, tile->name)) {
            badger.image((const uint8_t *)indicator_icon,
                Rect(tile_offset + (tile_pad_x*i) + indicator_offset, tile_pad_y + indicator_offset, image_indicator_size, image_indicator_size));
        }
    }

    DEBUG_PRINTF("current column: %d, max: %d\n", tiles_get_column(), tiles_max_column());
    char current_col = tiles_get_column() % 10;
    char last_10s_col = tiles_max_column() - tiles_max_column() % 10;
    char max_col = (tiles_get_column() < last_10s_col) ? 10 : tiles_max_column() % 10;
    for (char i=0; i < max_col; i++) {
        badger.graphics->set_pen(0);
        char y = column_pad_y + (HEIGHT / 2) - (max_col * 10 / 2) + (i * 10);
        badger.graphics->rectangle(Rect(WIDTH - 10, y, 20, 8));
        if (current_col != i) {
            badger.graphics->set_pen(15);
            badger.graphics->rectangle(Rect(WIDTH - 10 + 1, y + 1, 18, 6));
        }
    }
}

void draw_tile_detail(pimoroni::Badger2040W &badger, TILE *tile) {
    if (!tile) {
        badger.graphics->set_pen(0);
        badger.graphics->text("No tile selected", Point(20, 40), WIDTH, 2.0f);
        return;
    }

    // Use the same positioning grid as draw_tiles for consistency
    char tile_pad_x = WIDTH / 3;      // 98px per column
    char tile_pad_y = 25;              // vertical position for content
    char tile_offset = 18;             // x offset from column start
    char text_offset = 20;             // y offset for text below icon
    char label_width = tile_pad_x;
    float scale = 2.0f;

    // Tile A: icon position (same as tiles view)
    int32_t name_y_offset = badger.graphics->bitmap_font->height * scale * 0.75f;
    if (tile->image) {
        badger.image((const uint8_t *)tile->image, Rect(tile_offset, tile_pad_y + name_y_offset, image_tile_size, image_tile_size));
    }

    // Header
    badger.graphics->set_pen(0);
    badger.graphics->set_font("bitmap8");
    int32_t name_size = badger.graphics->measure_text(tile->name, scale);
    int32_t name_x_offset = (tile_pad_x - name_size) /2 ;
    badger.graphics->text(tile->name, Point(name_x_offset, tile_pad_y), tile_pad_x, scale);
    badger.graphics->set_font("bitmap8");

    char line[48];
    
    // Tile B: Status label and value (column 2)
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "STATUS");
    badger.graphics->text(line, Point(tile_pad_x + tile_offset, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
    if (tile->display_value && tile->display_value[0]) {
        snprintf(line, sizeof(line), "%s", tile->display_value);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x + tile_offset, tile_pad_y + text_offset), label_width, scale);

    // Tile C: Mode label and value (column 3)
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "MODE");
    badger.graphics->text(line, Point(tile_pad_x * 2 + tile_offset, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
        if (tile->mode <= 5) {
        snprintf(line, sizeof(line), "%d", tile->mode);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x * 2 + tile_offset, tile_pad_y + text_offset), label_width, scale);
    badger.graphics->set_font("bitmap8");
    // const char* labels[] = {"MODE++ ", " ", " "};
    // for (int i = 0; i < 3; ++i) {
    //     auto t = labels[i];
    //     int32_t text_size = badger.graphics->measure_text(t, 1.5f);
    //     int32_t text_x_offset = (tile_pad_x - text_size) / 2;
    //     badger.graphics->text(t, Point((tile_pad_x * i) + text_x_offset, 106), label_width, 1.5f);
    // }

    // const char* labels_vert[] = {"BACK", "REFRESH"};
    // for (int i = 0; i < 2; ++i) {
    //     auto t = labels_vert[i];
    //     int32_t text_size = badger.graphics->measure_text(t, 1.5f);
    //     int32_t text_x_offset = (tile_pad_x - text_size) / 2;
    //     badger.graphics->text(t, Point((tile_pad_x * 3) - 16, tile_pad_y + text_x_offset + (HEIGHT / 2 * i)), label_width, 1.5f, 270);
    // }
    badger.graphics->set_font("bitmap8");
}
