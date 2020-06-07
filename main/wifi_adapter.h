#include "utils.h"

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#ifndef _WIFI_ADAPTER_H
#define _WIFI_ADAPTER_H

#define WIFI_ADAPTER_CHECK_READY() if (!wifi_adapter_get_flag(WIFI_ADAPTER_READY_FLAG)) { ESP_LOGE(TAG, "wifi adapter not ready!"); return; } static_assert(true, "")

// flags
#define WIFI_ADAPTER_READY_FLAG         GET_BIT(0)
#define WIFI_ADAPTER_CONNECTED_FLAG     GET_BIT(1)
#define WIFI_ADAPTER_CONNECT_FAIL_FLAG  GET_BIT(2)
#define WIFI_ADAPTER_SOFTAP_FLAG        GET_BIT(3)
#define WIFI_ADAPTER_HAS_IP             GET_BIT(4)

#define TAG "wifi_adapter"

typedef enum {
    WIFI_ADAPTER_AP = WIFI_MODE_AP,
    WIFI_ADAPTER_STA = WIFI_MODE_STA,
    WIFI_ADAPTER_DUAL = WIFI_MODE_APSTA
} wifi_adapter_mode;

static uint8_t wifi_adapter_flags = 0;

void wifi_adapter_set_flag(uint8_t flag) { wifi_adapter_flags |= flag; }
void wifi_adapter_clear_flag(uint8_t flag) { wifi_adapter_flags &= ~flag; }
bool wifi_adapter_get_flag(uint8_t flag) { return wifi_adapter_flags & flag; }

static void wifi_adapter_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGV(TAG, "wifi connected!");
                SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG);
                CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG);
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGV(TAG, "wifi disconnected!");
                CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG);
                CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP);
                break;
            
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "wifi station started, connecting to ap.");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "softap initialized!");
                wifi_adapter_set_flag(WIFI_ADAPTER_SOFTAP_FLAG);
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t * event = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "got ip: %s.", ip4addr_ntoa(&event->ip_info.ip));
                SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP);
                //wifi_adapter_set_flag(WIFI_ADAPTER_CONNECTED_FLAG);
                //wifi_adapter_clear_flag(WIFI_ADAPTER_CONNECT_FAIL_FLAG);
                break;
            }
        }
    }
}

void wifi_adapter_init(wifi_adapter_mode mode) {
    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_adapter_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_adapter_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode((wifi_mode_t)mode));

    SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_READY_FLAG);
    //wifi_adapter_set_flag(WIFI_ADAPTER_READY_FLAG);

    ESP_LOGI(TAG, "initialized wifi adapter.");
}

void wifi_adapter_set_server_src(wifi_adapter_mode src) {
    tcpip_adapter_ip_info_t ip_info = { 0 };
    switch (src) {
        case WIFI_MODE_AP: {
            ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
            IP4_ADDR(&ip_info.ip, 10, 0, 0, 1);
            IP4_ADDR(&ip_info.gw, 10, 0, 0, 1);
            IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
            ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info));
            ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
            SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP);
            break;
        }
        case WIFI_MODE_STA: {
            ESP_LOGE(TAG, "cannot set values when conencted to an ap!");
            break;
        }
        case WIFI_MODE_APSTA: {
            ESP_LOGE(TAG, "cannot configure two interfaces at the same time!");
            break;
        }
    }
}

void wifi_adapter_stop() {
    ESP_ERROR_CHECK(esp_wifi_stop());
}

// tries to connect to an ap given an ssid and password. use a NULL password if the network is open.
void wifi_adapter_try_connect(const char * ssid, const char * pwd) {
    ESP_LOGI(TAG, "attempting to connect to %s.", ssid);

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, 32);
    if (pwd) strncpy((char *)wifi_config.sta.password, pwd, 64);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
}

// tries to create an ap given an ssid and password. use a NULL password if the network should be open.
void wifi_adapter_begin_softap(const char * ssid, const char * pwd) {

    ESP_LOGI(TAG, "setting up softap with ssid %s", ssid);

    wifi_config_t wifi_config = { 0 };

    strncpy((char *)wifi_config.ap.ssid, ssid, 32);
    if (pwd) {
        strncpy((char *)wifi_config.ap.password, pwd, 64);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ESP_LOGW(TAG, "the softap is open!");
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_config.ap.max_connection = 1;

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
}

void wifi_adapter_disconnect() {
    if (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG)) {
        ESP_LOGE(TAG, "must be connected to a wifi network to disconnect!");
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_disconnect());
}

#undef TAG

#endif