/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "time.h"

#include "esp_sleep.h"

#include "ble_adapter.h"
#include "tracer.h"

#define LED_BUILTIN 2

char hash_str[] = "hello";
// w/o null terminator, is 16 bytes
char hash_str_block[] = "hello world!!!!!";

// 7f c4 2a 88 36 a1 92 34 3d f5 9a 95 81 e7 cf 97
uint8_t crypt_key[] = { 0x7f, 0xc4, 0x2a, 0x88, 0x36, 0xa1, 0x92, 0x34, 0x3d, 0xf5, 0x9a, 0x95, 0x81, 0xe7, 0xcf, 0x97 };
//uint8_t zeroes[]    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void print_hex_buffer(void * data, size_t data_len) {
    for (int i = 0; i < data_len; i++) {
        printf("%02x", ((uint8_t*)data)[i]);
    }
}

void app_main(void)
{

    gpio_pad_select_gpio(LED_BUILTIN);
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);

    printf("esp booted!\n");

    uint8_t * new_mac = rng_gen(6, NULL);

    ESP_ERROR_CHECK(esp_base_mac_addr_get(new_mac));
    printf("mac address set to: ");
    print_hex_buffer(new_mac, 6);
    printf("\n");

    free(new_mac);

    /*uint8_t * hash = NULL;
    uint8_t * zeroes = NULL;

    printf("hashing string \"%s\"...\n", hash_str);
    hash = sha256(hash_str, sizeof(hash_str)-1, NULL);                // passed!
    print_hex_buffer(hash, SHA256_HASH_SIZE);
    printf("\n");
    free(hash);
    free(zeroes);

    printf("getting hkdf of string \"%s\"...\n", hash_str);
    hash = hkdf(hash_str, sizeof(hash_str)-1, 
        NULL, 0, 
        NULL, 0,
        16, 
        NULL);     // passed!
    print_hex_buffer(hash, 16);
    printf("\n");
    free(hash);
    free(zeroes);

    printf("getting aes of string \"%s\"...\n", hash_str_block);
    hash = encrypt_aes_block(crypt_key, AES128_KEY_SIZE, hash_str_block, NULL);
    print_hex_buffer(hash, AES128_BLOCK_SIZE);
    printf("\n");
    free(hash);
    free(zeroes);

    // passed!
    printf("getting aes_ctr of string \"%s\"...\n", hash_str);
    zeroes = zeroes_gen(16, NULL);
    hash = encrypt_aes_block_ctr(crypt_key, AES128_KEY_SIZE, zeroes, hash_str_block, AES128_BLOCK_SIZE, NULL);
    print_hex_buffer(hash, AES128_BLOCK_SIZE);
    printf("\n");
    free(hash);
    free(zeroes);*/

    ble_adapter_init();

    uint8_t flags[] = { 0x1a };
    uint8_t uuid[]  = { 0x6f, 0xfd };
    uint8_t data[]  = { 0x6f, 0xfd, 0xde, 0xad, 0xbe, 0xef };

    //ble_adapter_add_raw(data, sizeof(data));

    //ble_adapter_add_long(0x01, 0x1a, 1);
    //ble_adapter_add_long(0x03, 0xfd6f, 2);
    //ble_adapter_add_long(0x16, 0xfd6fbeef, 4);

    ble_adapter_add_record(0x01, flags, sizeof(flags));
    ble_adapter_add_record(0x02, uuid, sizeof(uuid));
    ble_adapter_add_record(0x16, data, sizeof(data));

    while (1) {
        esp_bt_sleep_disable();
        gpio_set_level(LED_BUILTIN, 1);
        ble_adapter_start_advertising();
        vTaskDelay(5 / portTICK_PERIOD_MS);
        ble_adapter_stop_advertising();
        esp_bt_sleep_enable();
        gpio_set_level(LED_BUILTIN, 0);
        esp_sleep_enable_timer_wakeup(1000*1000);
        esp_light_sleep_start();
    }
    
}
