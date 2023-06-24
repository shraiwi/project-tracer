#include "wifi_adapter.h"
#include "utils.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_sntp.h"

#include "time.h"

#ifndef _TIMESYNC_H_
#define _TIMESYNC_H_

#define TIMESYNC_SYNCED_FLAG        GET_BIT(0)

#define TAG "timesync_lib"

uint8_t timesync_flags = 0;

void timesync_sntp_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "sntp event fired.");
    SET_FLAG(timesync_flags, TIMESYNC_SYNCED_FLAG);
}

void timesync_sync() {

    ESP_LOGI(TAG, "syncing time");
    
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(timesync_sntp_cb);

    sntp_init();

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
        ESP_LOGD(TAG, "waiting for sync...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "sync complete");

    sntp_stop();
}

#undef TAG

#endif