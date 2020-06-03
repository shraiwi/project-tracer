#include "wifi_adapter.h"

static const char *TAG = "wifi_adapter";

static uint8_t wifi_adapter_flags = 0;

inline void wifi_adapter_set_flag(uint8_t flag) { wifi_adapter_flags |= flag; }
inline void wifi_adapter_clear_flag(uint8_t flag) { wifi_adapter_flags &= ~flag; }
inline bool wifi_adapter_get_flag(uint8_t flag) { return wifi_adapter_flags & flag; }

static void wifi_adapter_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "wifi connected!");
                wifi_adapter_set_flag(WIFI_ADAPTER_CONNECTED_FLAG);
                wifi_adapter_clear_flag(WIFI_ADAPTER_CONNECT_FAIL_FLAG);
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "wifi disconnected!");
                wifi_adapter_clear_flag(WIFI_ADAPTER_CONNECTED_FLAG);
                wifi_adapter_set_flag(WIFI_ADAPTER_CONNECT_FAIL_FLAG);
                break;
            
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "wifi station started, connecting to ap.");
                esp_wifi_connect();
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t * event = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(TAG, "got ip: %s.", ip4addr_ntoa(&event->ip_info.ip));
                wifi_adapter_set_flag(WIFI_ADAPTER_CONNECTED_FLAG);
                wifi_adapter_clear_flag(WIFI_ADAPTER_CONNECT_FAIL_FLAG);
        }
    }
}

void wifi_adapter_init() {
    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_adapter_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_adapter_event_handler, NULL));

    wifi_adapter_set_flag(WIFI_ADAPTER_READY_FLAG);

    ESP_LOGI(TAG, "initialized wifi adapter.");
}

void wifi_adapter_try_connect(const char * ssid, const char * pwd) {
    ESP_LOGI(TAG, "attempting to connect to %s.", ssid);

    wifi_config_t wifi_config = { 0 };
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, pwd);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}