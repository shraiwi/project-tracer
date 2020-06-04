#include "utils.h"
#include "wifi_adapter.h"

#include "esp_wifi.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "time.h"

#ifndef _HTTP_H
#define _HTTP_H

#define TAG "http_lib"

#define DATA_BLOCK_SIZE 64

char * http_string_append(char * src_str, const char * str_b) {
    src_str = realloc(src_str, strlen(src_str) + strlen(str_b) + 1);
    if (src_str == NULL) ESP_LOGE(TAG, "realloc() error! no more avaiable memory in the given block!");
    strcat(src_str, str_b);
    return (src_str);
}

char * http_get_request_string(const char * server, const char * port, const char * path) {
    char * out = calloc(1, 1);
    out = http_string_append(out, "GET ");
    out = http_string_append(out, path);
    out = http_string_append(out, " HTTP/1.0\r\nHost: ");
    out = http_string_append(out, server);
    out = http_string_append(out, ":");
    out = http_string_append(out, port);
    out = http_string_append(out, "\r\nUser-Agent:\r\n\r\n");
    return out;
}

// performs an http GET request at the given server and address.
int http_get(const char * server, const char * port, const char * path, void (*callback)(char * data, size_t head)) {

    if (!wifi_adapter_get_flag(WIFI_ADAPTER_CONNECTED_FLAG) || !callback) return;

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = 1,
    };

    struct addrinfo *res;
    struct in_addr *addr;
    int http_socket, bytes_read;
    char recv_block[DATA_BLOCK_SIZE];

    ESP_LOGD(TAG, "performing dns lookup");
    int err = getaddrinfo(server, port, &hints, &res);

    if (err || res == NULL) {
        ESP_LOGE(TAG, "dns lookup failed with code %d", err);
        return -1;
    }

    ESP_LOGD(TAG, "allocating socket");
    http_socket = socket(res->ai_family, res->ai_socktype, 0);
    if (http_socket < 0) {
        ESP_LOGE(TAG, "failed to allocate socket");
        freeaddrinfo(res);
        return -1;
    }

    ESP_LOGD(TAG, "attempting socket connect");
    if (connect(http_socket, res->ai_addr, res->ai_addrlen)) {
        ESP_LOGE(TAG, "socket connect failed with code %d", errno);
        close(http_socket);
        freeaddrinfo(res);
        return -1;
    }

    ESP_LOGI(TAG, "connected, sending data...");
    freeaddrinfo(res);

    ESP_LOGD(TAG, "getting request string");

    char * http_request_str = http_get_request_string(server, port, path);

    if (write(http_socket, http_request_str, strlen(http_request_str)) < 0) {
        ESP_LOGE(TAG, "failed to send request");
        close(http_socket);
        free(http_request_str);
        return -1;
    }
    
    free(http_request_str);

    struct timeval recv_timeout;
    recv_timeout.tv_sec = 5;
    recv_timeout.tv_usec = 0;

    ESP_LOGD(TAG, "setting timeout");
    if (setsockopt(http_socket, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0) {
        ESP_LOGE(TAG, "failed to set timeout");
        close(http_socket);
        return -1;
    }

    ESP_LOGD(TAG, "reading from socket");
    size_t head = 0;
    do {
        memset(recv_block, 0, DATA_BLOCK_SIZE);
        bytes_read = read(http_socket, recv_block, DATA_BLOCK_SIZE-1);
        callback(recv_block, head);
        head += bytes_read;
    } while (bytes_read > 0);

    ESP_LOGD(TAG, "http GET request complete.");
    close(http_socket);
    return 0;
}

#undef TAG

#endif