#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

#include "memory.h"

#ifndef _BLE_ADAPTER_H_
#define _BLE_ADAPTER_H_

static esp_ble_adv_params_t ble_adapter_adv_params = {
    .adv_int_min = 320,
	.adv_int_max = 320,
	.adv_type = ADV_TYPE_NONCONN_IND,
	.own_addr_type  = BLE_ADDR_TYPE_PUBLIC,
	.channel_map = ADV_CHNL_ALL,
	.adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

uint8_t ble_adapter_adv_data[ESP_BLE_ADV_DATA_LEN_MAX];
uint8_t ble_adapter_adv_data_head = 0;

static void ble_adapter_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch(event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            printf("advertising data set.\n");
            esp_ble_gap_start_advertising(&ble_adapter_adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) printf("advertising started.\n");
            else printf("advertising start error! (code %d)\n", param->scan_start_cmpl.status);
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) printf("advertising stopped.\n");
            else printf("advertising stop error! (code %d)\n", param->scan_stop_cmpl.status);
            break;
        default:
            printf("unhandled event fired! (code %d)\n", event);
            break;
    }
}

void ble_adapter_init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);

    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_adapter_gap_cb));
}

void ble_adapter_clear_data() {
    ble_adapter_adv_data_head = 0;
}

void ble_adapter_add_record(uint8_t type, void * data, int len) {
    ble_adapter_adv_data[ble_adapter_adv_data_head++] = len + 1;
    ble_adapter_adv_data[ble_adapter_adv_data_head++] = type;
    memcpy(ble_adapter_adv_data + ble_adapter_adv_data_head, data, len);
    ble_adapter_adv_data_head += len;
}

void ble_adapter_add_raw(void * data, int len) {
    memcpy(ble_adapter_adv_data + ble_adapter_adv_data_head, data, len);
    ble_adapter_adv_data_head += len;
}

void ble_adapter_add_long(uint8_t type, int data, int len) {
    
    int num_bits = len*8;

    // reverse endianness
    /*uint64_t out = 0;
    uint8_t byte;
    for (int i = 0; i < num_bits; i += 8) {
        byte = (data >> i) & 0xff;
        out |=  byte << (num_bits - 8 - i);
    }*/

    //uint64_t out = data;

    ble_adapter_adv_data[ble_adapter_adv_data_head++] = len + 1;
    ble_adapter_adv_data[ble_adapter_adv_data_head++] = type;
    memcpy(&ble_adapter_adv_data[ble_adapter_adv_data_head], &data, len);
    ble_adapter_adv_data_head += len;
}

void ble_adapter_start_advertising() {
    esp_ble_gap_config_adv_data_raw(ble_adapter_adv_data, ble_adapter_adv_data_head);
}

void ble_adapter_stop_advertising() {
    esp_ble_gap_stop_advertising();
}

#endif