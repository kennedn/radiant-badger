#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lwip/err.h"

typedef enum {
    REQUEST_TYPE_BOILER = 0,
    REQUEST_TYPE_RADIATOR = 1,
    REQUEST_TYPE_RADIATOR_BATTERY = 2,
    REQUEST_TYPE_BOILER_SCHEDULE = 3,
    REQUEST_TYPE_RESTFUL = 4
} HTTP_REQUEST_TYPE;


typedef void (*http_callback_t)(char *key, char *value, int response_code, void *arg);
void http_request(const char *url, const char *endpoint, const char *method, const char *json_body, HTTP_REQUEST_TYPE request_type, http_callback_t callback, void *arg, const char **keys, size_t keys_count);
uint8_t http_active_request_count(void);