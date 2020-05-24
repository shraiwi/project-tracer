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

#include "stdio.h"
#include "time.h"

#include "ble_adapter.h"
#include "storage.h"
#include "tracer.h"

#define LED_BUILTIN         2
#define WATCHDOG_TIMEOUT    1000

// teehee
#define STOOPID_DEBUG() printf("got to line %d!\n", __LINE__ + 1)

void print_hex_buffer(void * data, size_t data_len) {
    for (int i = 0; i < data_len; i++) {
        printf("%02x", ((uint8_t*)data)[i]);
    }
}

// gets the unix epoch time
uint32_t get_epoch() {
    time_t out;
    time(&out);
    return out;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    gpio_pad_select_gpio(LED_BUILTIN);                      // setup builtin led
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);      // set led as an output

    printf("esp booted!\n");

    /*storage_file name = storage_open("bootfile");
    
    if (name.data == NULL) {
        printf("new file created\n");

        char fstring[] = "hello world!";

        name.data = fstring;
        name.data_len = sizeof(fstring);

    } else printf("boot data: %s\n", (char *)name.data);

    storage_close(name);*/

    uint8_t * new_mac = rng_gen(6, NULL);

    ble_adapter_set_mac(new_mac);
    printf("mac address set to: ");
    print_hex_buffer(new_mac, 6);
    printf("\n");

    free(new_mac);

    ble_adapter_init();

    uint8_t flags[] = { 0x1a };
    uint8_t uuid[]  = { 0x6f, 0xfd };
    uint8_t data[]  = { 0x6f, 0xfd, 0xde, 0xad, 0xbe, 0xef };

    ble_adapter_add_record(0x01, flags, sizeof(flags));
    ble_adapter_add_record(0x02, uuid, sizeof(uuid));
    ble_adapter_add_record(0x16, data, sizeof(data));

    // main loop
    while (true) {
        
        uint32_t epoch = get_epoch();
        
        volatile tracer_tek * tek = tracer_derive_tek(epoch);
        
        volatile tracer_rpik rpik = tracer_derive_rpik(*tek);
        volatile tracer_aemk aemk = tracer_derive_aemk(*tek);

        volatile tracer_rpi rpi = tracer_derive_rpi(rpik, epoch);

        volatile tracer_metadata meta = tracer_derive_metadata(ble_adapter_get_adv_tx_power());
        volatile tracer_aem aem = tracer_derive_aem(aemk, rpi, meta);

        volatile tracer_ble_payload payload = tracer_derive_ble_payload(rpi, aem);

        //printf("free RAM: %d\n", esp_get_free_heap_size());

        gpio_set_level(LED_BUILTIN, 1);                             // turn builtin led on
        ble_adapter_start_advertising();                            // start advertising
        vTaskDelay(10 / portTICK_PERIOD_MS);                        // wait for 10ms (1 RTOS tick)
        ble_adapter_stop_advertising();                             // stop advertising
        gpio_set_level(LED_BUILTIN, 0);                             // turn off builtin led
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(1000*1000));  // enable wakeup in 1 second
        ESP_ERROR_CHECK(esp_light_sleep_start());                   // start sleeping
    }
    
}
