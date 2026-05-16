#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "http.h"

typedef struct RESTFUL_REQUEST_DATA_ {
    char *method;
    char *endpoint;
    char *json_body;
    char **keys;
    size_t keys_count;
} RESTFUL_REQUEST_DATA;

typedef struct RESTFUL_REQUEST_ {
    void *tile;
    char *base_url;
    RESTFUL_REQUEST_DATA *action_request;
    RESTFUL_REQUEST_DATA *status_request;
    RESTFUL_REQUEST_DATA *battery_request;
    RESTFUL_REQUEST_DATA *active_request;
    size_t keys_count;
    size_t keys_remaining;
    bool visible_refresh_batch;
    uint16_t visible_refresh_generation;
    uint8_t stage;
    int template_value;
    char resolved_json_body[128];
    http_callback_t callback;
} RESTFUL_REQUEST;

void restful_request(RESTFUL_REQUEST *request);
bool restful_request_continue(RESTFUL_REQUEST *request);
RESTFUL_REQUEST *restful_make_request(void *tile, const char *base_url, RESTFUL_REQUEST_DATA *action_request, RESTFUL_REQUEST_DATA *status_request, RESTFUL_REQUEST_DATA *battery_request, int template_value, http_callback_t callback);
RESTFUL_REQUEST_DATA *restful_make_request_data(char *method, char *endpoint, char *json_body);
void restful_free_request(RESTFUL_REQUEST *request);
void restful_free_request_data(RESTFUL_REQUEST_DATA *request);