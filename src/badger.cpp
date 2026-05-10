#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "pico/util/datetime.h"

extern "C" {
#include "modules/http.h"
#include "modules/images.h"
#include "modules/ntp.h"
#include "modules/power.h"
#include "modules/restful.h"
#include "modules/tiles.h"
#include "modules/piezo.h"
}

#include "modules/screens.h"
#include "badger.h"
#include "libraries/badger2040w/badger2040w.hpp"

#define WIFI_STATUS_POLL_MS 3000
#define WIDTH 296
#define HEIGHT 128

using namespace pimoroni;

Badger2040W badger;

// BOOTSEL button detection function
// Must disable flash access while checking button state
bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;

    // Must disable interrupts, as interrupt handlers may be in flash, and we
    // are about to temporarily disable flash access!
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i);

    // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
    // Note the button pulls the pin *low* when pressed.
#if PICO_RP2040
    #define CS_BIT (1u << 1)
#else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    // Need to restore the state of chip select, else we are going to have a
    // bad time when we return to code in flash!
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

// 1 minute alarm
datetime_t ntp_alarm = datetime_t{
    .day = -1,
    .hour = -1,
    .min = 15,
    .sec = 00};

int request_buttons[] = {badger.A, badger.B, badger.C};

static volatile bool ntp_time_set = false;
static volatile bool halt_initiated = false;
static volatile bool initialised = false;

enum APP_SCREEN {
    APP_SCREEN_TILES = 0,
    APP_SCREEN_DETAIL = 1,
    APP_SCREEN_BOOST = 2,
    APP_SCREEN_SCHEDULE = 3,
};

static APP_SCREEN current_screen = APP_SCREEN_TILES;
static TILE *active_tile = NULL;
static volatile bool visible_refresh_active = false;
static volatile uint8_t visible_refresh_pending = 0;
static uint8_t visible_refresh_column = 0;

void wifi_wait();
void deinit(const char *message = NULL);
void restful_callback(void *result, int status_code, void *arg);
static void mode_update_callback(void *result, int status_code, void *arg);
static void boiler_target_update_callback(void *result, int status_code, void *arg);
static void boost_update_callback(void *result, int status_code, void *arg);
static void schedule_update_callback(void *result, int status_code, void *arg);
static void render_current_screen(const char *message = NULL);
static void refresh_tile_status(TILE *tile);
static void refresh_visible_tiles();
static void request_tile_mode(TILE *tile);
static void request_boiler_target_toggle(TILE *tile);
static void request_tile_boost_edit(TILE *tile);
static void request_tile_boost_submit(TILE *tile);
static void set_tile_boost_value(TILE *tile, int value);
static void request_tile_schedule_edit(TILE *tile);
static void request_tile_schedule_submit(TILE *tile);
static void set_tile_schedule_index(TILE *tile, int index);
static void restore_tiles_screen();
static void refresh_tile_detail(TILE *tile);
static void refresh_current_screen();

void rearm_rtc_timer() {
    // Set RTC interrupt to wake up after 15 minutes
    badger.pcf85063a->set_timer(15, PCF85063A::TIMER_TICK_1_OVER_60HZ);
    badger.pcf85063a->enable_timer_interrupt(true);

    // Clear source of interrupt
    badger.pcf85063a->clear_timer_flag();
}

void ntp_callback(datetime_t *datetime, void *arg) {
    if (datetime == NULL) {
        ntp_time_set = true;
        return;
    }
    char dt_string[128];
    datetime_to_str(dt_string, sizeof(dt_string), datetime);
    DEBUG_PRINTF("Got datetime from NTP: %s\n", dt_string);
    badger.pcf85063a->set_datetime(datetime);
    rtc_set_datetime(datetime);

    DEBUG_PRINTF("Setting daily NTP time alarm\n");

    // Flag that NTP callback has concluded
    ntp_time_set = true;
}

static void render_current_screen(const char *message) {

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

    // Always draw status bar last so it overlays all other UI elements.
    draw_status_bar(badger, message);
    badger.update();
    badger.uc8151->busy_wait();
    DEBUG_PRINTF("render_current_screen (since boot): %ld us\n", (long)(to_us_since_boot(get_absolute_time())));
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
        restful_callback);
    if (!request) {
        return;
    }

    if (tile->type == TILE_TYPE_RESTFUL) {
        http_request_with_key(
            request->base_url,
            tile->status_request->endpoint,
            tile->status_request->method,
            tile->status_request->json_body,
            REQUEST_TYPE_RESTFUL,
            restful_callback,
            request,
            tile->status_key);
        return;
    }

    restful_request(request);
}

static void refresh_visible_tiles() {
    visible_refresh_active = true;
    visible_refresh_pending = 0;
    visible_refresh_column = (uint8_t)tiles_get_column();

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

    if (visible_refresh_pending == 0) {
        visible_refresh_active = false;
        if (current_screen == APP_SCREEN_TILES && tiles_get_column() == visible_refresh_column) {
            render_current_screen(NULL);

        }
    }
}

static void refresh_current_screen() {
    if (!wifi_up()) {
        wifi_wait();
    }
    if (current_screen == APP_SCREEN_TILES) {
        refresh_visible_tiles();
    } else if (current_screen == APP_SCREEN_DETAIL && active_tile) {
        refresh_tile_detail(active_tile);
    }
}

static void restore_tiles_screen() {
    current_screen = APP_SCREEN_TILES;
    active_tile = NULL;
    badger.graphics->set_pen(15);
    badger.graphics->clear();

    if (!wifi_up()) {
        wifi_wait();
    }
    refresh_visible_tiles();
}

static void request_tile_mode(TILE *tile) {
    if (!tile || !tile->mode_request || tile->mode > 4) {
        return;
    }

    uint8_t next_mode = (tile->mode + 1) % 5;

    restful_request(restful_make_request(
        tile,
        tile_array->base_url,
        tile->mode_request,
        NULL,
        NULL,
        next_mode,
        mode_update_callback));
}

static void request_boiler_target_toggle(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_BOILER) {
        return;
    }

    if (!tile->target_request) {
        return;
    }

    uint16_t next_target = (tile->target_temp == 50) ? 350 : 50;

    restful_request(restful_make_request(
        tile,
        tile_array->base_url,
        tile->target_request,
        NULL,
        NULL,
        next_target,
        boiler_target_update_callback));
}

static void set_tile_boost_value(TILE *tile, int value) {
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
    free(tile->boost_value);
    tile->boost_value = (char *)malloc(strlen(value_str) + 1);
    strcpy(tile->boost_value, value_str);
}

static void set_tile_schedule_index(TILE *tile, int index) {
    if (!tile) {
        return;
    }
    if (tile->schedule_count == 0) {
        return;
    }

    index %= tile->schedule_count;
    if (index < 0) {
        index += tile->schedule_count;
    }

    char value_str[16];
    snprintf(value_str, sizeof(value_str), "%d", index);
    free(tile->schedule_value);
    tile->schedule_value = (char *)malloc(strlen(value_str) + 1);
    strcpy(tile->schedule_value, value_str);
}

static int get_tile_schedule_index(TILE *tile) {
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

static int get_tile_boost_value(TILE *tile) {
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

static void request_tile_boost_edit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_RADIATOR || !tile->boost_request) {
        return;
    }

    set_tile_boost_value(tile, 0);
    current_screen = APP_SCREEN_BOOST;
    render_current_screen(NULL);
}

static void request_tile_schedule_edit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_BOILER || !tile->schedule_request) {
        return;
    }

    set_tile_schedule_index(tile, 0);
    current_screen = APP_SCREEN_SCHEDULE;
    render_current_screen(NULL);
}

static void request_tile_boost_submit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_RADIATOR || !tile->boost_request) {
        return;
    }

    int boost_value = get_tile_boost_value(tile);
    restful_request(restful_make_request(
        tile,
        tile_array->base_url,
        tile->boost_request,
        NULL,
        NULL,
        boost_value,
        boost_update_callback));
}

static void request_tile_schedule_submit(TILE *tile) {
    if (!tile || tile->type != TILE_TYPE_BOILER || !tile->schedule_request) {
        return;
    }

    int idx = get_tile_schedule_index(tile);
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
    RESTFUL_REQUEST *req = restful_make_request(tile, tile_array->base_url, tmp, NULL, NULL, -1, schedule_update_callback);
    if (req) {
        restful_request(req);
    } else {
        restful_free_request_data(tmp);
    }
}

static void refresh_tile_detail(TILE *tile) {
    if (!tile) {
        return;
    }

    restful_request(restful_make_request(
        tile,
        tile_array->base_url,
        NULL,
        tile->status_request,
        tile->battery_request,
        -1,
        restful_callback));
}

void restful_callback(void *result, int status_code, void *arg) {
    if (arg == NULL) {
        return;
    }

    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    TILE *tile = (TILE *)request->tile;
    DEBUG_PRINTF("restful_callback: tile=%s, status_code=%d, battery_value=%s\n", tile ? tile->name : "(null)", status_code, (tile && tile->battery_value) ? tile->battery_value : "(null)");

    DEBUG_PRINTF("restful_callback: status_code=%d, caller=%s, tile_type=%u\n", status_code, tile->name, tile->type);

    // Store HTTP status code for rendering
    if (tile) {
        tile->http_status_code = status_code;
    }

    // Free any existing dynamic strings in tile
    if (tile->current_value) {
        free(tile->current_value);
        tile->current_value = NULL;
    }
    if (tile->target_value) {
        free(tile->target_value);
        tile->target_value = NULL;
    }
    if (tile->boost_status_value) {
        free(tile->boost_status_value);
        tile->boost_status_value = NULL;
    }
    if (tile->type == TILE_TYPE_RESTFUL) {
        if (status_code == 200 && result) {
            HTTP_REQUEST_RESULT *temp_result = (HTTP_REQUEST_RESULT *)result;
            free(tile->status_value);
            tile->status_value = (char *)malloc(strlen(temp_result->value) + 1);
            strcpy(tile->status_value, temp_result->value);
            DEBUG_PRINTF("restful_callback: restful status_value=%s\n", tile->status_value);
        } else {
            free(tile->status_value);
            tile->status_value = NULL;
        }
    } else if (tile->type == TILE_TYPE_BOILER || tile->type == TILE_TYPE_RADIATOR) {
        if (status_code == 200 && result) {
            HTTP_REQUEST_RESULT *temp_result = (HTTP_REQUEST_RESULT *)result;
            char buffer[128];
            float current = strtof(temp_result->current, NULL) / 10.0f;
            snprintf(buffer, sizeof(buffer), "%.1f", current);
            tile->current_value = (char *)malloc(strlen(buffer) + 1);
            strcpy(tile->current_value, buffer);

            float target = strtof(temp_result->target, NULL) / 10.0f;
            snprintf(buffer, sizeof(buffer), "%.1f", target);
            tile->target_value = (char *)malloc(strlen(buffer) + 1);
            strcpy(tile->target_value, buffer);

            tile->mode = (uint8_t)atoi(temp_result->mode);
            tile->target_temp = (uint16_t)atoi(temp_result->target);
            // Copy optional boost timestamp into tile->boost_status_value
            if (temp_result->boost[0]) {
                free(tile->boost_status_value);
                tile->boost_status_value = (char *)malloc(strlen(temp_result->boost) + 1);
                strcpy(tile->boost_status_value, temp_result->boost);
            } else {
                free(tile->boost_status_value);
                tile->boost_status_value = NULL;
            }
        }
    }

    if (visible_refresh_active) {
        if (visible_refresh_pending > 0) {
            visible_refresh_pending--;
        }
        if (visible_refresh_pending == 0) {
            visible_refresh_active = false;
            if (current_screen == APP_SCREEN_TILES && tiles_get_column() == visible_refresh_column) {
                render_current_screen(NULL);
            }
        }
        restful_free_request(request);
        return;
    }

    if (tile && tile->type == TILE_TYPE_RESTFUL && current_screen == APP_SCREEN_TILES && active_tile == tile) {
        render_current_screen(NULL);
    }

    if (current_screen == APP_SCREEN_DETAIL && active_tile == tile) {
        render_current_screen(NULL);
    }

    restful_free_request(request);
}

static void mode_update_callback(void *result, int status_code, void *arg) {
    (void)result;
    if (!arg) {
        return;
    }

    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    TILE *tile = (TILE *)request->tile;
    if (!tile) {
        restful_free_request(request);
        return;
    }

    if (status_code == 200) {
        refresh_tile_status(tile);
        restful_free_request(request);
        return;
    }

    restful_free_request(request);
    render_current_screen(NULL);
}

static void boiler_target_update_callback(void *result, int status_code, void *arg) {
    (void)result;
    if (!arg) {
        return;
    }

    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    TILE *tile = (TILE *)request->tile;
    if (!tile) {
        restful_free_request(request);
        return;
    }

    if (status_code == 200) {
        restful_free_request(request);
        refresh_tile_detail(tile);
        return;
    }

    restful_free_request(request);
    if (current_screen == APP_SCREEN_DETAIL && active_tile == tile) {
        render_current_screen(NULL);
    }
}

static void boost_update_callback(void *result, int status_code, void *arg) {
    (void)result;
    if (!arg) {
        return;
    }

    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    TILE *tile = (TILE *)request->tile;
    if (!tile) {
        restful_free_request(request);
        return;
    }

    restful_free_request(request);

    if (status_code == 200) {
        current_screen = APP_SCREEN_DETAIL;
        refresh_tile_detail(tile);
        return;
    }

    if (current_screen == APP_SCREEN_BOOST && active_tile == tile) {
        render_current_screen(NULL);
    }
}

static void schedule_update_callback(void *result, int status_code, void *arg) {
    (void)result;
    if (!arg) {
        return;
    }

    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    TILE *tile = (TILE *)request->tile;
    if (!tile) {
        if (request->action_request) {
            restful_free_request_data(request->action_request);
        }
        restful_free_request(request);
        return;
    }

    if (request->action_request) {
        // Free the temporary action_request created in submit
        restful_free_request_data(request->action_request);
    }

    restful_free_request(request);

    if (status_code == 200) {
        current_screen = APP_SCREEN_DETAIL;
        refresh_tile_detail(tile);
        return;
    }

    if (current_screen == APP_SCREEN_SCHEDULE && active_tile == tile) {
        render_current_screen(NULL);
    }
}

int64_t halt_timeout_callback(alarm_id_t id, void *arg) {
    halt_initiated = true;
    return 0;
}


bool datetime_is_sane(datetime_t *datetime) {
    if (datetime->year < 0 || datetime->year > 4095) { return false; }
    if (datetime->month < 1 || datetime->month > 12) { return false; }
    if (datetime->day < 1 || datetime->day > 31) { return false; }
    if (datetime->dotw < 0 || datetime->dotw > 6) { return false; }
    if (datetime->hour < 0 || datetime->hour > 23) { return false; }
    if (datetime->min < 0 || datetime->min > 59) { return false; }
    if (datetime->sec < 0 || datetime->sec > 59) { return false; }
    return true;
}

void retrieve_time(bool from_ntp) {
    ntp_time_set = false;
    // Check if RTC has been initialised previously, if not or if the RTC int is high get the internet time via NTP
    if (from_ntp) {
        DEBUG_PRINTF("Retrieving time from NTP\n");
        ntp_get_time(ntp_callback, NULL);
        // Block until the callback sets datetime
        while (!ntp_time_set) {
            tight_loop_contents();
        }
    } else {
        DEBUG_PRINTF("Retrieving time from RTC\n");
        // Retrieve stored time from external RTC
        datetime_t datetime = badger.pcf85063a->get_datetime();
        if (!datetime_is_sane(&datetime)) {
            DEBUG_PRINTF("RTC time is insane, triggering NTP retrieval\n");
            retrieve_time(true);
            return;
        }
        // Store time in internal RTC
        rtc_set_datetime(&datetime);
    }
}

alarm_id_t rearm_halt_timeout(alarm_id_t id) {
    if (id != -1) {
        cancel_alarm(id);
    }
    halt_initiated = false;
    return add_alarm_in_ms(HALT_TIMEOUT_MS, halt_timeout_callback, NULL, false);
}


void wifi_connect_async() {
    // // Use cyw43_wifi_join so that wifi channel can be specified. This shaves ~700ms of connection time, static IP / disable DNS in lwipopts.h shaves ~800ms too
    // cyw43_wifi_join(&cyw43_state, strlen(WIFI_SSID), (const uint8_t *)WIFI_SSID, strlen(WIFI_PASSWORD),
    //                 (const uint8_t *)WIFI_PASSWORD, CYW43_AUTH_WPA2_MIXED_PSK, WIFI_BSSID, WIFI_CHANNEL);

    // Just use the higher level async connect function for simplicity
      cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
}

bool wifi_up() {
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

void wifi_wait() {
    uint32_t wifi_timer = to_ms_since_boot(get_absolute_time());
    uint32_t sw_timer = 0;
    uint32_t led_timer = 0;
    uint8_t attempts = 0;
    bool led = true;
    while(!wifi_up()) {
        if (halt_initiated) {
            deinit("SLEEPING");
        }
        if (attempts > WIFI_CONNECT_ATTEMPTS) {
            deinit("NO WIFI");
        }
        // Retry wifi connect if link status is in error
        if ((sw_timer == 0 || (to_ms_since_boot(get_absolute_time()) - sw_timer) > WIFI_STATUS_POLL_MS) && cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) <= 0) {
            wifi_connect_async();
            sw_timer = to_ms_since_boot(get_absolute_time());
            attempts++;
        }
        if (led_timer == 0 || (to_ms_since_boot(get_absolute_time()) - led_timer) > 100) {
            badger.led(led ? 255 : 0);
            led = !led;
            led_timer = to_ms_since_boot(get_absolute_time());
        }
    }
    DEBUG_PRINTF("wifi_wait: %ld us\n", (long)(to_us_since_boot(get_absolute_time()) - wifi_timer));
    badger.led(255);
}

int wait_for_button_press_release() {
    uint32_t mask = (1UL << badger.A) | (1UL << badger.B) | (1UL << badger.C) | (1UL << badger.UP) | (1UL << badger.DOWN);
    uint32_t sw_timer = to_ms_since_boot(get_absolute_time());
    int counter = 0;
    while (true) {
        // Wait for button press
        while(!(gpio_get_all() & mask)) {
            if (get_bootsel_button()) {
                return -1;
            }
            // timer callback has asked for a halt
            if (halt_initiated) {
                deinit("SLEEPING");
            }
            // Allow a grace period before returning to record subsequent clicks
            if (counter > 0 && (to_ms_since_boot(get_absolute_time()) - sw_timer) > MULTI_CLICK_WAIT_MS) {
                return counter;
            }
        }
        sw_timer = to_ms_since_boot(get_absolute_time());
        counter++;
        badger.update_button_states();
        badger.led(0);

        // Wait for button release
        while((gpio_get_all() & mask)) {
            tight_loop_contents();
        }

        badger.led(255);
        sleep_ms(80);    // debounce
    }
}

void tiles_init() {
    tiles_make_tiles();
    if (!wifi_up()) {
        wifi_wait();
    }
    initialised = true;
    current_screen = APP_SCREEN_TILES;
    active_tile = NULL;
    refresh_visible_tiles();
}

void init() {
    badger.init();
    badger.led(255);


    adc_init();
    rtc_init();

    if (cyw43_arch_init()) {
        DEBUG_PRINTF("failed to initialise\n");
        deinit();
    }
    cyw43_arch_enable_sta_mode();

    wifi_connect_async();

    badger.graphics->set_font("bitmap8");
    badger.uc8151->set_update_speed(2);
    badger.graphics->set_thickness(2);

    badger.graphics->set_pen(15);
    badger.graphics->clear();

    // External RTC has 1 free byte, we can use this to store the current column and retrieve at reboot
    // This limits the max num of columns to 2^8 - 2, since 0 is being used here to signal that the RTC is not initialised
    uint8_t rtc_byte = badger.pcf85063a->get_byte();
    tiles_set_column((rtc_byte - 1 >= 0) ? rtc_byte - 1: 0);

    // If External RTC is not initialized or we were woken by an external RTC interrupt
    if(!rtc_byte || badger.pressed_to_wake(badger.RTC)) {
        // Must wait for WiFi to be up before triggering an NTP request
        wifi_wait();
        // Initialise tiles
        tiles_init();
        // Retrieve time from NTP and re-arm RTC alarm
        retrieve_time(true);

        // If we were woken by the RTC alarm, just go back to sleep
        if (badger.pressed_to_wake(badger.RTC)) {
            deinit("SLEEPING");
        }
        
        // Set the external RTC free byte so we can later determine if it has been initalized
        badger.pcf85063a->set_byte(1);
    } else {
        retrieve_time(false);
    }
}

void deinit(const char *message) {
    // Try again later if we are charging, because calling halt now would keep us awake
    if (power_is_charging()) {
        DEBUG_PRINTF("Cannot deinit whilst charging, rearming halt timeout instead of sleeping\n");
        rearm_halt_timeout(-1);
        return;
    }

    DEBUG_PRINTF("Going to sleep zZzZ\n");
    if (message) {
        restore_tiles_screen();
        while(visible_refresh_pending > 0) {
            tight_loop_contents();
        }
    }
    if(initialised) {
        cyw43_arch_deinit();
    }
    
    tiles_free();
    rearm_rtc_timer();
    badger.led(0);
    badger.halt();
}


int main() {
    // Calling the full init here is too slow if we want to catch button presses from wake, so instead
    // Only configure crucial stuff for now e.g Enable_3v3, LED & check RTC

    stdio_init_all();
    gpio_set_function(badger.ENABLE_3V3, GPIO_FUNC_SIO);
    gpio_set_dir(badger.ENABLE_3V3, GPIO_OUT);
    gpio_put(badger.ENABLE_3V3, 1);
    pwm_config cfg = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(badger.LED), 65535);
    pwm_init(pwm_gpio_to_slice_num(badger.LED), &cfg, true);
    gpio_set_function(badger.LED, GPIO_FUNC_PWM);
    gpio_set_function(badger.RTC, GPIO_FUNC_SIO);
    gpio_set_dir(badger.RTC, GPIO_IN);
    gpio_set_pulls(badger.RTC, false, true);
    if (gpio_get_all() & 1 << badger.RTC) {
        init();
    }

    alarm_id_t halt_timeout_id = -1;
    while(true) {
        halt_timeout_id = rearm_halt_timeout(halt_timeout_id);

        
        // Wait for button press
        int click_count = wait_for_button_press_release();

        if (!initialised) {
            init();
            tiles_init();
            continue;
        }

        if (visible_refresh_active) {
            continue;
        }

        // Check BOOTSEL button
        if (click_count == -1) {
            badger.led(0);
            DEBUG_PRINTF("BOOTSEL button pressed, refreshing data\n");
            while(click_count == -1) {
                click_count = get_bootsel_button() ? -1 : 0;
                sleep_ms(10);
            }
            badger.led(255);
            refresh_current_screen();
            continue;
        }

        char tiles_base_idx = tiles_get_base_idx();

        if (current_screen == APP_SCREEN_TILES) {
            if (badger.pressed(badger.UP) || badger.pressed(badger.DOWN)) {
                if (badger.pressed(badger.UP)) {
                    tiles_previous_column(click_count);
                } else {
                    tiles_next_column(click_count);
                }
                badger.pcf85063a->set_byte(tiles_get_column() + 1);
                if (!wifi_up()) {
                    wifi_wait();
                }
                refresh_visible_tiles();
                initialised = true;
                continue;
            }

            TILE *tile = NULL;
            for (uint i = 0; i < count_of(request_buttons); i++) {
                if (!badger.pressed(request_buttons[i])) {
                    continue;
                }
                if (tiles_idx_in_bounds(tiles_base_idx + i)) {
                    tile = tile_array->tiles[tiles_base_idx + i];
                }
            }

            if (!tile) {
                continue;
            }

            active_tile = tile;
            
            // For RESTFUL tiles, just trigger the action/status callback chain
            if (tile->type == TILE_TYPE_RESTFUL) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                restful_request(restful_make_request(
                    tile,
                    tile_array->base_url,
                    tile->action_request,
                    tile->status_request,
                    NULL,
                    -1,
                    restful_callback));
                continue;
            }
            
            current_screen = APP_SCREEN_DETAIL;
            // Defer rendering until after status/battery requests complete.
            refresh_tile_detail(active_tile);
            initialised = true;
            continue;
        }

        if (current_screen == APP_SCREEN_DETAIL && active_tile) {
            if (badger.pressed(badger.UP)) {
                restore_tiles_screen();
                continue;
            }
            if (badger.pressed(badger.DOWN)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                refresh_tile_detail(active_tile);
                continue;
            }
            if (badger.pressed(badger.A)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                if (active_tile->type == TILE_TYPE_RADIATOR) {
                    request_tile_boost_edit(active_tile);
                } else {
                    if (active_tile->schedule_request) {
                        request_tile_schedule_edit(active_tile);
                    } else {
                        request_tile_mode(active_tile);
                    }
                }
                continue;
            }
            if (badger.pressed(badger.B) && active_tile->type == TILE_TYPE_BOILER) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                request_boiler_target_toggle(active_tile);
                continue;
            }
        }

        if (current_screen == APP_SCREEN_BOOST && active_tile) {
            if (badger.pressed(badger.UP)) {
                current_screen = APP_SCREEN_DETAIL;
                if (!wifi_up()) {
                    wifi_wait();
                }
                refresh_tile_detail(active_tile);
                continue;
            }
            if (badger.pressed(badger.B)) {
                set_tile_boost_value(active_tile, get_tile_boost_value(active_tile) + click_count);
                render_current_screen(NULL);
                continue;
            }
            if (badger.pressed(badger.C)) {
                set_tile_boost_value(active_tile, get_tile_boost_value(active_tile) - click_count);
                render_current_screen(NULL);
                continue;
            }
            if (badger.pressed(badger.A)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                request_tile_boost_submit(active_tile);
                continue;
            }
        }

        if (current_screen == APP_SCREEN_SCHEDULE && active_tile) {
            if (badger.pressed(badger.UP)) {
                current_screen = APP_SCREEN_DETAIL;
                if (!wifi_up()) {
                    wifi_wait();
                }
                refresh_tile_detail(active_tile);
                continue;
            }
            if (badger.pressed(badger.B)) {
                int idx = get_tile_schedule_index(active_tile) + click_count;
                set_tile_schedule_index(active_tile, idx);
                render_current_screen(NULL);
                continue;
            }
            if (badger.pressed(badger.C)) {
                // decrement schedule index by click_count
                int idx = get_tile_schedule_index(active_tile) - click_count;
                set_tile_schedule_index(active_tile, idx);
                render_current_screen(NULL);
                continue;
            }
            if (badger.pressed(badger.A)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                request_tile_schedule_submit(active_tile);
                continue;
            }
        }

    }
    return 0;
}
