#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "badger.h"
#include "http.h"
#include "restful.h"
#include "tiles.h"

static RESTFUL_REQUEST_DATA *restful_stage_request(RESTFUL_REQUEST *request, uint8_t stage) {
    if (!request) {
        return NULL;
    }

    switch (stage) {
        case 0:
            return request->action_request;
        case 1:
            return request->status_request;
        case 2:
            return request->battery_request;
        default:
            return NULL;
    }
}

static const char *restful_resolve_json_body(RESTFUL_REQUEST *request, RESTFUL_REQUEST_DATA *sub_request) {
    if (!request || !sub_request || !sub_request->json_body) {
        return "";
    }

    if (request->template_value >= 0 && strstr(sub_request->json_body, "%d") != NULL) {
        snprintf(request->resolved_json_body, sizeof(request->resolved_json_body), sub_request->json_body, request->template_value);
        return request->resolved_json_body;
    }

    return sub_request->json_body;
}

static bool restful_issue_stage(RESTFUL_REQUEST *request, uint8_t stage) {
    RESTFUL_REQUEST_DATA *sub_request = restful_stage_request(request, stage);
    if (!request || !sub_request) {
        return false;
    }

    request->stage = stage;
    request->active_request = sub_request;
    request->keys_count = sub_request->keys_count;
    request->keys_remaining = sub_request->keys_count;

    const char *json_body = restful_resolve_json_body(request, sub_request);
    http_request(request->base_url, sub_request->endpoint, sub_request->method, json_body, REQUEST_TYPE_RESTFUL, request->callback, request, (const char **)sub_request->keys, sub_request->keys_count);
    return true;
}

void restful_request(RESTFUL_REQUEST *request) {
    if (!request) {
        return;
    }

    request->stage = 0;
    request->active_request = NULL;
    request->keys_count = 0;
    request->keys_remaining = 0;
    restful_request_continue(request);
}

bool restful_request_continue(RESTFUL_REQUEST *request) {
    if (!request) {
        return false;
    }

    uint8_t next_stage = request->active_request ? (uint8_t)(request->stage + 1) : request->stage;
    while (next_stage < 3) {
        if (restful_issue_stage(request, next_stage)) {
            return true;
        }
        next_stage++;
    }

    request->active_request = NULL;
    request->keys_count = 0;
    request->keys_remaining = 0;
    return false;
}

RESTFUL_REQUEST *restful_make_request(void *tile, const char *base_url, RESTFUL_REQUEST_DATA *action_request, RESTFUL_REQUEST_DATA *status_request, RESTFUL_REQUEST_DATA *battery_request, int template_value, http_callback_t callback) {
    if (!base_url || !callback || (!action_request && !status_request && !battery_request)) {
        DEBUG_PRINTF("Required data in request was NULL\n");
        return NULL;
    }

    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)malloc(sizeof(RESTFUL_REQUEST));
    request->tile = tile;
    request->base_url = (char *)malloc(strlen(base_url) + 1);
    strcpy(request->base_url, base_url);
    request->action_request = action_request;
    request->status_request = status_request;
    request->battery_request = battery_request;
    request->active_request = NULL;
    request->keys_count = 0;
    request->keys_remaining = 0;
    request->visible_refresh_batch = false;
    request->visible_refresh_generation = 0;
    request->stage = 0;
    request->template_value = template_value;
    request->resolved_json_body[0] = '\0';
    request->callback = callback;

    return request;
}

void restful_free_request(RESTFUL_REQUEST *request) {
    if (!request) {
        return;
    }

    free(request->base_url);
    free(request);
}

RESTFUL_REQUEST_DATA *restful_make_request_data(char *method, char *endpoint, char *json_body) {
    if (!method || !endpoint || !json_body || !method[0] || !endpoint[0] || !json_body[0]) {
        return NULL;
    }

    RESTFUL_REQUEST_DATA *request_data = (RESTFUL_REQUEST_DATA *)malloc(sizeof(RESTFUL_REQUEST_DATA));
    request_data->method = method;
    request_data->endpoint = endpoint;
    request_data->json_body = json_body;
    request_data->keys = NULL;
    request_data->keys_count = 0;

    return request_data;
}

void restful_free_request_data(RESTFUL_REQUEST_DATA *request) {
    if (!request) {
        return;
    }

    free(request->method);
    free(request->endpoint);
    free(request->json_body);
    free(request);
}