#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <sys/dir.h>
#include <sys/dirent.h>
#include <sys/time.h>

#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "esp_log.h"

#include "stdio.h"
#include "stdint.h"
#include "time.h"
#include "limits.h"

#include "streamop.h"
#include "tracer.h"
#include "cvec.h"

static const char * TAG = "main";

// gets the unix epoch time
uint32_t get_epoch() {
    time_t out;
    time(&out);
    return out;
}

void app_main(void) {
    ESP_LOGI(TAG, "esp booted!");

    uint32_t epoch = get_epoch();

    tracer_tek tek = *tracer_derive_tek(epoch);

    tracer_datapair pair = tracer_derive_datapair(epoch, 0);

    tracer_ble_payload payload = tracer_derive_ble_payload(pair);


    uint32_t last_datapair_epoch = epoch;
    uint32_t last_scan_epoch = epoch;

    // advertising loop
    while (true) {

        // do tracer stuff

        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(270*1000));   // enable wakeup in 0.2 second
        ESP_ERROR_CHECK(esp_light_sleep_start());                   // sleep
    }
    
}
