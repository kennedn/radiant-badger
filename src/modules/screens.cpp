#include "modules/screens.h"

#include <stdio.h>
#include <string.h>

#include "badger.h"

extern "C" {
#include "modules/images.h"
#include "modules/power.h"
}

#include <time.h>

extern "C" char *strptime(const char *s, const char *format, struct tm *tm);

#include "pico/util/datetime.h"
#include "pico/stdlib.h"
#include "libraries/badger2040w/badger2040w.hpp"

using namespace pimoroni;

#define WIDTH 296
#define HEIGHT 128

static bool format_boost_value(const char *boost_value, char *buffer, size_t buffer_size) {
    setenv("TZ", TZ, 1);
    tzset();
    if (!boost_value || !boost_value[0] || !buffer || buffer_size == 0) {
        return false;
    }

    struct tm parsed = {0};
    char *end = strptime(boost_value, "%Y-%m-%dT%H:%M:%SZ", &parsed);
    if (!end) {
        return false;
    }

    time_t boost_time = pico_mktime(&parsed);
    if (boost_time < 0) {
        return false;
    }

    datetime_t boost_datetime;
    if (!time_to_datetime(boost_time, &boost_datetime)) {
        return false;
    }

    snprintf(buffer, buffer_size, "%02d:%02d", boost_datetime.hour, boost_datetime.min);
    return true;
}

static int get_boost_edit_value(TILE *tile) {
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

static int get_schedule_edit_index(TILE *tile) {
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

void draw_status_bar(pimoroni::Badger2040W &badger, const char *title, bool sleeping) {
    datetime_t datetime;
    float voltage;
    char powerPercent = 0;
    char percentStr[5] = {0};
    char clock_str[24] = {0};
    char status_x_offset = 0;
    char x_pad = 4;
    int32_t clock_x_offset;
    uint8_t *wifi_image = (uint8_t *)image_icon_wifi_on;
    uint8_t *battery_image = (uint8_t *)image_status_battery_charging;
    uint8_t *heading_icon = (uint8_t *)tiles_get_heading()->icon;

    if (!power_is_charging()) {
        battery_image = (uint8_t *)image_status_battery_discharging;
        status_x_offset = 22;

        power_voltage(&voltage);
        powerPercent = power_percent(&voltage);
        sprintf(percentStr, "%d%%", powerPercent);
    }

    if (sleeping) {
        wifi_image = (uint8_t *)image_status_sleeping;
    }
    
    if (title) {
        snprintf(clock_str, sizeof(clock_str), "%s", title);
    } else {
        if(!wifi_up()) {
            wifi_image = (uint8_t *)image_icon_wifi_off;
        }
        rtc_get_datetime(&datetime);
        snprintf(clock_str, sizeof(clock_str), "%02d:%02d\n", datetime.hour, datetime.min);
    }
    clock_x_offset = badger.graphics->measure_text(clock_str, 2.0f) / 2;

    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(0, 0, WIDTH, 20));

    badger.graphics->set_pen(15);
    badger.graphics->text(clock_str, Point(WIDTH / 2 - clock_x_offset, 3), WIDTH, 2.0f);

    if (heading_icon) {
        badger.graphics->text(tiles_get_heading()->heading, Point(image_status_size + x_pad * 2, 3), WIDTH, 2.0f);
        badger.image(heading_icon, Rect(x_pad, 2, image_status_size, image_status_size));
    } else if (!title) {
        badger.graphics->text("radiantBadger", Point(x_pad, 6), WIDTH, 1.0f);
    }
    badger.image(battery_image, Rect(WIDTH - status_x_offset - (image_status_size + x_pad), 2, image_status_size, image_status_size));
    badger.image(wifi_image, Rect(WIDTH - status_x_offset - (image_status_size + x_pad) * 2, 2, image_status_size, image_status_size));
    if (!power_is_charging()) {
        badger.graphics->set_pen(15);
        badger.graphics->text(percentStr, Point(WIDTH - status_x_offset, 6), WIDTH, 1.0);
        badger.graphics->rectangle(Rect(WIDTH - status_x_offset - (image_status_size + x_pad) + 2, 7, powerPercent / 10 + 1, 6));
    }
}

void draw_tiles(pimoroni::Badger2040W &badger, const char *selected_name, const char *indicator_icon) {
    char tiles_base_idx = tiles_get_base_idx();
    char tile_pad_x = WIDTH / 3;
    char tile_pad_y = 25 + 4;
    char tile_offset = 18;
    // char column_pad_y = 10;
    char indicator_offset = 44;
    float scale = 2.0f;
        
    //Footer Rect
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(0, HEIGHT - 20, WIDTH, 20));

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
        if (tile->type == TILE_TYPE_RESTFUL) {
            char status_text[8];
            
            // Check if we got a non-200 HTTP response (error case)
            if (tile->http_status_code != 200 && tile->http_status_code != -1) {
                snprintf(status_text, sizeof(status_text), "?");
            } else if (!tile->status_value || !tile->status_value[0]) {
                // No status value (no endpoint or not yet fetched)
                status_text[0] = '\0';
            } else if (tile->status_on_value && tile->status_off_value && !strcmp(tile->status_value, tile->status_on_value)) {
                snprintf(status_text, sizeof(status_text), "ON");
            } else if (tile->status_on_value && tile->status_off_value && !strcmp(tile->status_value, tile->status_off_value)) {
                snprintf(status_text, sizeof(status_text), "OFF");
            } else {
                snprintf(status_text, sizeof(status_text), "?");
            }
            
            badger.graphics->set_pen(15);
            int32_t text_size = badger.graphics->measure_text(status_text, scale);
            int32_t text_x_offset = (tile_pad_x - text_size) / 2;
            badger.graphics->text(status_text, Point((tile_pad_x*i) + text_x_offset, HEIGHT - 20 + 3), tile_pad_x, scale);
        } else if (tile->current_value && tile->target_value) {
            char display_value[128];
            snprintf(display_value, sizeof(display_value), "%s/%s", tile->current_value, tile->target_value);
            badger.graphics->set_pen(15);
            int32_t text_size = badger.graphics->measure_text(display_value, scale);
            int32_t text_x_offset = (tile_pad_x - text_size) / 2;
            badger.graphics->text(display_value, Point((tile_pad_x*i) + text_x_offset, HEIGHT - 20 + 3), tile_pad_x, scale);
        }
        if(selected_name && !strcmp(selected_name, tile->name)) {
            badger.image((const uint8_t *)indicator_icon,
                Rect(tile_offset + (tile_pad_x*i) + indicator_offset, tile_pad_y + indicator_offset, image_indicator_size, image_indicator_size));
        }
    }
    DEBUG_PRINTF("current column: %d, max: %d\n", tiles_get_column(), tiles_max_column());
    char total_columns = tiles_max_column();
    char current_column = tiles_get_column();
    char pad = 1;  // Vertical padding between rects
    
    // Calculate square height to fit all columns in available space (between header and footer)
    char available_height = HEIGHT - 40;  // Between y=20 and y=HEIGHT-20
    char square_height = (available_height - (total_columns - 1) * pad) / total_columns;  // Account for gaps
    if (square_height < 4) square_height = 4;  // Minimum height
    
    char total_height = total_columns * square_height + (total_columns - 1) * pad;  // Include gaps
    char start_y = 20 + ((available_height - total_height) / 2);  // Center within safe area
    
    for (char i = 0; i < total_columns; i++) {
        badger.graphics->set_pen(0);
        char y = start_y + (i * (square_height + pad));  // +pad for gap
        badger.graphics->rectangle(Rect(WIDTH - 10, y, 18, square_height));
        if (current_column != i){
            badger.graphics->set_pen(15);
            badger.graphics->rectangle(Rect(WIDTH - 10 + 1, y + 1, 16, square_height - 2));
        }
    }
}

void draw_tile_detail(pimoroni::Badger2040W &badger, TILE *tile) {
    if (!tile) {
        badger.graphics->set_pen(0);
        badger.graphics->text("No tile selected", Point(20, 40), WIDTH, 2.0f);
        return;
    }


    uint8_t *back_indicator = (uint8_t *)image_indicator_back;
    uint8_t *refresh_indicator = (uint8_t *)image_indicator_refresh;

    // Use the same positioning grid as draw_tiles for consistency
    char tile_pad_x = WIDTH / 3;      // 98px per column
    char tile_pad_y = 25 + 4;              // vertical position for content
    char tile_offset = 18;             // x offset from column start
    char text_offset = 20;             // y offset for text below icon
    char label_width = tile_pad_x;
    float scale = 2.0f;

    // Tile A: icon position (same as tiles view)
    int32_t name_y_offset_full = badger.graphics->bitmap_font->height * scale;
    int32_t name_y_offset = name_y_offset_full * 0.75f;
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

    //Footer Rect
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(0, HEIGHT - 20, WIDTH, 20));

    char line[48];
    
    // Row 1 Column 2: Current label and value
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "CURRENT");
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
    if (tile->current_value) {
        snprintf(line, sizeof(line), "%s", tile->current_value);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, tile_pad_y + text_offset), label_width, scale);

    // Row 1 Column 3: Target label and value
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "TARGET");
    badger.graphics->text(line, Point(tile_pad_x * 2, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
    if (tile->target_value) {
        snprintf(line, sizeof(line), "%s", tile->target_value);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x * 2, tile_pad_y + text_offset), label_width, scale);

    // Row 2 Column 2: Battery label and value
    if (tile->type == TILE_TYPE_RADIATOR) {
        snprintf(line, sizeof(line), "BATTERY");
        badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset * 2 - 18), label_width, scale);
        if (tile->battery_value && tile->battery_value[0]) {
            snprintf(line, sizeof(line), "%s%%", tile->battery_value);
        } else {
            snprintf(line, sizeof(line), "--");
        }
        badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset - 18), label_width, scale);
    }

    // Row 2 Column 3: Boost label and value
    badger.graphics->set_font("bitmap8");
    if (tile->type == TILE_TYPE_RADIATOR) {
        snprintf(line, sizeof(line), "BOOST");
        badger.graphics->text(line, Point(tile_pad_x * 2, HEIGHT - text_offset * 2 - 18), label_width, scale);
        if (format_boost_value(tile->boost_status_value, line, sizeof(line))) {
            // formatted above
        } else {
            snprintf(line, sizeof(line), "Off");
        }
        badger.graphics->text(line, Point(tile_pad_x * 2, HEIGHT - text_offset - 18), label_width, scale);
    } else {
        snprintf(line, sizeof(line), "SCHEDULE");
        badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset * 2 - 18), label_width, scale);
        if (tile->schedule_status_value && tile->schedule_status_value[0]) {
            snprintf(line, sizeof(line), "%s", tile->schedule_status_value);
        } else {
            snprintf(line, sizeof(line), "--");
        }
        badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset - 18), label_width, scale);
    }
    badger.graphics->set_font("bitmap8");

    const char* radiator_labels[] = { "BOOST", "", "" };
    const char* boiler_labels[] = { "SCHEDULE", "TOGGLE", "" };
    const char **labels = (tile->type == TILE_TYPE_RADIATOR) ? radiator_labels : boiler_labels;
    for (int i = 0; i < 3; ++i) {
        auto t = labels[i];
        if (!t[0]) {
            continue;
        }
        int32_t text_size = badger.graphics->measure_text(t, scale);
        int32_t text_x_offset = (tile_pad_x - text_size) / 2;
        Point text_point = Point((tile_pad_x * i) + text_x_offset, HEIGHT - name_y_offset - 4);
        badger.graphics->set_pen(15);
        badger.graphics->text(t, text_point, label_width, scale);
    }

    Rect back_rect = Rect(WIDTH - image_indicator_size - 4, 30, image_indicator_size, image_indicator_size);
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(back_rect.x - 2, back_rect.y - 2, back_rect.w + 14, back_rect.h + 4));
    badger.graphics->set_pen(15);
    badger.graphics->rectangle(Rect(back_rect.x - 1, back_rect.y - 1, back_rect.w + 12, back_rect.h + 2));
    badger.image(back_indicator, back_rect);

    Rect refresh_rect = Rect(WIDTH - image_indicator_size - 4, HEIGHT - 30 - image_indicator_size, image_indicator_size, image_indicator_size);
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(refresh_rect.x - 2, refresh_rect.y - 2, refresh_rect.w + 14, refresh_rect.h + 4));
    badger.graphics->set_pen(15);
    badger.graphics->rectangle(Rect(refresh_rect.x - 1, refresh_rect.y - 1, refresh_rect.w + 12, refresh_rect.h + 2));
    badger.image(refresh_indicator, refresh_rect);

    // const char* labels_vert[] = {"BACK", "REFRESH"};
    // for (int i = 0; i < 2; ++i) {
    //     auto t = labels_vert[i];
    //     int32_t text_size = badger.graphics->measure_text(t, 1.5f);
    //     int32_t text_x_offset = (tile_pad_x - text_size) / 2;
    //     badger.graphics->text(t, Point((tile_pad_x * 3) - 16, tile_pad_y + text_x_offset + (HEIGHT / 2 * i)), label_width, 1.5f, 270);
    // }
    badger.graphics->set_font("bitmap8");
}

void draw_tile_boost(pimoroni::Badger2040W &badger, TILE *tile) {
    if (!tile) {
        badger.graphics->set_pen(0);
        badger.graphics->text("No tile selected", Point(20, 40), WIDTH, 2.0f);
        return;
    }

    uint8_t *back_indicator = (uint8_t *)image_indicator_back;

    // Keep detail layout identical, only change boost value and bottom labels.
    char tile_pad_x = WIDTH / 3;
    char tile_pad_y = 25 + 4;
    char tile_offset = 18;
    char text_offset = 20;
    char label_width = tile_pad_x;
    float scale = 2.0f;

    int32_t name_y_offset_full = badger.graphics->bitmap_font->height * scale;
    int32_t name_y_offset = name_y_offset_full * 0.75f;
    if (tile->image) {
        badger.image((const uint8_t *)tile->image, Rect(tile_offset, tile_pad_y + name_y_offset, image_tile_size, image_tile_size));
    }

    badger.graphics->set_pen(0);
    badger.graphics->set_font("bitmap8");
    int32_t name_size = badger.graphics->measure_text(tile->name, scale);
    int32_t name_x_offset = (tile_pad_x - name_size) / 2;
    badger.graphics->text(tile->name, Point(name_x_offset, tile_pad_y), tile_pad_x, scale);
    badger.graphics->set_font("bitmap8");

    // Footer Rect
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(0, HEIGHT - 20, WIDTH, 20));

    char line[48];
    int boost_value = get_boost_edit_value(tile);

    // Row 1 Column 2: Current label and value
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "CURRENT");
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
    if (tile->current_value) {
        snprintf(line, sizeof(line), "%s", tile->current_value);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, tile_pad_y + text_offset), label_width, scale);

    // Row 1 Column 3: Target label and value
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "TARGET");
    badger.graphics->text(line, Point(tile_pad_x * 2, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
    if (tile->target_value) {
        snprintf(line, sizeof(line), "%s", tile->target_value);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x * 2, tile_pad_y + text_offset), label_width, scale);

    // Row 2 Column 2: Battery label and value
    if (tile->type == TILE_TYPE_RADIATOR) {
        snprintf(line, sizeof(line), "BATTERY");
        badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset * 2 - 18), label_width, scale);
        if (tile->battery_value && tile->battery_value[0]) {
            snprintf(line, sizeof(line), "%s%%", tile->battery_value);
        } else {
            snprintf(line, sizeof(line), "--");
        }
        badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset - 18), label_width, scale);
    }

    // Row 2 Column 3: Boost label and value
    badger.graphics->set_font("bitmap8");
    if (tile->type == TILE_TYPE_RADIATOR) {
        snprintf(line, sizeof(line), "BOOST");
        badger.graphics->text(line, Point(tile_pad_x * 2, HEIGHT - text_offset * 2 - 18), label_width, scale);
        snprintf(line, sizeof(line), "%d", boost_value);
        badger.graphics->text(line, Point(tile_pad_x * 2, HEIGHT - text_offset - 18), label_width, scale);
    } else {
        snprintf(line, sizeof(line), "MODE");
        badger.graphics->text(line, Point(tile_pad_x * 2, HEIGHT - text_offset * 2 - 18), label_width, scale);
        if (tile->mode <= 5) {
            snprintf(line, sizeof(line), "%d", tile->mode);
        } else {
            snprintf(line, sizeof(line), "--");
        }
        badger.graphics->text(line, Point(tile_pad_x * 2, HEIGHT - text_offset - 18), label_width, scale);
    }
    badger.graphics->set_font("bitmap8");

    const char* boost_labels[] = { "OK ", "+", "  -" };
    for (int i = 0; i < 3; ++i) {
        const char *t = boost_labels[i];
        scale = (i == 0) ? 2.0f : 3.0f;
        uint8_t text_height = badger.graphics->bitmap_font->height * scale;
        int32_t text_size = badger.graphics->measure_text(t, scale);
        int32_t text_x_offset = (tile_pad_x - text_size) / 2;
        Point text_point = Point((tile_pad_x * i) + text_x_offset, HEIGHT - text_height);
        badger.graphics->set_pen(15);
        badger.graphics->text(t, text_point, label_width, scale);
    }

    Rect back_rect = Rect(WIDTH - image_indicator_size - 4, 30, image_indicator_size, image_indicator_size);
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(back_rect.x - 2, back_rect.y - 2, back_rect.w + 14, back_rect.h + 4));
    badger.graphics->set_pen(15);
    badger.graphics->rectangle(Rect(back_rect.x - 1, back_rect.y - 1, back_rect.w + 12, back_rect.h + 2));
    badger.image(back_indicator, back_rect);

    badger.graphics->set_font("bitmap8");
}

void draw_tile_schedule(pimoroni::Badger2040W &badger, TILE *tile) {
    if (!tile) {
        badger.graphics->set_pen(0);
        badger.graphics->text("No tile selected", Point(20, 40), WIDTH, 2.0f);
        return;
    }

    uint8_t *back_indicator = (uint8_t *)image_indicator_back;

    // Keep detail layout identical, only change schedule value and bottom labels.
    char tile_pad_x = WIDTH / 3;
    char tile_pad_y = 25 + 4;
    char tile_offset = 18;
    char text_offset = 20;
    char label_width = tile_pad_x;
    float scale = 2.0f;

    int32_t name_y_offset_full = badger.graphics->bitmap_font->height * scale;
    int32_t name_y_offset = name_y_offset_full * 0.75f;
    if (tile->image) {
        badger.image((const uint8_t *)tile->image, Rect(tile_offset, tile_pad_y + name_y_offset, image_tile_size, image_tile_size));
    }

    badger.graphics->set_pen(0);
    badger.graphics->set_font("bitmap8");
    int32_t name_size = badger.graphics->measure_text(tile->name, scale);
    int32_t name_x_offset = (tile_pad_x - name_size) / 2;
    badger.graphics->text(tile->name, Point(name_x_offset, tile_pad_y), tile_pad_x, scale);
    badger.graphics->set_font("bitmap8");

    // Footer Rect
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(0, HEIGHT - 20, WIDTH, 20));

    char line[48];
    int schedule_index = get_schedule_edit_index(tile);

    // Row 1 Column 2: Current label and value
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "CURRENT");
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
    if (tile->current_value) {
        snprintf(line, sizeof(line), "%s", tile->current_value);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, tile_pad_y + text_offset), label_width, scale);

    // Row 1 Column 3: Target label and value
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "TARGET");
    badger.graphics->text(line, Point(tile_pad_x * 2, tile_pad_y), label_width, scale);
    badger.graphics->set_font("bitmap8");
    if (tile->target_value) {
        snprintf(line, sizeof(line), "%s", tile->target_value);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x * 2, tile_pad_y + text_offset), label_width, scale);

    // Row 2 Column 2: Battery label and value
    if (tile->type == TILE_TYPE_RADIATOR) {
        snprintf(line, sizeof(line), "BATTERY");
        badger.graphics->text(line, Point(tile_pad_x, HEIGHT - text_offset * 2 - 18), label_width, scale);
        if (tile->battery_value && tile->battery_value[0]) {
            snprintf(line, sizeof(line), "%s%%", tile->battery_value);
        } else {
            snprintf(line, sizeof(line), "--");
        }
        badger.graphics->text(line, Point(tile_pad_x, HEIGHT - text_offset - 18), label_width, scale);
    }

    // Row 2 Column 3: Schedule label and value
    badger.graphics->set_font("bitmap8");
    snprintf(line, sizeof(line), "SCHEDULE");
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset * 2 - 18), label_width, scale);
    if (tile->schedules && tile->schedule_count > 0 && schedule_index >= 0 && schedule_index < tile->schedule_count) {
        snprintf(line, sizeof(line), "%s", tile->schedules[schedule_index]);
    } else {
        snprintf(line, sizeof(line), "--");
    }
    badger.graphics->text(line, Point(tile_pad_x + tile_offset / 2, HEIGHT - text_offset - 18), label_width, scale);
    badger.graphics->set_font("bitmap8");

    const char* boost_labels[] = { "OK ", "+", "  -" };
    for (int i = 0; i < 3; ++i) {
        const char *t = boost_labels[i];
        scale = (i == 0) ? 2.0f : 3.0f;
        uint8_t text_height = badger.graphics->bitmap_font->height * scale;
        int32_t text_size = badger.graphics->measure_text(t, scale);
        int32_t text_x_offset = (tile_pad_x - text_size) / 2;
        Point text_point = Point((tile_pad_x * i) + text_x_offset, HEIGHT - text_height);
        badger.graphics->set_pen(15);
        badger.graphics->text(t, text_point, label_width, scale);
    }

    Rect back_rect = Rect(WIDTH - image_indicator_size - 4, 30, image_indicator_size, image_indicator_size);
    badger.graphics->set_pen(0);
    badger.graphics->rectangle(Rect(back_rect.x - 2, back_rect.y - 2, back_rect.w + 14, back_rect.h + 4));
    badger.graphics->set_pen(15);
    badger.graphics->rectangle(Rect(back_rect.x - 1, back_rect.y - 1, back_rect.w + 12, back_rect.h + 2));
    badger.image(back_indicator, back_rect);

    badger.graphics->set_font("bitmap8");
}
