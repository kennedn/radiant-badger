#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "restful.h"
#include "tiles.h"
#include "badger.h"

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
    RESTFUL_REQUEST_DATA *sub_request = request->status_request ? request->status_request : request->action_request;

    if (sub_request == NULL) {
        DEBUG_PRINTF("No requests present, nothing to do\n");
        return;
    }

    TILE *tile = (TILE *)request->tile;
    if (tile && (tile->type == TILE_TYPE_BOILER || tile->type == TILE_TYPE_RADIATOR)) {
        HTTP_REQUEST_TYPE req_type = (tile->type == TILE_TYPE_BOILER) ? REQUEST_TYPE_BOILER : REQUEST_TYPE_RADIATOR;
        http_request(request->base_url, sub_request->endpoint, sub_request->method, sub_request->json_body, req_type, restful_callback, request);
        return;
    }
}

RESTFUL_REQUEST *restful_make_request(void *tile, const char *base_url, RESTFUL_REQUEST_DATA *action_request, RESTFUL_REQUEST_DATA *status_request, restful_callback_t callback) {
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
    request->callback = callback;

    return request;
}

void restful_free_request(RESTFUL_REQUEST *request) {
    free(request->base_url);
    free(request);
}

RESTFUL_REQUEST_DATA *restful_make_request_data(char *method, char *endpoint, char *json_body) {
    if (!method || !endpoint || !json_body) {
        DEBUG_PRINTF("Required data in request data was NULL\n");
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
}