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

typedef struct {
    uint8_t * adv_data;
    uint8_t adv_data_len;
    uint8_t mac_addr[6];
    int8_t rssi;
} ble_adapter_scan_result;

static esp_ble_adv_params_t ble_adapter_adv_params = {
    .adv_int_min = 0x1e0,
	.adv_int_max = 0x1e0,
	.adv_type = ADV_TYPE_NONCONN_IND,
	.own_addr_type  = BLE_ADDR_TYPE_RANDOM,
	.channel_map = ADV_CHNL_ALL,
	.adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_scan_params_t ble_adapter_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x10,
    .scan_window            = 0x10
};

uint8_t ble_adapter_adv_data[ESP_BLE_ADV_DATA_LEN_MAX];
uint8_t ble_adapter_adv_data_head = 0;
void (*ble_adapter_scan_cb)(ble_adapter_scan_result) = NULL;
bool ble_adapter_ready = false;

static void ble_adapter_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch(event) {
        case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
            ble_adapter_ready = true;
            break;

        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            //printf("advertising data set.\n");
            ble_adapter_ready = true;
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            ble_adapter_ready = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ble_adapter_ready = param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS;
            break;

        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            ble_adapter_ready = param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS;
            break;
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            ble_adapter_ready = (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
            break;
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            ble_adapter_ready = (param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS);
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (ble_adapter_scan_cb && param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
                ble_adapter_scan_result res;
                res.adv_data = param->scan_rst.ble_adv;
                res.adv_data_len = param->scan_rst.adv_data_len;
                memcpy(res.mac_addr, param->scan_rst.bda, sizeof(res.mac_addr));
                res.rssi = param->scan_rst.rssi;
                ble_adapter_scan_cb(res);
            } else if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
                ble_adapter_ready = true;
            }
        } break;
        default:
            break;
    }
}

void ble_adapter_wait_for_ready() {
    while (!ble_adapter_ready) { }  // this'll cause a watchdog reset if the adapter never returns to ready instead of blocking the main app thread
}

// initializes the bluetooth adapter.
void ble_adapter_init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));

    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_adapter_gap_cb));

    ble_adapter_ready = true;
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&ble_adapter_scan_params));
    ble_adapter_wait_for_ready();
}

void ble_adapter_clear_data() {
    ble_adapter_adv_data_head = 0;
    ble_adapter_ready = false;
}

void ble_adapter_update_data() {
    ble_adapter_ready = false;
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw(ble_adapter_adv_data, ble_adapter_adv_data_head));
    ble_adapter_wait_for_ready();
}

void ble_adapter_add_record(uint8_t type, void * data, size_t len) {
    ble_adapter_adv_data[ble_adapter_adv_data_head++] = len + 1;
    ble_adapter_adv_data[ble_adapter_adv_data_head++] = type;
    memcpy(ble_adapter_adv_data + ble_adapter_adv_data_head, data, len);
    ble_adapter_adv_data_head += len;

    ble_adapter_update_data();
}

void ble_adapter_add_raw(void * data, size_t len) {
    memcpy(ble_adapter_adv_data + ble_adapter_adv_data_head, data, len);
    ble_adapter_adv_data_head += len;

    ble_adapter_update_data();
}

void ble_adapter_set_raw(void * data, size_t len) {
    memcpy(ble_adapter_adv_data, data, len);
    ble_adapter_adv_data_head = len;

    ble_adapter_update_data();
}

void ble_adapter_add_long(uint8_t type, int data, int len) {
    ble_adapter_adv_data[ble_adapter_adv_data_head++] = len + 1;
    ble_adapter_adv_data[ble_adapter_adv_data_head++] = type;
    memcpy(&ble_adapter_adv_data[ble_adapter_adv_data_head], &data, len);
    ble_adapter_adv_data_head += len;

    ble_adapter_update_data();
}

void ble_adapter_start_advertising() {
    if (!ble_adapter_ready) return;
    ble_adapter_ready = false;
    ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&ble_adapter_adv_params));
    ble_adapter_wait_for_ready();
    
}

void ble_adapter_stop_advertising() {
    if (!ble_adapter_ready) return;
    ble_adapter_ready = false;
    ESP_ERROR_CHECK(esp_ble_gap_stop_advertising());
    ble_adapter_wait_for_ready();
}

void ble_adapter_register_scan_callback(void (*cb)(ble_adapter_scan_result)) {
    ble_adapter_scan_cb = cb;
}

void ble_adapter_start_scanning() {
    if (!ble_adapter_ready) return;
    ble_adapter_ready = false;
    ESP_ERROR_CHECK(esp_ble_gap_start_scanning(0));
    ble_adapter_wait_for_ready();
}

void ble_adapter_stop_scanning() {
    if (!ble_adapter_ready) return;
    ble_adapter_ready = false;
    ESP_ERROR_CHECK(esp_ble_gap_stop_scanning());
    ble_adapter_wait_for_ready();
}

void ble_adapter_set_rand_mac(uint8_t data[6]) {
    if (!ble_adapter_ready) return;
    ble_adapter_ready = false;
    ESP_ERROR_CHECK(esp_ble_gap_set_rand_addr(data));
    ble_adapter_wait_for_ready();
}

void ble_adapter_set_adv_tx_power(int8_t dbm) {
    esp_power_level_t pwr = dbm / 3 + 4;
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, pwr));
}

int8_t ble_adapter_get_adv_tx_power() {
    return (esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV) - 4) * 3;
}

#endif