/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"

#include "stdio.h"
#include "time.h"

#include "ble_adapter.h"
#include "storage.h"
#include "tracer.h"
#include "cvec.h"

#define LED_BUILTIN         2
#define WATCHDOG_TIMEOUT    1000

tracer_datapair * scanned_data = NULL;

// gets the unix epoch time
uint32_t get_epoch() {
    time_t out;
    time(&out);
    return out;
}

void scan_cb(ble_adapter_scan_result res) {
    tracer_ble_payload payload;
    payload.len = res.adv_data_len;
    memcpy(payload.value, res.adv_data, payload.len);
    
    tracer_datapair pair;

    if (tracer_parse_ble_payload(payload, &pair)) {
        bool rpi_exists = false;

        for (size_t i = 0; i < cvec_sizeof(scanned_data); i++) {
            rpi_exists |= tracer_compare_datapairs(pair, scanned_data[i]);
            if (rpi_exists) break;
        }

        if (!rpi_exists) cvec_append(scanned_data, pair);
    }
}

void scan_for_peers() {
    scanned_data = cvec_arrayof(tracer_datapair);

    ble_adapter_start_scanning();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ble_adapter_stop_scanning();

    cvec_crunch(scanned_data);  // free up unused memory

    //tracer_tek match_tek;
    //uint8_t test_tek[] = {0x4c, 0x22, 0xbd, 0xa7, 0x59, 0x94, 0x2e, 0x07, 0x58, 0xe4, 0x79, 0x22, 0xed, 0x4d, 0x6c, 0x3e};
    //memcpy(match_tek.value, test_tek, sizeof(match_tek.value));

    //storage_file saved_pairs = storage_open("saved_pairs");

    //cvec_convert(saved_pairs.data, tracer_record, saved_pairs.data_len);

    for (size_t i = 0; i < cvec_sizeof(scanned_data); i++) {
        //cvec_append(saved_pairs, scanned_data[i]);
        printf("peer %d\n\trpi: ", i);
        print_hex_buffer(scanned_data[i].rpi.value, sizeof(scanned_data[i].rpi.value));
        printf("\n\taem: ");
        print_hex_buffer(scanned_data[i].aem.value, sizeof(scanned_data[i].aem.value));
        printf("\n");
        //if (tracer_verify(scanned_data[i], match_tek, NULL)) {
        //    printf("\tmatches test tek!!\n");
        //}
    }

    cvec_free(scanned_data);
}

void app_main(void)
{
    printf("esp booted!\n");

    ESP_ERROR_CHECK(nvs_flash_init());

    gpio_pad_select_gpio(LED_BUILTIN);                      // setup builtin led
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);      // set led as an output

    uint8_t * random_mac = rng_gen(6, NULL);

    printf("setting new random mac: ");
    print_hex_buffer(random_mac, 6);
    printf("\n");
    
    ble_adapter_set_mac(random_mac);

    free(random_mac);

    ble_adapter_init();

    ble_adapter_register_scan_callback(&scan_cb);

    scan_for_peers();

    uint32_t epoch = get_epoch();

    tracer_tek * tek = tracer_derive_tek(epoch);

    tracer_datapair pair = tracer_derive_datapair(epoch, ble_adapter_get_adv_tx_power());

    tracer_ble_payload payload = tracer_derive_ble_payload(pair);

    ble_adapter_add_raw(payload.value, payload.len);

    // main loop
    while (true) {
        gpio_set_level(LED_BUILTIN, 1);                             // turn builtin led on
        ble_adapter_start_advertising();                            // start advertising
        vTaskDelay(10 / portTICK_PERIOD_MS);                        // wait for 10ms (1 RTOS tick)
        ble_adapter_stop_advertising();                             // stop advertising
        gpio_set_level(LED_BUILTIN, 0);                             // turn off builtin led
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(1000*1000));  // enable wakeup in 1 second
        ESP_ERROR_CHECK(esp_light_sleep_start());                   // start sleeping
    }
    
}
