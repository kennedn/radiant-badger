#pragma once
#define DEBUG_PRINTF(...) \
  do { if (DEBUG_PRINT > 0) printf(__VA_ARGS__); } while (0)

#include <stdbool.h>

#define HALT_TIMEOUT_MS 60000
#define MULTI_CLICK_WAIT_MS 350
#define WIFI_CONNECT_ATTEMPTS 3

bool wifi_up();