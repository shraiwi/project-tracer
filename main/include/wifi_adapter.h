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

inline void wifi_adapter_set_flag(uint8_t flag);
inline void wifi_adapter_clear_flag(uint8_t flag);
inline bool wifi_adapter_get_flag(uint8_t flag);

static void wifi_adapter_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data);
void wifi_adapter_init();
void wifi_adapter_try_connect(const char * ssid, const char * pwd);

#endif