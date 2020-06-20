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
#define WIFI_ADAPTER_SCANNING           GET_BIT(5)

#define TAG "wifi_adapter"

typedef enum {
    WIFI_ADAPTER_AP = WIFI_MODE_AP,
    WIFI_ADAPTER_STA = WIFI_MODE_STA,
    WIFI_ADAPTER_DUAL = WIFI_MODE_APSTA
} wifi_adapter_mode;

static uint8_t wifi_adapter_flags = 0;
bool wifi_adapter_eloop_created = false;

static void wifi_adapter_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "wifi connected!");
                CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG);
                SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG);
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "wifi disconnected!");
                SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG);
                CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG);
                CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP);
                break;
            
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "wifi station started, connecting to ap.");
                SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_READY_FLAG);
                //esp_wifi_connect();
                break;
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "softap initialized!");
                SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_SOFTAP_FLAG);
                break;

            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "wifi scan complete!");
                CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_SCANNING);
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
    if (!wifi_adapter_eloop_created) {
        tcpip_adapter_init();
        ESP_ERROR_CHECK(esp_event_loop_create_default());
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    if (!wifi_adapter_eloop_created) {
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_adapter_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_adapter_event_handler, NULL));
        wifi_adapter_eloop_created = true;
    }

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_wifi_set_mode((wifi_mode_t)mode));

    SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_READY_FLAG);
    //wifi_adapter_set_flag(WIFI_ADAPTER_READY_FLAG);

    ESP_LOGI(TAG, "initialized wifi adapter.");
}

// disables all other netifs except for the given one.
void _wifi_adapter_sole_netif(tcpip_adapter_if_t netif) {
    for (int i = 0; i < TCPIP_ADAPTER_IF_MAX; i++) {
        if (tcpip_adapter_is_netif_up(i)) ESP_ERROR_CHECK_WITHOUT_ABORT(tcpip_adapter_down(i)); // shut down all other network interfaces
    }
    ESP_ERROR_CHECK(tcpip_adapter_up(netif));   // boot up requested interface
}

// will attempt to safely stop the dhcps
void _wifi_adapter_stop_dhcps() {
    ESP_ERROR_CHECK_WITHOUT_ABORT(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
}

// sets the tcpip interface to a certain netif within the ESP32. this can be used to switch between certain network contexts in a dual-mode wifi configuration.
void wifi_adapter_set_netif(wifi_adapter_mode src) {
    tcpip_adapter_ip_info_t ip_info = { 0 };
    switch (src) {
        case WIFI_MODE_AP: {
            ESP_ERROR_CHECK_WITHOUT_ABORT(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
            //ESP_ERROR_CHECK_WITHOUT_ABORT(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));
            IP4_ADDR(&ip_info.ip, 10, 0, 0, 1);
            IP4_ADDR(&ip_info.gw, 10, 0, 0, 1);
            IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
            ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info));
            ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
            SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP);
            //_wifi_adapter_sole_netif(TCPIP_ADAPTER_IF_AP);
            break;
        }
        case WIFI_MODE_STA: {
            //ESP_ERROR_CHECK_WITHOUT_ABORT(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
            //ESP_ERROR_CHECK_WITHOUT_ABORT(tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA));
            //_wifi_adapter_sole_netif(TCPIP_ADAPTER_IF_STA);
            break;
        }
        case WIFI_MODE_APSTA: {
            ESP_LOGE(TAG, "cannot configure two interfaces to be active at the same time!");
            break;
        }
    }
}

void wifi_adapter_stop() {
    ESP_ERROR_CHECK(esp_wifi_stop());
}

void wifi_adapter_deinit() {
    ESP_ERROR_CHECK(esp_wifi_deinit());
    wifi_adapter_flags = 0;
}

// gets the number of connected devices in AP mode.
size_t wifi_adapter_get_num_connected() {
    wifi_sta_list_t sta_list;
    ESP_ERROR_CHECK(esp_wifi_ap_get_sta_list(&sta_list));
    return sta_list.num;
}

// checks if the wifi adapter has credentials saved in nvs
bool wifi_adapter_has_saved_credentials() {
    wifi_config_t cfg = { 0 };
    ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &cfg));
    return strlen((char*)cfg.sta.ssid) > 0;
}

// tries to connect to an ap given an ssid and password. use a NULL password if the network is open. use a NULL ssid if you do not want to update the target network's credentials.
void wifi_adapter_connect(const char * ssid, const char * pwd) {
    if (ssid != NULL) {
        ESP_LOGI(TAG, "attempting to connect to %s.", ssid);

        wifi_config_t wifi_config = { 0 };
        strncpy((char *)wifi_config.sta.ssid, ssid, 32);
        if (pwd) strncpy((char *)wifi_config.sta.password, pwd, 64);

        CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG);
        CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG);

        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    //while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG)) { vTaskDelay(50 / portTICK_PERIOD_MS); }
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
}

// starts a wifi scan.
void wifi_adapter_begin_scan() {
    ESP_LOGI(TAG, "starting scan...");

    wifi_config_t wifi_cfg = { 0 };

    wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
    SET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_SCANNING);
}

// stops a wifi scan.
void wifi_adapter_stop_scan() {
    ESP_LOGI(TAG, "stopping scan...");
    ESP_ERROR_CHECK(esp_wifi_scan_stop());
    CLEAR_FLAG(wifi_adapter_flags, WIFI_ADAPTER_SCANNING);
}

// gets a pointer to an array of scan data, given a pointer to a value indicating how many aps to allocate for. the pointer will be set to the number of APs returned.
wifi_ap_record_t * wifi_adapter_get_scan(uint16_t * len) {
    ESP_LOGI(TAG, "getting scan data...");

    //ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(len)); 

    wifi_ap_record_t * out = calloc((*len + 1) * sizeof(wifi_ap_record_t), 1);

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(len, out));

    return out;
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