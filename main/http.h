#include "utils.h"
#include "wifi_adapter.h"

#include "esp_wifi.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#ifndef _HTTP_H
#define _HTTP_H

// performs an http GET request at the given server and address.
void http_get(const char * server, const char * port, const char * path) {
    if (!wifi_adapter_get_flag(WIFI_ADAPTER_CONNECTED_FLAG)) return;
    char * http_request_str = http_get_request_string(server, port, path);

}

char * http_get_request_string(const char * server, const char * port, const char * path) {
    char * out = NULL;
    http_string_append(out, "GET ");
    http_string_append(out, path);
    http_string_append(out, " HTTP/1.0\r\nHost: ");
    http_string_append(out, server);
    http_string_append(out, ":");
    http_string_append(out, port);
    http_string_append(out, "\r\nUser-Agent:\r\n\r\n");
    return out;
}

void http_string_append(char * src_str, const char * str_b) {
    realloc(src_str, strlen(src_str) + strlen(str_b) + 1);
    strcat(src_str, str_b);
}

#endif