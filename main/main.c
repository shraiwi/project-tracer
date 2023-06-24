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

//#include "passwords.h"      // this file is in .gitignore for me. you probably want to create your own passwords.h and input your own data.
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

#define CONFIG_SUBMIT_FLAG  BIT(0)
#define CONFIG_WIFI_FLAG    BIT(1)

#define SPIFFS_ROOT         "/spiffs"
#define TEKFILE_NAME        "tekfile"
#define MATCHFILE_NAME      "matches"

#define TRACER_KEYSERVER    "10.0.0.173"

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

        if (!rpi_exists) {
            //ESP_LOGI(TAG, "rpi: %x\taem: %x", *(uint32_t *)pair.rpi.value, *(uint32_t *)pair.aem.value);
            cvec_append(scanned_data, pair);
        }
    }
}

void http_get_cb(char * data, size_t len, void * user_data) {
    printf("%s", data);
}

void scan_for_peers(uint32_t epoch, uint32_t ms) {
    uint32_t scanin = tracer_epoch2scanin(epoch);

    scanned_data = cvec_arrayof(tracer_datapair);

    ble_adapter_start_scanning();
    vTaskDelay(ms / portTICK_PERIOD_MS);
    ble_adapter_stop_scanning();

    cvec_crunch(scanned_data);  // free up unused memory

    // to save on storeage, the tracer api will now not write to a scanfile if no peers are found.
    if (cvec_len(scanned_data) > 0) {
        char * fname = b64_encode(&scanin, sizeof(scanin));
        fname = realloc(fname, strlen(SPIFFS_ROOT"/") + strlen(fname) + 1);
        memmove(fname + strlen(SPIFFS_ROOT"/"), fname, strlen(fname) + 1);
        memcpy(fname, SPIFFS_ROOT"/", strlen(SPIFFS_ROOT"/"));

        ESP_LOGD(TAG, "writing to %s...", fname);

        FILE * scan_record = fopen(fname, "w");

        free(fname);

        if (scan_record != NULL) {
            fwrite(scanned_data, cvec_sizeof(scanned_data), 1, scan_record);
        } else {
            ESP_LOGE(TAG, "file null!");
        }

        ESP_LOGI(TAG, "found %d peers.", cvec_len(scanned_data));

        fclose(scan_record);
    } else {
        ESP_LOGI(TAG, "no peers found.");
    }

    cvec_free(scanned_data);
}

// saves the teks to spiffs.
void save_teks() {
    FILE * tek_file = fopen(SPIFFS_ROOT "/" TEKFILE_NAME, "w");
    if (tek_file) {
        ESP_LOGI(TAG, "writing tek array to tekfile.");
        fwrite(tracer_tek_array, 1, sizeof(tracer_tek_array), tek_file);
        fclose(tek_file);
    } else {
        ESP_LOGE(TAG, "error opening tekfile!");
    }
}

void load_teks() {
    FILE * tek_file = fopen(SPIFFS_ROOT "/" TEKFILE_NAME, "r");
    if (tek_file) {
        fread(tracer_tek_array, 1, sizeof(tracer_tek_array), tek_file);
        uint32_t newest = 0;
        for (size_t i = 0; i < TRACER_TEK_STORE_PERIOD; i++) {
            if (tracer_tek_array[i].epoch > newest) {
                newest = tracer_tek_array[i].epoch;
                tracer_tek_array_head = i;
            }
        }
        ESP_LOGI(TAG, "found tekfile, set head to %d.", tracer_tek_array_head);
        fclose(tek_file);
    } else {
        ESP_LOGE(TAG, "error opening tekfile!");
    }
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
    else if (wifi_adapter_has_saved_credentials()) httpd_resp_sendstr(req, "saved");
    else httpd_resp_sendstr(req, "disconnected");

    return ESP_OK;
}

esp_err_t config_post_wifi_data_handler(httpd_req_t * req) {
    char recv_buf[64];
    int ret, remaining = req->content_len;

    streamop_token match_ssid_begin = streamop_create_token_from_str(POST_SSID_KEY_BEGIN);
    streamop_token match_pwd_begin = streamop_create_token_from_str(POST_PWD_KEY_BEGIN);

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

    wifi_adapter_connect(ssid, strlen(pwd) > 0 ? pwd : NULL);

    while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG) && !GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
        httpd_resp_sendstr(req, "fail");
        ESP_LOGI(TAG, "wifi failed!");
    } else {
        httpd_resp_sendstr(req, "ok");
        ESP_LOGI(TAG, "wifi connected!");
        while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP)) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }

    return ESP_OK;

}

esp_err_t config_get_flash_state(httpd_req_t * req) {
    size_t total, used;
    ESP_LOGI(TAG, "getting flash usage...");

    ESP_ERROR_CHECK(esp_spiffs_info(NULL, &total, &used));

    char datastring[64] = { 0 };
    sprintf(datastring, "%u/%u", used, total);
    httpd_resp_sendstr(req, datastring);

    return ESP_OK;
}

esp_err_t config_get_exposure_handler(httpd_req_t * req) {
    ESP_LOGI(TAG, "attempting to send matchfile to client...");

    FILE * matchfile = fopen(SPIFFS_ROOT"/"MATCHFILE_NAME, "r");

    if (matchfile) {
        uint32_t match;
        while (fread(&match, sizeof(uint32_t), 1, matchfile)) {
            ESP_LOGI(TAG, "read 1 epoch");
            httpd_resp_send_chunk(req, &match, sizeof(match));
        }
        httpd_resp_sendstr_chunk(req, NULL);

        fclose(matchfile);
    } else {
        ESP_LOGE(TAG, "matchfile not available for opening!");
    }

    return ESP_OK;
}

esp_err_t config_erase_flash_handler(httpd_req_t * req) {
    ESP_ERROR_CHECK(esp_spiffs_format(NULL));
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

void config_submit_keys_http_cb(char * data, size_t len, void * user_dat) {

    ESP_LOGI(TAG, "got response %s.", data);

    return;
}

esp_err_t config_get_submit_positive_diagnosis_handler(httpd_req_t * req) {

    if (!wifi_adapter_has_saved_credentials()) {
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

    submit_ctx->valid = data_head > 0;

    return ESP_OK;
}

// enters configuration for a certain amount of time
// flags can be used to specify when to break out of configuration prematurely (on WiFi connect or submit request)
void enter_config(uint32_t time, uint8_t flags) {

    struct config_ctx {
        bool valid;
        char * data_buf;
    } submit_ctx;

    submit_ctx.data_buf = NULL;
    submit_ctx.valid = false;

    wifi_adapter_init(WIFI_ADAPTER_DUAL);
    wifi_adapter_set_netif(WIFI_ADAPTER_AP);

    char ssid_name[32] = "tracer-";
    size_t ssid_name_head = strlen(ssid_name);
    const char ssid_alphabet[] = "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < 5; i++) {
        ssid_name[ssid_name_head++] = ssid_alphabet[esp_random() % strlen(ssid_alphabet)];
    }

    wifi_adapter_begin_softap(ssid_name, NULL);

    http_server_begin();

    http_server_onrequest(HTTP_GET, "/", config_get_handler, NULL);
    http_server_onrequest(HTTP_GET, "/scandata", config_get_scanned_wifi_handler, NULL);
    http_server_onrequest(HTTP_GET, "/getwifistatus", config_get_wifi_status_handler, NULL);
    http_server_onrequest(HTTP_GET, "/getspiffsstate", config_get_flash_state, NULL);
    http_server_onrequest(HTTP_GET, "/matches", config_get_exposure_handler, NULL);
    http_server_onrequest(HTTP_GET, "/formatflash", config_erase_flash_handler, NULL);
    http_server_onrequest(HTTP_POST, "/submitkeys", config_get_submit_positive_diagnosis_handler, &submit_ctx);
    http_server_onrequest(HTTP_POST, "/postwifi", config_post_wifi_data_handler, NULL);

    uint8_t state_flags = 0;

    for (size_t wait_time = 0; wait_time < time; wait_time++) {
        state_flags |= (bool)(submit_ctx.valid) << 0;
        state_flags |= (bool)(GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG)) << 1;

        if (state_flags & flags) break;

        gpio_set_level(LED_PIN, 1);
        vTaskDelay(500L / portTICK_PERIOD_MS);
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(500L / portTICK_PERIOD_MS);
    }

    vTaskDelay(1000L / portTICK_PERIOD_MS);

    http_server_stop();

    if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG)) wifi_adapter_disconnect();
    wifi_adapter_stop();
    wifi_adapter_deinit();

    if (submit_ctx.valid) {
        ESP_LOGI(TAG, "posting data...");
        wifi_adapter_init(WIFI_ADAPTER_STA);
        wifi_adapter_connect(NULL, NULL);

        while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG) && !GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
            ESP_LOGD(TAG, "waiting for connection...");
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
            ESP_LOGE(TAG, "can't connect to wifi!");
        } else {
            while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP)) {
                ESP_LOGD(TAG, "waiting for ip assignment...");
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
            //ESP_LOGI(TAG, "%s", submit_ctx.data_buf);

            char post_buf[7 + sizeof(tracer_tek_array)] = { 0 };
            memcpy(post_buf, submit_ctx.data_buf, 7);                           // copy caseid from post request
            memcpy(post_buf + 7, tracer_tek_array, sizeof(tracer_tek_array));   // copy tracer tek array

            http_req_ip("POST", TRACER_KEYSERVER, TRACER_KEYSERVER"/", post_buf, sizeof(post_buf), config_submit_keys_http_cb, NULL);

            wifi_adapter_disconnect();
        }

        wifi_adapter_stop();
        wifi_adapter_deinit();
    }

    free(submit_ctx.data_buf);
    //free(scan_data);
}

// tests a chunk of teks against stored matches.
void test_teks(tracer_tek * tek_array, size_t tek_array_len) {
    ESP_LOGI(TAG, "validating %u teks.", tek_array_len);

    DIR * root_dir = opendir(SPIFFS_ROOT);
    struct dirent * de;

    FILE * matchfile = fopen(SPIFFS_ROOT"/"MATCHFILE_NAME, "a");

    tracer_tek stream_tek;
    //streamop_token tek_token = streamop_create_chunk_token(&stream_tek, sizeof(tracer_tek));

    while ((de = readdir(root_dir)) != NULL) {
        if (strcmp(de->d_name, TEKFILE_NAME) == 0 || strcmp(de->d_name, MATCHFILE_NAME) == 0) {
            ESP_LOGI(TAG, "found tekfile or matchfile");
        } else {
            uint32_t * decoded_scanin = (uint32_t *)b64_decode(de->d_name, NULL);
            uint32_t file_scanin = *decoded_scanin;
            uint32_t file_epoch = tracer_scanin2epoch(file_scanin);
            free(decoded_scanin);

            ESP_LOGD(TAG, "file: %s\tderived scanin: %u", de->d_name, file_scanin);

            char ffullpath[strlen(de->d_name) + strlen(SPIFFS_ROOT"/") + 1];
            memcpy(ffullpath, SPIFFS_ROOT"/", sizeof(SPIFFS_ROOT"/"));
            strcat(ffullpath, de->d_name);

            FILE * scanfile = fopen(ffullpath, "r");

            if (scanfile == NULL) {
                ESP_LOGE(TAG, "couldn't open file %s!", ffullpath);
            } else {
                tracer_datapair datapair;
                while (fread(&datapair, sizeof(tracer_datapair), 1, scanfile)) {
                    for (size_t i = 0; i < tek_array_len; i++) {
                        if (tracer_verify(datapair, tek_array[i], NULL, NULL)) {
                            ESP_LOGW(TAG, "tek match!");
                            fwrite(&file_epoch, sizeof(uint32_t), 1, matchfile);
                        }
                    }
                }
                fclose(scanfile);
            }
        }
    }

    fclose(matchfile);

    closedir(root_dir);
}

void validate_tek_http_stream(char * data, size_t data_len, void * user_dat) {

    //ESP_LOGD(TAG, "scanning files...");
    ESP_LOGI(TAG, "recieved chunk %u bytes long.", data_len);

    struct {
        tracer_tek tek_buffer[128];
        size_t tek_buffer_head;
        size_t expected_chunk_len;
        tracer_tek current_tek;
        streamop_token chunker;
        streamop_token http_end;
        bool body_valid;
    } * stream_ctx = user_dat;

    for (size_t i = 0; i < data_len; i++) {
        char c = data[i];
        if (stream_ctx->body_valid) {
            //ESP_LOGI(TAG, "body now valid.");
            if (streamop_chunk_character(&stream_ctx->chunker, c) == STREAMOP_CHUNK_OK) {
                stream_ctx->tek_buffer[stream_ctx->tek_buffer_head++] = stream_ctx->current_tek;
                if (stream_ctx->tek_buffer_head == 128) {
                    test_teks(stream_ctx->tek_buffer, stream_ctx->tek_buffer_head);
                    stream_ctx->tek_buffer_head = 0;
                }
            }
        }
        stream_ctx->body_valid |= streamop_match_character(&stream_ctx->http_end, c) == STREAMOP_MATCH;
    }
    if (stream_ctx->expected_chunk_len != data_len) {   // on the last chunk
        test_teks(stream_ctx->tek_buffer, stream_ctx->tek_buffer_head);
        stream_ctx->tek_buffer_head = 0;
    }
}

void check_teks() {
    wifi_adapter_init(WIFI_ADAPTER_STA);

    if (!wifi_adapter_has_saved_credentials()) {
        ESP_LOGW(TAG, "no wifi credentials saved, cancelling download...");
        return;
    }

    wifi_adapter_connect(NULL, NULL);

    while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG) && !GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
        ESP_LOGE(TAG, "wifi failed!");
    } else {
        ESP_LOGI(TAG, "wifi connected!");
        while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP)) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "ip acquired, syncing time...");

        timesync_sync();

        ESP_LOGI(TAG, "time synced!");

        struct {
            tracer_tek tek_buffer[128];
            size_t tek_buffer_head;
            size_t expected_chunk_len;
            tracer_tek current_tek;
            streamop_token chunker;
            streamop_token http_end;
            bool body_valid;
        } tek_stream_ctx;

        memset(tek_stream_ctx.tek_buffer, 0, sizeof(tek_stream_ctx.tek_buffer));
        tek_stream_ctx.tek_buffer_head = 0;
        tek_stream_ctx.expected_chunk_len = 64;
        tek_stream_ctx.chunker = streamop_create_chunk_token(&tek_stream_ctx.current_tek, sizeof(tracer_tek));

        tek_stream_ctx.http_end = streamop_create_token_from_str("\r\n\r\n");
        tek_stream_ctx.body_valid = false;

        http_req_ip("GET", TRACER_KEYSERVER, TRACER_KEYSERVER"/", NULL, 0, validate_tek_http_stream, &tek_stream_ctx);

        wifi_adapter_disconnect();
    }

    wifi_adapter_stop();
    wifi_adapter_deinit();    
}

// deletes scanfiles older than max_scanin_age
void free_spiffs(uint32_t epoch, uint32_t max_scanin_age) {

    DIR * root_dir = opendir(SPIFFS_ROOT);
    struct dirent * de;

    uint32_t min_age = tracer_epoch2scanin(epoch) - max_scanin_age;

    while ((de = readdir(root_dir)) != NULL) {
        if (strcmp(de->d_name, TEKFILE_NAME) == 0 || strcmp(de->d_name, MATCHFILE_NAME) == 0) {
            ESP_LOGI(TAG, "found tekfile or matchfile");
        } else {
            uint32_t * decoded_enin = (uint32_t *)b64_decode(de->d_name, NULL);
            uint32_t file_enin = *decoded_enin;
            free(decoded_enin);

            char ffullpath[strlen(de->d_name) + strlen(SPIFFS_ROOT"/") + 1];
            memcpy(ffullpath, SPIFFS_ROOT"/", sizeof(SPIFFS_ROOT"/"));
            strcat(ffullpath, de->d_name);

            if (file_enin < min_age) {
                ESP_LOGI(TAG, "deleting file %s", de->d_name);
                remove(ffullpath);
            }
        }
    }

    closedir(root_dir);
}

// initializes configuration 
void startup_config() {
    
    wifi_adapter_init(WIFI_ADAPTER_STA);
    bool has_persistence = wifi_adapter_has_saved_credentials();
    wifi_adapter_deinit();

    if (!has_persistence) {
        ESP_LOGI(TAG, "wifi credentials not found, entering configuration.");
        enter_config(-1, CONFIG_WIFI_FLAG); // -1 makes for an underflow 2^32 - 1 seconds (basically forever)
    } else {
        ESP_LOGI(TAG, "persistent credentials found, syncing system time.");
        wifi_adapter_init(WIFI_ADAPTER_STA);
        wifi_adapter_connect(NULL, NULL);

        while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECTED_FLAG) && !GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
            ESP_LOGD(TAG, "waiting for connection...");
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        if (GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_CONNECT_FAIL_FLAG)) {
            ESP_LOGE(TAG, "can't connect to wifi!");
        } else {
            while (!GET_FLAG(wifi_adapter_flags, WIFI_ADAPTER_HAS_IP)) {
                ESP_LOGD(TAG, "waiting for ip assignment...");
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }

            timesync_sync();

            wifi_adapter_disconnect();
        }

        wifi_adapter_stop();
        wifi_adapter_deinit();
    }
    
}

void app_main(void) {
    ESP_LOGI(TAG, "esp booted!");

    ESP_ERROR_CHECK(nvs_flash_init());
    //ESP_ERROR_CHECK(nvs_flash_erase());

    init_touch();           // initialize touch and activate callbacks
    init_gpio();            // initialize GPIO

    adc_power_off();        // turn off adc

    //esp_spiffs_format(NULL);

    init_spiffs();          // initialize spiffs

    load_teks();

    startup_config();       // enter configuration if wifi credentials not found

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

    //delete_old_enins(epoch);

    uint32_t last_datapair_epoch = epoch;
    uint32_t last_scan_epoch = epoch;

    // advertising loop
    while (true) {
        if (touch_wake) {

            ESP_LOGI(TAG, "entering configuration mode for 300 seconds...");

            enter_config(300, CONFIG_SUBMIT_FLAG);

            touch_wake = false;
        }

        gpio_set_level(LED_PIN, 1);                                 // turn builtin led on
        
        epoch = get_epoch();

        if (tracer_detect_tek_rollover(tek.epoch, epoch)) {
            ESP_LOGI(TAG, "tek rollover!");
            tek = *tracer_derive_tek(epoch);
            save_teks();
            check_teks();
        } 

        if (tracer_detect_enin_rollover(last_datapair_epoch, epoch)) {
            ESP_LOGI(TAG, "enin rollover!");
            pair = tracer_derive_datapair(epoch, ble_adapter_get_adv_tx_power());
            //ESP_LOGI(TAG, "derived datapair, generating payload.");
            payload = tracer_derive_ble_payload(pair);
            //ESP_LOGI(TAG, "setting raw data.");
            ble_adapter_set_raw(payload.value, payload.len);
            last_datapair_epoch = epoch;
        }

        ble_adapter_start_advertising();                            // start advertising

        if (tracer_detect_scanin_rollover(last_scan_epoch, epoch)) {
            ESP_LOGI(TAG, "scanin rollover!");
            scan_for_peers(epoch, 600);
            free_spiffs(epoch, TRACER_SCAN_EXPIRY);
            last_scan_epoch = epoch;
        }

        vTaskDelay(20 / portTICK_PERIOD_MS);                        // wait for 10ms (1 RTOS tick)
        ble_adapter_stop_advertising();                             // stop advertising
        gpio_set_level(LED_PIN, 0);                                 // turn off builtin led
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(270*1000));   // enable wakeup in 0.2 second
        ESP_ERROR_CHECK(esp_light_sleep_start());                   // sleep
    }
    
}
