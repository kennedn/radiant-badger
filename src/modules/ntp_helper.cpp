#include "modules/ntp_helper.h"

#include <time.h>

extern "C" {
#include "modules/ntp.h"
#include "badger.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
}

#include "libraries/badger2040w/badger2040w.hpp"
#include "drivers/pcf85063a/pcf85063a.hpp"

extern pimoroni::Badger2040W badger;

namespace {

static volatile bool ntp_time_set = false;

static bool datetime_is_sane(datetime_t *datetime) {
    if (datetime->year < 0 || datetime->year > 4095) { return false; }
    if (datetime->month < 1 || datetime->month > 12) { return false; }
    if (datetime->day < 1 || datetime->day > 31) { return false; }
    if (datetime->dotw < 0 || datetime->dotw > 6) { return false; }
    if (datetime->hour < 0 || datetime->hour > 23) { return false; }
    if (datetime->min < 0 || datetime->min > 59) { return false; }
    if (datetime->sec < 0 || datetime->sec > 59) { return false; }
    return true;
}

static bool ntp_rtc_set_datetime() {
    datetime_t datetime = badger.pcf85063a->get_datetime();
    if (!datetime_is_sane(&datetime)) {
        return false;
    }
    rtc_set_datetime(&datetime);
    return true;
}

static void ntp_callback(datetime_t *datetime, void *arg) {
    (void)arg;
    if (datetime == NULL) {
        DEBUG_PRINTF("Failed to get time from NTP, attempting to fallback to RTC\n");
        ntp_rtc_set_datetime();
        ntp_time_set = true;
        return;
    }

    char dt_string[128];
    datetime_to_str(dt_string, sizeof(dt_string), datetime);
    DEBUG_PRINTF("Got datetime from NTP: %s\n", dt_string);
    badger.pcf85063a->set_datetime(datetime);
    rtc_set_datetime(datetime);
    DEBUG_PRINTF("Setting daily NTP time alarm\n");
    ntp_time_set = true;
}

} // namespace

void ntp_helper_rearm_rtc_timer(void) {
    // Set RTC interrupt to wake up after 15 minutes.
    badger.pcf85063a->set_timer(15, pimoroni::PCF85063A::TimerTickPeriod::TIMER_TICK_1_OVER_60HZ);
    badger.pcf85063a->enable_timer_interrupt(true);
    badger.pcf85063a->clear_timer_flag();
}

void ntp_helper_retrieve_time(bool from_ntp) {
    ntp_time_set = false;

    if (from_ntp) {
        DEBUG_PRINTF("Retrieving time from NTP\n");
        ntp_get_time(ntp_callback, NULL);
        while (!ntp_time_set) {
            tight_loop_contents();
        }
        return;
    }

    DEBUG_PRINTF("Retrieving time from RTC\n");
    if (!ntp_rtc_set_datetime()) {
        DEBUG_PRINTF("Failed to get time from RTC, triggering NTP retrieval\n");
        ntp_helper_retrieve_time(true);
    }
}