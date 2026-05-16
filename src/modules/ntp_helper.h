#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ntp_helper_rearm_rtc_timer(void);
void ntp_helper_retrieve_time(bool from_ntp);

#ifdef __cplusplus
}
#endif