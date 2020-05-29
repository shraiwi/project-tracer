/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <sys/dir.h>
#include <sys/dirent.h>
#include <sys/time.h>

#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_spiffs.h"

#include <driver/adc.h>

#include "stdio.h"
#include "time.h"

#include "ble_adapter.h"
#include "storage.h"
#include "tracer.h"
#include "cvec.h"

#define LED_BUILTIN         2
#define PHOTOCELL_PIN       34
#define WATCHDOG_TIMEOUT    1000
#define SPIFFS_ROOT         "/spiffs"

tracer_datapair * scanned_data = NULL;

int64_t get_micros() {
    return esp_timer_get_time();
}

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

void scan_for_peers(uint32_t epoch) {
    uint32_t enin = tracer_en_interval_number(epoch);

    scanned_data = cvec_arrayof(tracer_datapair);

    ble_adapter_start_scanning();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ble_adapter_stop_scanning();

    cvec_crunch(scanned_data);  // free up unused memory

    char * fname = b64_encode(&enin, sizeof(enin));

    fname = realloc(fname, sizeof(SPIFFS_ROOT) + 1 + strlen(fname));

    memmove(fname + sizeof(SPIFFS_ROOT), fname, strlen(fname) + 1);
    memcpy(fname, SPIFFS_ROOT, sizeof(SPIFFS_ROOT)-1);
    fname[sizeof(SPIFFS_ROOT)-1] = '/';

    printf("writing to %s...\n", fname);

    FILE * scan_record = fopen(fname, "w");

    free(fname);

    if (scan_record == NULL) {
        printf("\tfile null!\n");

        fclose(scan_record);
        cvec_free(scanned_data);

        return;
    }

    for (size_t i = 0; i < cvec_sizeof(scanned_data); i++) {
        printf("peer %d\n\trpi: ", i);
        print_hex_buffer(scanned_data[i].rpi.value, sizeof(scanned_data[i].rpi.value));
        printf("\n\taem: ");
        print_hex_buffer(scanned_data[i].aem.value, sizeof(scanned_data[i].aem.value));
        printf("\n");
        fwrite(&scanned_data[i], sizeof(tracer_datapair), 1, scan_record);
        //if (tracer_verify(scanned_data[i], match_tek, NULL)) {
        //    printf("\tmatches test tek!!\n");
        //}
    }

    fclose(scan_record);

    cvec_free(scanned_data);
}

void delete_old_enins(uint32_t epoch) {

    uint32_t enin = tracer_en_interval_number(epoch);

    printf("scanning files...\n");

    DIR * root_dir = opendir(SPIFFS_ROOT);
    struct dirent * de;

    while ((de = readdir(root_dir)) != NULL) {
        uint32_t * file_enin = b64_decode(de->d_name);
        printf("\tfile: %s\n\tderived enin: %u\n", de->d_name, *file_enin);
        free(file_enin);
    }

    closedir(root_dir);
}

void sample_adc() {
    printf("sampling adc for 2000 samples...\n");
    for (size_t i = 0; i < 2000; i++) {
        int64_t next_tick = get_micros() + 1000;
        printf("%d\n", adc1_get_raw(ADC1_CHANNEL_6));
        while (get_micros() < next_tick) {  }
    }
}

void app_main(void)
{
    printf("esp booted!\n");

    ESP_ERROR_CHECK(nvs_flash_init());

    gpio_pad_select_gpio(LED_BUILTIN);                      // setup builtin led
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);      // set led as an output

    adc1_config_width(ADC_WIDTH_12Bit);                     // set adc to 12 bits
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_11db);  // set attenuation to -11dB (0-3v)

    //struct timeval tv;

    //tv.tv_sec = 3082;

    //settimeofday(&tv, NULL);

    // initialize spiffs

    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = SPIFFS_ROOT,
        .partition_label = NULL,
        .max_files = 2,
        .format_if_mount_failed = true,
    };

    //ESP_ERROR_CHECK(esp_spiffs_format(spiffs_conf.partition_label));

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&spiffs_conf));

    size_t total = 0, used = 0;

    ESP_ERROR_CHECK(esp_spiffs_info(spiffs_conf.partition_label, &total, &used));

    printf("spiffs used %d/%d bytes.\n", used, total);

    // randomly generate a new mac address

    uint8_t * random_mac = rng_gen(6, NULL);

    printf("setting new random mac: ");
    print_hex_buffer(random_mac, 6);
    printf("\n");
    
    ble_adapter_set_mac(random_mac);

    free(random_mac);

    // initialize ble adapter

    ble_adapter_init();

    ble_adapter_register_scan_callback(&scan_cb);

    // scan for peers

    uint32_t epoch = get_epoch();

    scan_for_peers(epoch);

    // generate ble payload

    tracer_tek * tek = tracer_derive_tek(epoch);

    tracer_datapair pair = tracer_derive_datapair(epoch, ble_adapter_get_adv_tx_power());

    tracer_ble_payload payload = tracer_derive_ble_payload(pair);

    ble_adapter_add_raw(payload.value, payload.len);

    // testing stuff

    delete_old_enins(epoch);

    /*char * b64 = b64_encode("hi!", sizeof("hi!"));

    printf("hi! => %s\n", b64);

    free(b64);

    b64 = (char *)b64_decode("aGkhAA==");

    printf("aGkhAA== => %s\n", b64);

    free(b64);*/

    sample_adc();

    // advertising loop
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
