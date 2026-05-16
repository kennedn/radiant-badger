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
#include "modules/app_runtime.h"
#include "modules/ntp_helper.h"
#include "modules/tiles.h"
#include "modules/tile_state.h"
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

static volatile bool halt_initiated = false;

void wifi_wait();
void deinit(const char *message = NULL);

int64_t halt_timeout_callback(alarm_id_t id, void *arg) {
    halt_initiated = true;
    return 0;
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
            deinit(NULL);
        }
        if (attempts > WIFI_CONNECT_ATTEMPTS) {
            deinit("NO WIFI");
        }
        // Retry wifi connect if link status is in error
        if ((sw_timer == 0 || (to_ms_since_boot(get_absolute_time()) - sw_timer) > WIFI_STATUS_POLL_MS) && !wifi_up()) {
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

static void http_activity_wait_led() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    static uint32_t request_start_timer = 0;
    static uint32_t led_timer = 0;
    static bool led = true;

    if (halt_initiated || !app_is_initialised()) {
        led = false;
        badger.led(0);
        return;
    }

    if (http_active_request_count() == 0) {
        request_start_timer = 0;
        led_timer = 0;
        led = true;
        badger.led(255);
        return;
    }

    if (request_start_timer == 0) {
        request_start_timer = now;
        badger.led(255);
        return;
    }

    if ((now - request_start_timer) < 300) {
        badger.led(255);
        return;
    }

    if (led_timer == 0 || (now - led_timer) > 100) {
        badger.led(led ? 255 : 0);
        led = !led;
        led_timer = now;
    }
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
                deinit(NULL);
            }
            // Allow a grace period before returning to record subsequent clicks
            if (counter > 0 && (to_ms_since_boot(get_absolute_time()) - sw_timer) > MULTI_CLICK_WAIT_MS) {
                return counter;
            }
            http_activity_wait_led();
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
    app_set_initialised(true);
    app_runtime_reset();
    app_refresh_visible_tiles(true);
}

void init() {
    badger.init();
    badger.led(0);

    adc_init();
    rtc_init();

    if (cyw43_arch_init()) {
        DEBUG_PRINTF("failed to initialise\n");
        deinit("NO WIFI");
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
        // Retrieve time from NTP and re-arm RTC alarm
        ntp_helper_retrieve_time(true);
        // Initialise tiles after the clock is valid so the first refresh shows the real time
        tiles_init();

        // If we were woken by the RTC alarm, wait for the initial
        // visible refresh to complete, render once, then go back to sleep.
        if (badger.pressed_to_wake(badger.RTC)) {
            while (app_is_refresh_pending()) {
                tight_loop_contents();
            }
            deinit(NULL);
        }
        
        // Set the external RTC free byte so we can later determine if it has been initalized
        badger.pcf85063a->set_byte(1);
    } else {
        ntp_helper_retrieve_time(false);
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

    // The RTC wake path already refreshed the tiles once during init(), so avoid a second
    // visible refresh here when we're just going straight back to sleep.
    if (!badger.pressed_to_wake(badger.RTC)) {
        // Return to tiles screen, update status(s) and render to e-ink one last time before going to sleep
        app_restore_tiles_screen(true);
        while(app_is_refresh_pending()) {
            tight_loop_contents();
        }
    }

    if(app_is_initialised()) {
        cyw43_arch_deinit();
    }
    
    tiles_free();
    ntp_helper_rearm_rtc_timer();
    badger.led(0);
    badger.halt();
}


int main() {
    // Calling the full init here is too slow if we want to catch button presses from wake, so instead
    // Only configure crucial stuff for now e.g Enable_3v3, LED & check RTC

    stdio_init_all();
    // Badger 2040W only recieves power from battery when a button / rtc / 3v3 pin are high, 
    //so we must set the 3v3 pin as soon as possible to latch power on after a button press
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

        if (!app_is_initialised()) {
            init();
            tiles_init();
            continue;
        }

        if (app_is_refresh_pending()) {
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
            app_refresh_current_screen();
            continue;
        }

        char tiles_base_idx = tiles_get_base_idx();

        if (app_get_current_screen() == APP_SCREEN_TILES) {
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
                app_refresh_visible_tiles(true);
                app_set_initialised(true);
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

            app_set_active_tile(tile);
            
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
                    app_restful_callback));
                continue;
            }
            
            app_set_current_screen(APP_SCREEN_DETAIL);
            // Defer rendering until after status/battery requests complete.
            app_refresh_tile_detail(app_get_active_tile());
            app_set_initialised(true);
            continue;
        }

        if (app_get_current_screen() == APP_SCREEN_DETAIL && app_get_active_tile()) {
            if (badger.pressed(badger.UP)) {
                app_restore_tiles_screen(true);
                continue;
            }
            if (badger.pressed(badger.DOWN)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                app_refresh_tile_detail(app_get_active_tile());
                continue;
            }
            if (badger.pressed(badger.A)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                if (app_get_active_tile()->type == TILE_TYPE_RADIATOR) {
                    app_request_tile_boost_edit(app_get_active_tile());
                } else {
                    if (app_get_active_tile()->schedule_request) {
                        app_request_tile_schedule_edit(app_get_active_tile());
                    }
                }
                continue;
            }
            if (badger.pressed(badger.B) && app_get_active_tile()->type == TILE_TYPE_BOILER) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                app_request_boiler_target_toggle(app_get_active_tile());
                continue;
            }
        }

        if (app_get_current_screen() == APP_SCREEN_BOOST && app_get_active_tile()) {
            if (badger.pressed(badger.UP)) {
                app_set_current_screen(APP_SCREEN_DETAIL);
                if (!wifi_up()) {
                    wifi_wait();
                }
                app_refresh_tile_detail(app_get_active_tile());
                continue;
            }
            if (badger.pressed(badger.B)) {
                tile_set_boost_value(app_get_active_tile(), tile_get_boost_value(app_get_active_tile()) + click_count);
                app_render_current_screen(NULL, false);
                continue;
            }
            if (badger.pressed(badger.C)) {
                tile_set_boost_value(app_get_active_tile(), tile_get_boost_value(app_get_active_tile()) - click_count);
                app_render_current_screen(NULL, false);
                continue;
            }
            if (badger.pressed(badger.A)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                app_request_tile_boost_submit(app_get_active_tile());
                continue;
            }
        }

        if (app_get_current_screen() == APP_SCREEN_SCHEDULE && app_get_active_tile()) {
            if (badger.pressed(badger.UP)) {
                app_set_current_screen(APP_SCREEN_DETAIL);
                if (!wifi_up()) {
                    wifi_wait();
                }
                app_refresh_tile_detail(app_get_active_tile());
                continue;
            }
            if (badger.pressed(badger.B)) {
                int idx = tile_get_schedule_index(app_get_active_tile()) + click_count;
                tile_set_schedule_index(app_get_active_tile(), idx);
                app_render_current_screen(NULL, false);
                continue;
            }
            if (badger.pressed(badger.C)) {
                // decrement schedule index by click_count
                int idx = tile_get_schedule_index(app_get_active_tile()) - click_count;
                tile_set_schedule_index(app_get_active_tile(), idx);
                app_render_current_screen(NULL, false);
                continue;
            }
            if (badger.pressed(badger.A)) {
                if (!wifi_up()) {
                    wifi_wait();
                }
                app_request_tile_schedule_submit(app_get_active_tile());
                continue;
            }
        }

    }
    return 0;
}
