#include "utils.h"
#include "wifi_adapter.h"

#include "esp_wifi.h"
#include "esp_tls.h"
#include "esp_http_server.h"
#include "esp_https_server.h"

#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "time.h"

#ifndef _HTTP_SERVER_H_
#define _HTTP_SERVER_H_

#define TAG "http_server_lib"

httpd_handle_t http_server = NULL;

// registers a handle to a request
void http_server_onrequest(httpd_method_t method, const char * path, esp_err_t (*callback)(httpd_req_t * req), void * context) {
    httpd_uri_t handler;
    handler.method = method;
    handler.uri = path;
    handler.handler = callback;
    handler.user_ctx = context;

    ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &handler));
}

// unregisters a uri handler
void http_server_delete_request(const char * path) {
    ESP_ERROR_CHECK(httpd_unregister_uri(http_server, path));
}

void http_server_begin() {
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();

    ESP_LOGI(TAG, "starting server...");
    ESP_ERROR_CHECK(httpd_start(&http_server, &server_config));

}

void http_server_stop() {
    httpd_stop(http_server);
    http_server = NULL;
}

#undef TAG

#endif