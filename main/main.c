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
#include "http_server.h"
#include "streamop.h"
#include "http.h"
#include "tracer.h"
#include "cvec.h"
#include "test_cert.h"

#define LED_PIN             2
#define PHOTOCELL_PIN       34
#define TOUCH_PIN           33
#define SPIFFS_ROOT         "/spiffs"

#define POST_SSID_KEY_BEGIN "ssid["
#define POST_PWD_KEY_BEGIN  "pwd["
#define POST_KEY_END        ']'

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

void http_get_cb(char * data, size_t len, void * user_data) {
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

void test_light_comms(int64_t time) {
    
    int64_t timeout = get_micros() + time * 1000L;

    adc_power_on();
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    ESP_ERROR_CHECK(esp_task_wdt_delete(idle_0));           // disable watchdog

    flasher_config(100, 5000L, 33333L, 500, get_micros());

    char c = 0;
    uint8_t c_head = 0;

    uint8_t header_counter = 0;

    bool receiving = false;

    while (get_micros() < timeout || receiving) {
        int8_t bit = flasher_feed(adc1_get_raw(ADC1_CHANNEL_6), get_micros());
        receiving = header_counter >= 3;
        if (bit == 2) {
            header_counter++;
        } else if (bit > -1 && bit < 2 && receiving) {
            if (receiving) {
                c |= bit << c_head++;
                if (c_head == 8) {
                    ESP_LOGI(TAG, "%c", c);
                    c = 0;
                    c_head = 0;
                }
            } else {
                header_counter = 0;
            }
        }
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

esp_err_t config_get_handler(httpd_req_t * req) {

    extern const char onboard[] asm("_binary_onboarding_html_start");

    httpd_resp_sendstr(req, onboard);
    return ESP_OK;
}

esp_err_t config_get_scanned_wifi_handler(httpd_req_t * req) {

    if (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_SCANNING)) {
        wifi_adapter_begin_scan();
    }

    while (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_SCANNING)) {
        vTaskDelay(100L / portTICK_PERIOD_MS);
    }

    uint16_t scan_data_len = 20;
    wifi_ap_record_t * scan_data = wifi_adapter_get_scan(&scan_data_len);

    //wifi_ap_record_t * scan_data = (wifi_ap_record_t *)req->user_ctx;

    httpd_resp_set_hdr(req, "Content-Type", "text/csv");

    httpd_resp_sendstr_chunk(req, "SSID,RSSI,Security\n");

    uint16_t i = 0;

    while (scan_data[i++].ssid[0]) {
        httpd_resp_sendstr_chunk(req, (const char *)scan_data[i].ssid);
        char num[32] = "";
        sprintf(num, ",%d,%d\n", scan_data[i].rssi, scan_data[i].authmode);
        httpd_resp_sendstr_chunk(req, num);
    }

    httpd_resp_sendstr_chunk(req, NULL);

    free(scan_data);

    return ESP_OK;
}

esp_err_t config_get_wifi_status_handler(httpd_req_t * req) {

    if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG)) httpd_resp_sendstr(req, "ok");
    else if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) httpd_resp_sendstr(req, "fail");
    else httpd_resp_sendstr(req, "disconnected");

    return ESP_OK;
}

esp_err_t config_post_wifi_data_handler(httpd_req_t * req) {
    char recv_buf[64];
    int ret, remaining = req->content_len;

    streamop_match_token match_ssid_begin = streamop_token_from_str(POST_SSID_KEY_BEGIN);
    streamop_match_token match_pwd_begin = streamop_token_from_str(POST_PWD_KEY_BEGIN);

    bool ssid_block = false, pwd_block = false;

    char ssid[33] = { 0 }, pwd[65] = { 0 };
    size_t ssid_head = 0, pwd_head = 0;

    while (remaining > 0) {
        ret = httpd_req_recv(req, recv_buf, MIN(sizeof(recv_buf), remaining));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }

        remaining -= ret;

        for (int i = 0; i < ret; i++) {
            char c = recv_buf[i];

            ssid_block &= c != POST_KEY_END;
            pwd_block &= c != POST_KEY_END;

            if (ssid_block) {
                ssid[ssid_head++] = c;
                ssid_block = ssid_head != (sizeof(ssid) - 1);
            }
            if (pwd_block) {
                pwd[pwd_head++] = c;
                pwd_block = pwd_head != (sizeof(pwd) - 1);
            }

            ssid_block |= streamop_match_character(&match_ssid_begin, c) == STREAMOP_MATCH;
            pwd_block |= streamop_match_character(&match_pwd_begin, c) == STREAMOP_MATCH;
        }
    }

    ESP_LOGI(TAG, "recieved POST with ssid: %s, pwd: %d chars", ssid, strlen(pwd));

    wifi_adapter_connect(ssid, strlen(pwd) > 8 ? pwd : NULL);

    while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG) && !GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
        httpd_resp_sendstr(req, "fail");
        ESP_LOGI(TAG, "wifi failed!");
    } else {
        httpd_resp_sendstr(req, "ok");
        ESP_LOGI(TAG, "wifi connected!");
    }

    return ESP_OK;

}

void config_submit_keys_http_cb(char * data, size_t len, void * user_dat) {

    //uint8_t * flags = (uint8_t *)user_dat;

    ESP_LOGI(TAG, "got response %s.", data);

    //if (strncmp(data, "ok", sizeof("ok")-1) == 0) SET_FLAG(*flags, BIT(0));

    //SET_FLAG(*flags, BIT(1));

    return;
}

esp_err_t config_get_submit_positive_diagnosis_handler(httpd_req_t * req) {

    if (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG)) {
        httpd_resp_sendstr(req, "fail");
        return ESP_FAIL;
    }

    struct config_ctx {
        bool valid;
        char * data_buf;
    } * submit_ctx = req->user_ctx;
    submit_ctx->data_buf = calloc(1024, 1);
    size_t data_head = 0;

    char recv_buf[64] = { 0 };
    int ret, remaining = req->content_len;

    while (remaining > 0) {
        ret = httpd_req_recv(req, recv_buf, MIN(sizeof(recv_buf), remaining));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }

        remaining -= ret;

        for (int i = 0; i < ret; i++) {
            char c = recv_buf[i];
            putchar(c);

            if (data_head != 1024) submit_ctx->data_buf[data_head++] = c;
        }
    }

    httpd_resp_sendstr(req, "ok");

    submit_ctx->valid = true;

    return ESP_OK;
}

void enter_config() {

    struct config_ctx {
        bool valid;
        char * data_buf;
    } submit_ctx;

    submit_ctx.data_buf = NULL;
    submit_ctx.valid = 0;

    wifi_adapter_init(WIFI_ADAPTER_DUAL);
    wifi_adapter_set_netif(WIFI_ADAPTER_AP);

    wifi_adapter_begin_softap("heeereee", NULL);

    http_server_begin();

    http_server_onrequest(HTTP_GET, "/", config_get_handler, NULL);
    http_server_onrequest(HTTP_GET, "/scandata", config_get_scanned_wifi_handler, NULL);
    http_server_onrequest(HTTP_GET, "/getwifistatus", config_get_wifi_status_handler, NULL);
    http_server_onrequest(HTTP_POST, "/submitkeys", config_get_submit_positive_diagnosis_handler, &submit_ctx);
    http_server_onrequest(HTTP_POST, "/postwifi", config_post_wifi_data_handler, NULL);

    for (int wait_time = 0; wait_time < 300; wait_time++) {
        if (submit_ctx.valid) break;
        vTaskDelay(1000L / portTICK_PERIOD_MS);
    }

    http_server_stop();

    wifi_adapter_stop();
    wifi_adapter_deinit();

    if (submit_ctx.valid) {
        ESP_LOGI(TAG, "posting data...");
        wifi_adapter_init(WIFI_ADAPTER_STA);
        wifi_adapter_connect(NULL, NULL);

        while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP)) {
            ESP_LOGD(TAG, "waiting for ip assignment...");
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        ESP_LOGI(TAG, "%s", submit_ctx.data_buf);

        char post_buf[7 + sizeof(tracer_tek_array)] = { 0 };
        memcpy(post_buf, submit_ctx.data_buf, 7);                           // copy caseid from post request
        memcpy(post_buf + 7, tracer_tek_array, sizeof(tracer_tek_array));   // copy tracer tek array

        http_req_ip("POST", submit_ctx.data_buf + 7, submit_ctx.data_buf + 7, post_buf, sizeof(post_buf), config_submit_keys_http_cb, NULL);
    }

    free(submit_ctx.data_buf);
    //free(scan_data);
}

void app_main(void) {
    ESP_LOGI(TAG, "esp booted!");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_erase());

    init_touch();           // initialize touch and activate callbacks
    init_gpio();            // initialize GPIO

    adc_power_off();        // turn of adc

    init_spiffs();          // initialize spiffs

    enter_config();

    //timesync_sync();

    //time_t now;
    //time(&now);

    //ESP_LOGI(TAG, "date: %s", ctime(&now));

    //http_get("example.com", "80", "/", http_get_cb);
    //http_req("GET", "www.example.com", "http://www.example.com/", NULL, 0, http_get_cb);

    //http_req("POST", "www.postman-echo.com", "http://postman-echo.com/post", "hello world!", sizeof("hello world!"), http_get_cb);

    //https_req("GET", "www.howsmyssl.com", "https://www.howsmyssl.com/a/check", NULL, 0, HOWSMYSSL_PEM, http_get_cb);
    
    //wifi_adapter_disconnect();
    //wifi_adapter_stop();

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

            test_light_comms(30L*1000L);

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
