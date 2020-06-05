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
#include "esp32/ulp.h"

#include <driver/adc.h>
#include <driver/touch_pad.h>

#include "stdio.h"
#include "time.h"
#include "limits.h"

#include "passwords.h"      // this file is in .gitignore for me. you probably want to create your own passwords.h and input your own data.
#include "wifi_adapter.h"
#include "ble_adapter.h"
#include "storage.h"
#include "timesync.h"
#include "flasher.h"
#include "http.h"
#include "tracer.h"
#include "cvec.h"

#define LED_PIN             2
#define PHOTOCELL_PIN       34
#define TOUCH_PIN           33
#define SPIFFS_ROOT         "/spiffs"

static const char * TAG = "app_main";

tracer_datapair * scanned_data = NULL;
bool touch_wake = false;

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

        for (size_t i = 0; i < cvec_len(scanned_data); i++) {
            rpi_exists |= tracer_compare_datapairs(pair, scanned_data[i]);
            if (rpi_exists) break;
        }

        if (!rpi_exists) cvec_append(scanned_data, pair);
    }
}

void http_get_cb(char * data, size_t head) {
    printf("%s", data);
}

void scan_for_peers(uint32_t epoch, uint32_t ms) {
    uint32_t scanin = tracer_scan_interval_number(epoch);

    scanned_data = cvec_arrayof(tracer_datapair);

    ble_adapter_start_scanning();
    vTaskDelay(ms / portTICK_PERIOD_MS);
    ble_adapter_stop_scanning();

    cvec_crunch(scanned_data);  // free up unused memory

    char * fname = b64_encode(&scanin, sizeof(scanin));

    fname = realloc(fname, sizeof(SPIFFS_ROOT) + 1 + strlen(fname));

    memmove(fname + sizeof(SPIFFS_ROOT), fname, strlen(fname) + 1);
    memcpy(fname, SPIFFS_ROOT, sizeof(SPIFFS_ROOT)-1);
    fname[sizeof(SPIFFS_ROOT)-1] = '/';

    ESP_LOGD(TAG, "writing to %s...", fname);

    FILE * scan_record = fopen(fname, "w");

    free(fname);

    if (scan_record == NULL) {
        ESP_LOGE(TAG, "\tfile null!");

        fclose(scan_record);
        cvec_free(scanned_data);

        return;
    }

    ESP_LOGV(TAG, "found %d peers.", cvec_len(scanned_data));

    fwrite(scanned_data, cvec_sizeof(scanned_data), 1, scan_record);

    fclose(scan_record);

    cvec_free(scanned_data);
}

// saves the teks to spiffs.
void save_teks() {
    FILE * tek_file = fopen(SPIFFS_ROOT"/tek_file", "w");
    fwrite(tracer_tek_array, 1, sizeof(tracer_tek_array), tek_file);
    fclose(tek_file);
}

void delete_old_enins(uint32_t epoch) {

    uint32_t enin = tracer_en_interval_number(epoch);

    ESP_LOGD(TAG, "scanning files...");

    DIR * root_dir = opendir(SPIFFS_ROOT);
    struct dirent * de;

    while ((de = readdir(root_dir)) != NULL) {
        uint32_t * file_enin = b64_decode(de->d_name);
        ESP_LOGD(TAG, "\tfile: %s\n\tderived enin: %u", de->d_name, *file_enin);
        free(file_enin);
    }

    closedir(root_dir);
}

char * derive_light_data(size_t timeout) {
    //printf("sampling adc...\n");

    adc_power_on();

    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    ESP_ERROR_CHECK(esp_task_wdt_delete(idle_0));           // disable watchdog

    const int hist_range = 7;       // histogram range
    const int chunk_size = 7;       // 128ms
    const size_t time_buf_size = 5; // 32
    const uint16_t min_range = 200; 
    const uint16_t min_count = 4;

    uint16_t chunk_data[1<<chunk_size];
    uint16_t chunk_head = 0;
    uint16_t chunk_min = UINT16_MAX, chunk_max = 0;
    int8_t last_value = -1;

    size_t last_time = 0;
    size_t longest_bin = 0;

    size_t * times = cvec_arrayof(size_t);

    uint8_t * data = cvec_arrayof(uint8_t);

    size_t i = 0;

    char * packet_data = cvec_arrayof(char);

    typedef struct {
        int value, count;
    } histogram_bin;

    histogram_bin * bins = cvec_arrayof(histogram_bin);
    
    while (i < timeout) {
        int64_t next_tick = get_micros() + 1000;
        chunk_data[chunk_head] = adc1_get_raw(ADC1_CHANNEL_6);

        gpio_set_level(LED_PIN, 1 & (get_micros() >> 16));  // blink LED approximately every 125ms

        //printf("%d", chunk_data[])

        if (chunk_data[chunk_head] < chunk_min) chunk_min = chunk_data[chunk_head];
        if (chunk_data[chunk_head] > chunk_max) chunk_max = chunk_data[chunk_head];

        chunk_head++;

        // on chunk full,
        if (chunk_head > 1 << chunk_size) {
            chunk_head = 0;

            uint16_t chunk_threshold = (chunk_max - chunk_min) >> 1;
            
            // make sure wave amplitude is big enough
            if (chunk_threshold > min_range) {
                // iterate over the chunk
                for (uint16_t j = 0; j < 1 << chunk_size; j++) {

                    chunk_data[j] -= chunk_min;                     // shift the data down

                    bool val = chunk_data[j] > chunk_threshold;     // determine if the bit is high or low

                    if (last_value == -1) last_value = val;         // set initial value

                    // if there's a bit flip,
                    if (val != last_value) {
                        //printf("\tbit flip!\n");

                        size_t time = i - (1 << chunk_size) + j;   // calculate the delta time

                        if (last_time == 0) last_time = time;      // set initial values

                        size_t delta = time - last_time;

                        cvec_append(times, delta);

                        //printf("\tnum bins: %d\n", cvec_sizeof(bins));

                        if (cvec_len(bins) == 0) {
                            //printf("\tnew bin!\n");
                            histogram_bin dat;
                            dat.value = delta;
                            dat.count = 1;
                            cvec_append(bins, dat);
                            //printf("bin:\n\tvalue: %d\n\ttcount: %d\n", dat.value, dat.count);
                        }

                        size_t num_bins = cvec_len(bins);

                        bool matched_to_bin = false;

                        for (size_t k = 0; k < num_bins; k++) {
                            histogram_bin * bin = &bins[k];
                            if ((delta > (bin->value - hist_range)) && (delta < (bin->value + hist_range))) {
                                bin->value = (bin->value * 3 + delta) / 4;
                                bin->count++;
                                
                                matched_to_bin = true;
                                break;
                            }
                        }

                        if (!matched_to_bin) {
                            histogram_bin dat;
                            dat.value = delta;
                            dat.count = 1;
                            cvec_append(bins, dat);
                        }

                        last_time = time;                          // set last value
                    }

                    last_value = val;   // set last value
                }

            }
            chunk_min = UINT16_MAX; // reset chunk minima and maxima
            chunk_max = 0;
        }

        i++;

        while (get_micros() < next_tick) { }
    }

    adc_power_off();

    // sanity check to get rid of null data
    if (cvec_len(bins) == 0) {
        ESP_LOGW(TAG, "no valid data found!");
        goto ret_packet;
    }
    
    //printf("getting top bins...\n");

    // indices of the top 3 bins
    size_t top_bins[4] = { 0 };

    for (size_t i = 0; i < cvec_len(bins); i++) {
        histogram_bin * bin = &bins[i];

        //ESP_LOGI(TAG, "bin %d:\n\tvalue: %d\n\tcount: %d\n", i, bin->value, bin->count);

        for (size_t j = 0; j < 3; j++) {
            histogram_bin * cmp_bin = &bins[top_bins[j]];
            if (bin->count >= cmp_bin->count) {
                memmove(&top_bins[j+1], &top_bins[j], sizeof(top_bins) - sizeof(size_t) * j);
                top_bins[j] = i;
                break;
            }
        }
        //if (bins[i].count > 3) printf("bin:\n\tvalue: %d\n\tcount: %d\n", bin->value, bins[i].count);
    }

    //printf("ordering top bins...\n");

    // bit times, ordered by value
    size_t bit_times[4] = { SIZE_MAX };

    for (size_t i = 0; i < 3; i++) {
        histogram_bin * bin = &bins[top_bins[i]];
        //ESP_LOGI(TAG, "bin %d:\n\tvalue: %d\n\tcount: %d\n", i, bin->value, bin->count);
        for (size_t j = 0; j < 3; j++) {
            if (bin->value <= bit_times[j]) {
                memmove(&bit_times[j+1], &bit_times[j], sizeof(bit_times) - sizeof(size_t) * j);
                bit_times[j] = bin->value;
                break;
            }
        }
    }

    /*printf("bin values...\n");*/
    for (size_t i = 0; i < 3; i++) {
        ESP_LOGI(TAG, "bin %d:\n\tvalue: %d\n\tcount: %d\n", i, bins[bit_times[i]].value, bins[bit_times[i]].count);
        if (bins[bit_times[i]].count < min_count) {
            ESP_LOGW(TAG, "no valid data found!");
            goto ret_packet;
        }
    }

    //printf("light data:\n");
    for (size_t i = 0; i < cvec_len(times); i++) {
        size_t delta = times[i];
        for (size_t j = 0; j < 3; j++) {
            if (delta > (bit_times[j] - hist_range) && delta < (bit_times[j] + hist_range)) {
                //printf("\t%d\n", j);
                cvec_append(data, j);
                break;
            }
        }
    }

    size_t header_counter = 0;

    cvec_append(packet_data, 0);

    uint8_t bit_head = 0;

    for (size_t i = 1; i < cvec_len(data); i++) {
        //uint8_t last_bit = data[i-1];
        uint8_t bit = data[i];
        ESP_LOGI(TAG, "\tbit %d", bit);
        
        if (bit == 2) {
            if (cvec_len(packet_data) > 1) {
                ESP_LOGI(TAG, "transmission complete!");
                break;       // if a 2-bit is encountered when the data buffer is written to, assume that the transmission has ended.
            }
            header_counter++;
        } else { 
            // on packet,
            if (header_counter >= 6) {
                packet_data[cvec_len(packet_data)-1] |= bit << bit_head++;  // add data to the buffer
                if (bit_head == 8) {                                        // handle byte rollover
                    cvec_append(packet_data, 0);
                    bit_head = 0;
                }
            } else {
                header_counter = 0;
            }
        }
    }

    ret_packet:

    cvec_free(data);
    cvec_free(bins);
    cvec_free(times);

    char * out = calloc(1, cvec_sizeof(packet_data) + 1);
    memcpy(out, packet_data, cvec_sizeof(packet_data));

    cvec_free(packet_data);

    ESP_ERROR_CHECK(esp_task_wdt_add(idle_0));   // re-enable watchdog

    return out;
}

void test_light_comms(int64_t time) {
    
    int64_t timeout = get_micros() + time * 1000L;

    adc_power_on();
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    ESP_ERROR_CHECK(esp_task_wdt_delete(idle_0));           // disable watchdog

    int64_t last_time = get_micros();
    bool last_value = false;

    while (get_micros() < timeout) {
        bool current_value = flasher_feed(adc1_get_raw(ADC1_CHANNEL_6));
        int64_t current_time = get_micros(), delta_time = 0;

        gpio_set_level(LED_PIN, !current_value);

        if (last_value != current_value) {
            delta_time = current_time - last_time;
            if (delta_time > 10000L) last_time = current_time;
            else delta_time = 0;
        }

        if (delta_time) ESP_LOGI(TAG, "bit flip: %lld frame", (delta_time + 8333L)/16667L);

        last_value = current_value;
        
        ets_delay_us(500);
    }

    adc_power_off();
    ESP_ERROR_CHECK(esp_task_wdt_add(idle_0));              // enable watchdog
}

static void touch_isr(void * arg) {
    touch_wake = true;
    touch_pad_clear_status();
}

// initialize gpio
void init_gpio() {
    gpio_pad_select_gpio(LED_PIN);                              // setup builtin led
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);              // set led as an output

    adc1_config_width(ADC_WIDTH_12Bit);                         // set adc to 12 bits
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_11db);  // set attenuation to -11dB (0-3v)
}

// initialize touch
void init_touch() {
    ESP_ERROR_CHECK(touch_pad_init());

    ESP_ERROR_CHECK(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER));
    ESP_ERROR_CHECK(touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V));

    ESP_ERROR_CHECK(touch_pad_config(TOUCH_PAD_GPIO33_CHANNEL, 600));

    ESP_ERROR_CHECK(touch_pad_filter_start(10));

    ESP_ERROR_CHECK(touch_pad_isr_register(touch_isr, NULL));

    ESP_ERROR_CHECK(touch_pad_intr_enable());
}

// initialize spiffs fs
void init_spiffs() {
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

    ESP_LOGV(TAG, "spiffs used %d/%d bytes.", used, total);
}

// randomizes the ble mac address
void randomize_mac() {
    uint8_t * random_mac = rng_gen(6, NULL);

    random_mac[0] |= 0xC0;

    ESP_LOGV(TAG, "setting new random mac.");
    //print_hex_buffer(random_mac, 6);
    
    ble_adapter_set_rand_mac(random_mac);

    free(random_mac);
}

void app_main(void)
{
    ESP_LOGI(TAG, "esp booted!");

    ESP_ERROR_CHECK(nvs_flash_init());

    init_touch();           // initialize touch and activate callbacks
    init_gpio();            // initialize GPIO

    adc_power_off();        // turn of adc

    init_spiffs();          // initialize spiffs

    // initialize wifi

    /*wifi_adapter_init();

    wifi_adapter_try_connect(WIFI_SSID, WIFI_PASSWORD); // defined in passwords.h, which is a file you need to create!

    while (!wifi_adapter_get_flag(WIFI_ADAPTER_CONNECTED_FLAG)) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        ESP_LOGD(TAG, "waiting for wifi connect...");
    }

    timesync_sync();

    time_t now;
    time(&now);

    ESP_LOGI(TAG, "date: %s", ctime(&now));

    http_get("example.com", "80", "/", http_get_cb);
    
    wifi_adapter_disconnect();
    wifi_adapter_stop();*/

    // initialize ble adapter

    ble_adapter_init();

    randomize_mac();        // randomly generate a new mac address

    ble_adapter_register_scan_callback(&scan_cb);

    // scan for peers

    uint32_t epoch = get_epoch();

    scan_for_peers(epoch, 600);

    // generate ble payload

    tracer_tek tek = *tracer_derive_tek(epoch);

    tracer_datapair pair = tracer_derive_datapair(epoch, ble_adapter_get_adv_tx_power());

    tracer_ble_payload payload = tracer_derive_ble_payload(pair);

    ble_adapter_set_raw(payload.value, payload.len);

    // testing stuff

    delete_old_enins(epoch);

    uint32_t last_datapair_epoch = epoch;
    uint32_t last_scan_epoch = epoch;

    // advertising loop
    while (true) {
        if (touch_wake) {

            ESP_LOGI(TAG, "scanning for light data.");

            test_light_comms(10*1000);

            //char * data = derive_light_data(10000); // scan for light data for 10 seconds

            //ESP_LOGI(TAG, "recieved light data %s.", data);

            //free(data);

            touch_wake = false;
        }

        gpio_set_level(LED_PIN, 1);                                 // turn builtin led on
        
        epoch = get_epoch();

        if (tracer_detect_tek_rollover(tek.epoch, epoch)) {
            ESP_LOGI(TAG, "tek rollover!");
            tek = *tracer_derive_tek(epoch);
        } 

        if (tracer_detect_enin_rollover(last_datapair_epoch, epoch)) {
            ESP_LOGI(TAG, "enin rollover!");
            pair = tracer_derive_datapair(epoch, ble_adapter_get_adv_tx_power());
            payload = tracer_derive_ble_payload(pair);
            ble_adapter_set_raw(payload.value, payload.len);
            last_datapair_epoch = epoch;
        }

        if (tracer_detect_scanin_rollover(last_scan_epoch, epoch)) {
            ESP_LOGI(TAG, "scanin rollover!");
            scan_for_peers(epoch, 600);
            last_scan_epoch = epoch;
        }

        ble_adapter_start_advertising();                            // start advertising
        vTaskDelay(10 / portTICK_PERIOD_MS);                        // wait for 10ms (1 RTOS tick)
        ble_adapter_stop_advertising();                             // stop advertising
        gpio_set_level(LED_PIN, 0);                                 // turn off builtin led
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(270*1000));   // enable wakeup in 0.2 second
        ESP_ERROR_CHECK(esp_light_sleep_start());                   // sleep
    }
    
}
