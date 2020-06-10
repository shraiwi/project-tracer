#include "utils.h"
#include "wifi_adapter.h"

#include "esp_wifi.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_tls.h"

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

// ternimates an http header
void http_terminate_header(char ** src) {
    *src = realloc(*src, strlen(*src) + 3);
    strcat(*src, "\r\n");
}

// adds a header to an http header string
void http_add_header(char ** src, const char * key, char * value) {
    *src = realloc(*src, strlen(*src) + strlen(key) + strlen(value) + 4);
    strcat(*src, key);
    strcat(*src, ":");
    strcat(*src, value);
    strcat(*src, "\r\n");
}

// generates the first line of an http header. append subsequent headers using http_add_header()
char * http_gen_header(const char * method, const char * url) {
    const char * fmt = "%s %s HTTP/1.0\r\n";
    size_t len = snprintf(NULL, 0, fmt, method, url) + 1;
    char * out = malloc(len);
    snprintf(out, len, fmt, method, url);
    return out;
}

// gets an http header string. be sure to free this after use!
char * http_get_header(const char * method, const char * url, const char * server) {
    const char * fmt = "%s %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent:\r\n"
        "\r\n";
    size_t len = snprintf(NULL, 0, fmt, method, url, server) + 1;
    char * out = malloc(len);
    snprintf(out, len, fmt, method, url, server);

    return out;
}

// performs an http request at the given server and address. assumes all body data is binary.
int http_req(const char * method, const char * server, const char * url, char * body, size_t body_len, void (*callback)(char * data, size_t head, void * user_data), void * user_data) {

    if (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG) || !callback) return;

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };

    struct addrinfo *res;
    struct in_addr *addr;
    int http_socket, bytes_read;
    char recv_block[DATA_BLOCK_SIZE];

    ESP_LOGD(TAG, "performing dns lookup");
    int err = getaddrinfo(server, "80", &hints, &res);

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
    char * header_str = http_gen_header(method, url);
    http_add_header(&header_str, "Host", server);
    http_add_header(&header_str, "User-Agent", "");

    if (body_len && body) {
        char strbuf[16];
        sprintf(strbuf, "%u", body_len);
        http_add_header(&header_str, "Content-Length", strbuf);
        http_add_header(&header_str, "Content-Type", "application/octet-stream");
    }

    http_terminate_header(&header_str);

    if (write(http_socket, header_str, strlen(header_str)) < 0) {
        ESP_LOGE(TAG, "failed to send headers");
        close(http_socket);
        free(header_str);
        return -1;
    }

    free(header_str);
    
    if (body && body_len) {
        if (write(http_socket, body, body_len) < 0) {
            ESP_LOGE(TAG, "failed to send body.");
            close(http_socket);
            return -1;
        }
    }

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
        callback(recv_block, head, user_data);
        head += bytes_read;
    } while (bytes_read > 0);

    ESP_LOGD(TAG, "http GET request complete.");
    close(http_socket);
    return 0;
}

// performs an http request given an ip
int http_req_ip(const char * method, const char * server, const char * url, char * body, size_t body_len, void (*callback)(char * data, size_t head, void * user_data), void * user_data) {

    if (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG) || !callback) return;

    struct sockaddr_in addr = {0};
    int http_socket, bytes_read;
    char recv_block[DATA_BLOCK_SIZE];

    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = inet_addr(server);

    ESP_LOGD(TAG, "allocating socket");
    http_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (http_socket < 0) {
        ESP_LOGE(TAG, "failed to allocate socket");
        return -1;
    }

    ESP_LOGD(TAG, "attempting socket connect");
    if (connect(http_socket, (struct sockaddr*)&addr, sizeof(addr))) {
        ESP_LOGE(TAG, "socket connect failed with code %d", errno);
        close(http_socket);
        return -1;
    }

    ESP_LOGI(TAG, "connected, sending data...");

    ESP_LOGD(TAG, "getting request string");
    char * header_str = http_gen_header(method, url);
    http_add_header(&header_str, "Host", server);
    http_add_header(&header_str, "User-Agent", "");

    if (body_len && body) {
        char strbuf[16];
        sprintf(strbuf, "%u", body_len);
        http_add_header(&header_str, "Content-Length", strbuf);
        http_add_header(&header_str, "Content-Type", "application/octet-stream");
    }

    http_terminate_header(&header_str);

    if (write(http_socket, header_str, strlen(header_str)) < 0) {
        ESP_LOGE(TAG, "failed to send headers");
        close(http_socket);
        free(header_str);
        return -1;
    }

    free(header_str);
    
    if (body && body_len) {
        if (write(http_socket, body, body_len) < 0) {
            ESP_LOGE(TAG, "failed to send body.");
            close(http_socket);
            return -1;
        }
    }

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
        callback(recv_block, head, user_data);
        head += bytes_read;
    } while (bytes_read > 0);

    ESP_LOGD(TAG, "http GET request complete.");
    close(http_socket);
    return 0;
}

// performs an https transaction at the given server
int https_req(const char * method, const char * server, const char * url, char * body, size_t body_len, void * pem_cert, void (*callback)(char data[DATA_BLOCK_SIZE], size_t length, void * user_data), void * user_data) {
    char recv_buf[DATA_BLOCK_SIZE];
    int ret, len;

    esp_tls_cfg_t tls_cfg = {
        .cacert_pem_buf =   (const unsigned char*)pem_cert,
        .cacert_pem_bytes = strlen((const char *)pem_cert)+1,
    };

    struct esp_tls * tls = esp_tls_conn_http_new(url, &tls_cfg);

    if (!tls) {
        ESP_LOGE(TAG, "https connection to %s failed.", url);
        goto https_get_free;
    }


    char * header_str = http_gen_header(method, url);
    http_add_header(&header_str, "Host", server);
    http_add_header(&header_str, "User-Agent", "");

    if (body_len && body) {
        char strbuf[16];
        sprintf(strbuf, "%u", body_len);
        http_add_header(&header_str, "Content-Length", strbuf);
        http_add_header(&header_str, "Content-Type", "application/octet-stream");
    }

    http_terminate_header(&header_str);

    size_t bytes_written = 0, header_len = strlen(header_str);

    ESP_LOGD(TAG, "http header: %s", header_str);

    do {
        ret = esp_tls_conn_write(tls, header_str + bytes_written, header_len - bytes_written);

        if (ret >= 0) {
            ESP_LOGI(TAG, "wrote %d bytes (header)", ret);
            bytes_written += ret;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "tls write error 0x%x", ret);
            goto https_get_free;
        }

    } while (bytes_written < header_len);


    if (body && body_len) {
        bytes_written = 0;

        do {
            ret = esp_tls_conn_write(tls, body + bytes_written, body_len - bytes_written);

            if (ret >= 0) {
                ESP_LOGI(TAG, "wrote %d bytes (body)", ret);
                bytes_written += ret;
            } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                ESP_LOGE(TAG, "tls write error 0x%x", ret);
                goto https_get_free;
            }

        } while (bytes_written < body_len);
    }

    do {
        len = DATA_BLOCK_SIZE - 1;
        memset(recv_buf, 0, DATA_BLOCK_SIZE);
        
        ret = esp_tls_conn_read(tls, recv_buf, len);

        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) continue;

        if (ret < 0) {
            ESP_LOGE(TAG, "tls read error -0x%x", -ret);
            break;
        }
        if (ret == 0) {
            ESP_LOGI(TAG, "tls connection closed.");
            break;
        }

        len = ret;
        
        callback(recv_buf, len, user_data);
    } while (true);


https_get_free:
    free(header_str);
    esp_tls_conn_delete(tls);
    return -1;
}

#undef TAG

#endif