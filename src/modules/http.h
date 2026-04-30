#pragma once

#include "lwip/err.h"

typedef enum {
    REQUEST_TYPE_BOILER = 0,
    REQUEST_TYPE_RADIATOR = 1
} HTTP_REQUEST_TYPE;

typedef struct {
    char current[32];
    char target[32];
    char mode[8];
} HTTP_TEMPERATURE_RESULT;

typedef void (*http_callback_t)(void *result, int response_code, void *arg);
void http_request(const char *url, const char *endpoint, const char *method, const char *json_body, HTTP_REQUEST_TYPE request_type, http_callback_t callback, void *arg);