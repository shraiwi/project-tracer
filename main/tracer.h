#include "tracer_crypto.h"

#ifndef _TRACER_H_
#define _TRACER_H_

/**
 * tracer API
 */

// something in here is causing the ESP32 to crash silently, haha. HAHA.
// probably has to do with the janky memory handling going on here, maybe a stack overflow?
// wait, nevermind. this works. or does it? i really can't tell.

// i smell a memory leak somewhere in this coooodeee

// gets size (in bytes) of an object's member.
#define sizeof_member(type, member) sizeof(((type*)0)->member)

// settings
#define TEK_STORE_PERIOD 14         // how many days to store the keys for.
#define TRACER_MAJOR_VERSION    1   // standard major version x.0
#define TRACER_MINOR_VERSION    0   // standard minor version 0.x

/**
 * Temporary Exposure Key
 */
typedef struct {
    uint32_t en_interval_num;
    uint8_t value[16];
} tracer_tek;

/**
 * Rolling Proximity Identifier
 */
typedef struct {
    uint8_t value[16];
} tracer_rpi;

/**
 * Rolling Proximity Identifier Key
 */
typedef struct {
    uint8_t value[16];
} tracer_rpik;

/**
 * Associated Encrypted Metadata Key
 */
typedef struct {
    uint8_t value[16];
} tracer_aemk;

/**
 * Associated Encrypted Metadata
 */
typedef struct {
    uint8_t value[4];
} tracer_aem;

/**
 * Metadata
 */
typedef struct {
    uint8_t value[4];
} tracer_metadata;

/**
 * BLE Advertising payload
 */
typedef struct {
    uint8_t value[31];  // ble payloads are already limited at 31 bytes
    size_t len;         // length of the bluetooth payload
} tracer_ble_payload;

// remember: TEK + epoch => RPIK => RPI =|
//                |=======> AEMK =====> AEM

uint32_t tracer_tek_rolling_period = 144;
tracer_tek tracer_tek_array[TEK_STORE_PERIOD];
size_t tracer_tek_array_head = 0;

void tracer_ble_payload_add_record(tracer_ble_payload * ble_payload, uint8_t type, void * data, size_t data_len) {
    ble_payload->value[ble_payload->len++] = data_len + 1;
    ble_payload->value[ble_payload->len++] = type;
    memcpy(ble_payload->value + ble_payload->len, data, data_len);
    ble_payload->len += data_len;
}

uint32_t tracer_en_interval_number(uint32_t epoch) {
    return (uint32_t)(epoch / (60 * 10));
}

tracer_rpik tracer_derive_rpik(tracer_tek tek) {
    char salt[16] = "EN_RPIK";

    tracer_rpik out;

    hkdf(tek.value, sizeof(tek.value),
        NULL, 0, 
        salt, 16,
        16, 
        out.value);

    return out;
}

// derives a new rolling proximity identifier
tracer_rpi tracer_derive_rpi(tracer_rpik rpik, uint32_t epoch) {
    tracer_rpi out;

    uint8_t padded_data[AES128_BLOCK_SIZE] = "EN-RPI";

    uint32_t enin = tracer_en_interval_number(epoch);                                // get the enintervalnumber

    memcpy(&padded_data[sizeof(padded_data) - sizeof(enin)], &enin, sizeof(enin));   // set the last 4 bytes of the padded date to the epoch

    encrypt_aes_block(rpik.value, sizeof(rpik.value), &padded_data, out.value);

    return out;
}

// derive new metadata
tracer_metadata tracer_derive_metadata(int8_t tx_power) {
    tracer_metadata out;
    memset(out.value, 0, sizeof(out.value));

    out.value[0] =                              // major & minor versioning
        (TRACER_MAJOR_VERSION & 0b11) << 6 |
        (TRACER_MINOR_VERSION & 0b11) << 4;
    out.value[1] = tx_power;                    // transmit tx_power

    return out;
}

// derive a new aemk
tracer_aemk tracer_derive_aemk(tracer_tek tek) {
    tracer_aemk out;

    uint8_t info[AES128_BLOCK_SIZE] = "EN_AEMK";

    hkdf(tek.value, sizeof(tek.value),
        NULL, 0, 
        info, sizeof(info), sizeof(out.value), out.value);

    return out;
}

tracer_aem tracer_derive_aem(tracer_aemk aemk, tracer_rpi rpi, tracer_metadata metadata) {
    tracer_aem out;

    encrypt_aes_block_ctr(aemk.value, sizeof(aemk.value), rpi.value, metadata.value, sizeof(metadata.value), out.value);

    return out;
}

tracer_ble_payload tracer_derive_ble_payload(tracer_rpi rpi, tracer_aem aem) {
    tracer_ble_payload out;
    
    uint8_t flags[]          =  { 0x1a };
    uint8_t uuid[]           =  { 0x6f, 0xfd };
    uint8_t service_data[2 
        + sizeof(rpi.value) 
        + sizeof(aem.value)] =  { 0x6f, 0xfd };
    
    memcpy(service_data + 2, rpi.value, sizeof(rpi.value));
    memcpy(service_data + 2 + sizeof(rpi.value), aem.value, sizeof(aem.value));
    
    out.len = 0;        // haha, it was YOU! darn you, uninitialized values!

    tracer_ble_payload_add_record(&out, 0x01, flags,        sizeof(flags));         // set bluetooth flags
    tracer_ble_payload_add_record(&out, 0x03, uuid,         sizeof(uuid));          // set service uuid
    tracer_ble_payload_add_record(&out, 0x16, service_data, sizeof(service_data));  // set service data

    return out;
}

// returns the address of a newly generated temporary exposure key
tracer_tek * tracer_derive_tek(uint32_t epoch) {
    tracer_tek * out = &tracer_tek_array[tracer_tek_array_head++];
    tracer_tek_array_head %= TEK_STORE_PERIOD;
    out->en_interval_num = tracer_en_interval_number(epoch);
    rng_gen(sizeof(out->value), out->value);
    return out;
}

#endif