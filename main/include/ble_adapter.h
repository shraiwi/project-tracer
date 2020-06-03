#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

#include "memory.h"

#ifndef _BLE_ADAPTER_H_
#define _BLE_ADAPTER_H_

#define BLE_ADAPTER_CHECK_READY() do { if (!ble_adapter_ready) { ESP_LOGE(TAG, "ble adapter not ready!"); return; } } while(0)

typedef struct {
    uint8_t * adv_data;
    uint8_t adv_data_len;
    uint8_t mac_addr[6];
    int8_t rssi;
} ble_adapter_scan_result;

void ble_adapter_wait_for_ready();

void ble_adapter_init();

void ble_adapter_clear_data();

void ble_adapter_update_data();

void ble_adapter_add_record(uint8_t type, void * data, size_t len);

void ble_adapter_add_raw(void * data, size_t len);

void ble_adapter_set_raw(void * data, size_t len);

void ble_adapter_start_advertising();

void ble_adapter_stop_advertising();

void ble_adapter_register_scan_callback(void (*cb)(ble_adapter_scan_result));

void ble_adapter_start_scanning();

void ble_adapter_stop_scanning();

void ble_adapter_set_rand_mac(uint8_t data[6]);

void ble_adapter_set_adv_tx_power(int8_t dbm);

int8_t ble_adapter_get_adv_tx_power();

#endif