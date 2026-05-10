#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "restful.h"
#include "tiles.h"
#include "badger.h"

static void restful_status_after_battery_callback(void *result, int response_code, void *arg);
static void restful_status_after_schedule_callback(void *result, int response_code, void *arg);
static void restful_status_after_action_callback(void *result, int response_code, void *arg);
static const char *restful_resolve_json_body(RESTFUL_REQUEST *request, RESTFUL_REQUEST_DATA *sub_request);

void restful_callback(void *result, int response_code, void *arg) {
    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    if (!request || !request->callback) {
        return;
    }
    request->callback(result, response_code, request);
}

void restful_request(RESTFUL_REQUEST *request) {
    if (!request) {
        return;
    }
    TILE *tile = (TILE *)request->tile;

    if (tile && tile->type == TILE_TYPE_RESTFUL) {
        if (request->action_request && request->status_request) {
            http_request(request->base_url, request->action_request->endpoint, request->action_request->method, request->action_request->json_body, REQUEST_TYPE_RESTFUL, restful_status_after_action_callback, request);
            return;
        }

        RESTFUL_REQUEST_DATA *sub_request = request->status_request ? request->status_request : request->action_request;
        if (!sub_request) {
            DEBUG_PRINTF("No requests present, nothing to do\n");
            return;
        }

        const char *json_body = restful_resolve_json_body(request, sub_request);
        http_request_with_key(request->base_url, sub_request->endpoint, sub_request->method, json_body, REQUEST_TYPE_RESTFUL, restful_callback, request, tile->status_key);
        return;
    }

    if (tile && tile->type == TILE_TYPE_BOILER && tile->schedule_status_request && request->status_request) {
        http_request(request->base_url, tile->schedule_status_request->endpoint, tile->schedule_status_request->method, tile->schedule_status_request->json_body, REQUEST_TYPE_BOILER_SCHEDULE, restful_status_after_schedule_callback, request);
        return;
    }

    if (tile && tile->type == TILE_TYPE_RADIATOR && request->battery_request && request->status_request) {
        http_request(request->base_url, request->battery_request->endpoint, request->battery_request->method, request->battery_request->json_body, REQUEST_TYPE_RADIATOR_BATTERY, restful_status_after_battery_callback, request);
        return;
    }

    RESTFUL_REQUEST_DATA *sub_request = request->status_request ? request->status_request : request->action_request;

    if (sub_request == NULL) {
        DEBUG_PRINTF("No requests present, nothing to do\n");
        return;
    }

    if (tile && (tile->type == TILE_TYPE_BOILER || tile->type == TILE_TYPE_RADIATOR)) {
        HTTP_REQUEST_TYPE req_type = (tile->type == TILE_TYPE_BOILER) ? REQUEST_TYPE_BOILER : REQUEST_TYPE_RADIATOR;
        const char *json_body = restful_resolve_json_body(request, sub_request);
        http_request(request->base_url, sub_request->endpoint, sub_request->method, json_body, req_type, restful_callback, request);
        return;
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

static void restful_status_after_action_callback(void *result, int response_code, void *arg) {
    (void)result;
    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    if (!request) {
        return;
    }

    TILE *tile = (TILE *)request->tile;
    if (!tile || tile->type != TILE_TYPE_RESTFUL) {
        return;
    }

    // Now fetch the status with the key extraction
    RESTFUL_REQUEST_DATA *sub_request = request->status_request;
    if (!sub_request) {
        if (request->callback) {
            request->callback(NULL, response_code, request);
        }
        return;
    }

    // Use http_request_with_key to extract the status value based on tile's status_key
    http_request_with_key(request->base_url, sub_request->endpoint, sub_request->method, sub_request->json_body, REQUEST_TYPE_RESTFUL, restful_callback, request, tile->status_key);
}

static void restful_status_after_battery_callback(void *result, int response_code, void *arg) {
    (void)result;
    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    if (!request) {
        return;
    }

    TILE *tile = (TILE *)request->tile;
    if (tile && tile->type == TILE_TYPE_RADIATOR) {
        if (response_code == 200 && result) {
            HTTP_REQUEST_RESULT *battery_result = (HTTP_REQUEST_RESULT *)result;
            DEBUG_PRINTF("restful: battery result='%s' for tile=%s\n", battery_result->battery, tile->name);
            free(tile->battery_value);
            tile->battery_value = (char *)malloc(strlen(battery_result->battery) + 1);
            strcpy(tile->battery_value, battery_result->battery);
        } else {
            free(tile->battery_value);
            tile->battery_value = NULL;
        }
    }

    RESTFUL_REQUEST_DATA *sub_request = request->status_request ? request->status_request : request->action_request;
    if (!sub_request) {
        if (request->callback) {
            request->callback(NULL, response_code, request);
        }
        return;
    }

    HTTP_REQUEST_TYPE req_type = (tile && tile->type == TILE_TYPE_BOILER) ? REQUEST_TYPE_BOILER : REQUEST_TYPE_RADIATOR;
    const char *json_body = restful_resolve_json_body(request, sub_request);
    http_request(request->base_url, sub_request->endpoint, sub_request->method, json_body, req_type, restful_callback, request);
}

static void restful_status_after_schedule_callback(void *result, int response_code, void *arg) {
    (void)result;
    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)arg;
    if (!request) {
        return;
    }

    TILE *tile = (TILE *)request->tile;
    if (tile && tile->type == TILE_TYPE_BOILER) {
        if (response_code == 200 && result) {
            HTTP_REQUEST_RESULT *schedule_result = (HTTP_REQUEST_RESULT *)result;
            DEBUG_PRINTF("restful: schedule result='%s' for tile=%s\n", schedule_result->schedule, tile->name);
            free(tile->schedule_status_value);
            tile->schedule_status_value = (char *)malloc(strlen(schedule_result->schedule) + 1);
            strcpy(tile->schedule_status_value, schedule_result->schedule);
        } else {
            free(tile->schedule_status_value);
            tile->schedule_status_value = NULL;
        }
    }

    RESTFUL_REQUEST_DATA *sub_request = request->status_request ? request->status_request : request->action_request;
    if (!sub_request) {
        if (request->callback) {
            request->callback(NULL, response_code, request);
        }
        return;
    }

    HTTP_REQUEST_TYPE req_type = REQUEST_TYPE_BOILER;
    const char *json_body = restful_resolve_json_body(request, sub_request);
    http_request(request->base_url, sub_request->endpoint, sub_request->method, json_body, req_type, restful_callback, request);
}

RESTFUL_REQUEST *restful_make_request(void *tile, const char *base_url, RESTFUL_REQUEST_DATA *action_request, RESTFUL_REQUEST_DATA *status_request, RESTFUL_REQUEST_DATA *battery_request, int template_value, restful_callback_t callback) {
    if (!base_url || !callback || (!action_request && !status_request)) {
        DEBUG_PRINTF("Required data in request was NULL\n");
        return NULL;
    }
    RESTFUL_REQUEST *request = (RESTFUL_REQUEST *)malloc(sizeof(RESTFUL_REQUEST));
    request->tile = tile;
    request->base_url = (char*) malloc(strlen(base_url) + 1 * sizeof(char));
    strcpy(request->base_url, base_url);
    request->action_request = action_request;
    request->status_request = status_request;
    request->battery_request = battery_request;
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