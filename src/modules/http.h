#pragma once

#include "lwip/err.h"

typedef enum {
    REQUEST_TYPE_BOILER = 0,
    REQUEST_TYPE_RADIATOR = 1,
    REQUEST_TYPE_RADIATOR_BATTERY = 2,
    REQUEST_TYPE_BOILER_SCHEDULE = 3,
    REQUEST_TYPE_RESTFUL = 4
} HTTP_REQUEST_TYPE;

typedef struct {
    char current[32];
    char target[32];
    char mode[32];
    char battery[32];
    char boost[32];
    char schedule[32];
    char value[128];
} HTTP_REQUEST_RESULT;

typedef void (*http_callback_t)(void *result, int response_code, void *arg);
void http_request(const char *url, const char *endpoint, const char *method, const char *json_body, HTTP_REQUEST_TYPE request_type, http_callback_t callback, void *arg);
void http_request_with_key(const char *url, const char *endpoint, const char *method, const char *json_body, HTTP_REQUEST_TYPE request_type, http_callback_t callback, void *arg, const char *extract_key);