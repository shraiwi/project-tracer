#include "tracer_crypto.h"

#ifndef _TRACER_H_
#define _TRACER_H_

void print_hex_buffer(void * data, size_t data_len) {
    for (int i = 0; i < data_len; i++) {
        printf("%02x", ((uint8_t*)data)[i]);
    }
}

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

// constants
#define RPI_STRING  "EN-RPI"
#define RPIK_STRING "EN-RPIK"
#define AEM_STRING  "EN-AEM"
#define AEMK_STRING "EN-AEMK"

// MAC address
typedef struct {
    uint8_t value[6];
} tracer_mac;

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
 * An RPI and AEM pair
 */
typedef struct {
    tracer_rpi rpi;
    tracer_aem aem;
} tracer_datapair;

/**
 * An RPIK and AEMK pair
 */
typedef struct {
    tracer_rpik rpik;
    tracer_aemk aemk;
} tracer_keypair;

/**
 * A record used for storing exposure notifications
 */
typedef struct {
    uint32_t en_interval_num;
    int8_t rssi;
    tracer_datapair datapair;
} tracer_record;

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

tracer_keypair tracer_current_keypair;

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
    char salt[16] = RPIK_STRING;

    tracer_rpik out;

    hkdf(tek.value, AES128_KEY_SIZE,
        NULL, 0, 
        salt, 16,
        16, 
        out.value);

    return out;
}

// derives a new rolling proximity identifier
tracer_rpi tracer_derive_rpi(tracer_rpik rpik, uint32_t epoch) {
    tracer_rpi out;

    uint8_t padded_data[AES128_BLOCK_SIZE] = RPI_STRING;                             // just a note, the rest of the char array will be initialized to 0

    uint32_t enin = tracer_en_interval_number(epoch);                                // get the enintervalnumber

    memcpy(&padded_data[sizeof(padded_data) - sizeof(enin)], &enin, sizeof(enin));   // set the last 4 bytes of the padded date to the epoch

    encrypt_aes_block(rpik.value, AES128_KEY_SIZE, &padded_data, out.value);

    return out;
}

// derive a new storage record
tracer_record tracer_derive_record(tracer_datapair pair, int8_t rssi, uint32_t epoch) {
    tracer_record out = {
        .en_interval_num = tracer_en_interval_number(epoch),
        .rssi = rssi,
        .datapair = pair,
    };
    return out;
}

// tests 2 datapairs for equality
bool tracer_compare_datapairs(tracer_datapair a, tracer_datapair b) {
    return memcmp(&a.aem.value, &b.aem.value, sizeof(a.aem.value)) == 0 && memcmp(&a.rpi.value, &b.rpi.value, sizeof(a.rpi.value)) == 0;
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

    uint8_t info[AES128_BLOCK_SIZE] = AEMK_STRING;

    hkdf(tek.value, sizeof(tek.value),
        NULL, 0, 
        info, sizeof(info), sizeof(out.value), out.value);

    return out;
}

tracer_aem tracer_derive_aem(tracer_aemk aemk, tracer_rpi rpi, tracer_metadata metadata) {
    tracer_aem out;

    flip_aes_block_ctr(aemk.value, AES128_KEY_SIZE, rpi.value, metadata.value, sizeof(metadata.value), out.value);

    return out;
}

tracer_keypair tracer_derive_keypair(tracer_tek tek) {
    tracer_keypair out;
    out.rpik = tracer_derive_rpik(tek);
    out.aemk = tracer_derive_aemk(tek);
    return out;
}

// derive a new ble payload given a datapair
tracer_ble_payload tracer_derive_ble_payload(tracer_datapair datapair) {
    tracer_ble_payload out;
    
    uint8_t flags[]          =  { 0x1a };
    uint8_t uuid[]           =  { 0x6f, 0xfd };     // the ESP32 is little-endian, and so is BLE. However, human readable stuff is big-endian.
    uint8_t service_data[2 
        + sizeof(datapair.rpi.value) 
        + sizeof(datapair.aem.value)] =  { 0x6f, 0xfd };
    
    memcpy(service_data + 2, datapair.rpi.value, sizeof(datapair.rpi.value));
    memcpy(service_data + 2 + sizeof(datapair.rpi.value), datapair.aem.value, sizeof(datapair.aem.value));
    
    out.len = 0;        // haha, it was YOU! darn you, uninitialized values!

    tracer_ble_payload_add_record(&out, 0x01, flags,        sizeof(flags));         // set bluetooth flags
    tracer_ble_payload_add_record(&out, 0x03, uuid,         sizeof(uuid));          // set service uuid
    tracer_ble_payload_add_record(&out, 0x16, service_data, sizeof(service_data));  // set service data

    return out;
}

// returns the address of a newly generated temporary exposure key, and updates the current keypair
tracer_tek * tracer_derive_tek(uint32_t epoch) {
    tracer_tek * out = &tracer_tek_array[tracer_tek_array_head++];
    tracer_tek_array_head %= TEK_STORE_PERIOD;
    out->en_interval_num = tracer_en_interval_number(epoch);
    rng_gen(sizeof(out->value), out->value);

    tracer_current_keypair = tracer_derive_keypair(*out);

    return out;
}

// parses a ble payload and derives an rpi and aem from it. if the parsing is completed, the function returns true.
bool tracer_parse_ble_payload(tracer_ble_payload payload, tracer_datapair * datapair) {

    bool output_valid = false, payload_valid = false;
    for (uint8_t i = 0; i < payload.len;) {
        uint8_t record_len = payload.value[i++];

        if (i + record_len > payload.len) return false; // segfault bad. >:( this protects the parser from them.

        uint8_t type = payload.value[i++];

        uint8_t * data = payload.value + i;
        uint8_t data_len = record_len - 1;

        //printf("record @ idx %d of type 0x%02x with length %d. data: ", i, type, data_len);
        //print_hex_buffer(data, data_len);
        //printf("\n");

        switch (type) {
            case 0x03:  // service uuid
                payload_valid = *(uint16_t *)(data) == 0xFD6F; // check if the service id matches the contact tracing standard
            break;
            case 0x16:  // service data
                if (data_len == 2 + sizeof(datapair->rpi.value) + sizeof(datapair->aem.value)) {    // check if the package is the right size
                    if (datapair) {
                        memcpy(datapair->rpi.value, data + 2, sizeof(datapair->rpi.value));
                        memcpy(datapair->aem.value, data + 2 + sizeof(datapair->aem.value), sizeof(datapair->aem.value));
                    }
                    output_valid = true;
                }
            break;
        }
        i += data_len;
    }

    return output_valid && payload_valid;
}

/*  example data:
tek: 4c22bda759942e0758e47922ed4d6c3e
rpik: 1ab8a1141ecd1a6ee9f6a33b7296322a
aemk: e47c05afa4c51eeb286344f1aa634111
rpi: f603d5f00d002e8eba6cd65090e530ec
metadata: 40050000
aem: 29fbe29e
ble payload: 02011a03036ffd17166ffdf603d5f00d002e8eba6cd65090e530ec29fbe29e
*/

// checks if an rpi and aem matches a given temporary exposure key, and writes the metadata to an output if there is a match.
bool tracer_verify(tracer_datapair datapair, tracer_tek tek, tracer_metadata * output_metadata) {
    tracer_rpik rpik = tracer_derive_rpik(tek);

    uint8_t * decrypted_rpik = (uint8_t *)decrypt_aes_block(rpik.value, sizeof(rpik.value), datapair.rpi.value, NULL);

    //print_hex_buffer(decrypted_rpik, sizeof(rpik.value));
    //printf("\n");

    bool valid = memcmp(decrypted_rpik, RPI_STRING, sizeof(RPI_STRING)) == 0;

    if (valid && output_metadata) {
        uint32_t enin = *(uint32_t*)(decrypted_rpik + AES128_BLOCK_SIZE - sizeof(uint32_t));
        tracer_aemk aemk = tracer_derive_aemk(tek);
        flip_aes_block_ctr(aemk.value, AES128_KEY_SIZE, datapair.rpi.value, datapair.aem.value, sizeof(datapair.aem.value), output_metadata->value);
    }

    free(decrypted_rpik);

    return valid;
}

// derives a rpi-aem pair given an epoch and tx power.
tracer_datapair tracer_derive_datapair(uint32_t epoch, int8_t tx_power) {
    tracer_datapair out;

    tracer_metadata meta = tracer_derive_metadata(tx_power);

    out.rpi = tracer_derive_rpi(tracer_current_keypair.rpik, epoch);
    out.aem = tracer_derive_aem(tracer_current_keypair.aemk, out.rpi, meta);

    return out;
}

// self-tests the tracer library.
/*bool tracer_self_test(uint32_t epoch) {

    printf("deriving keys... ");

    tracer_tek * tek = tracer_derive_tek(epoch);

    tracer_tek_array_head = 0;
    
    tracer_rpik rpik = tracer_derive_rpik(*tek);
    tracer_aemk aemk = tracer_derive_aemk(*tek);

    tracer_rpi rpi = tracer_derive_rpi(rpik, epoch);

    tracer_metadata meta = tracer_derive_metadata(ble_adapter_get_adv_tx_power());
    tracer_aem aem = tracer_derive_aem(aemk, rpi, meta);

    tracer_ble_payload payload = tracer_derive_ble_payload(rpi, aem);

    printf("DONE!\n\ttek: ");
    print_hex_buffer(tek->value, sizeof(tek->value));
    printf("\n\trpik: ");
    print_hex_buffer(rpik.value, sizeof(rpik.value));
    printf("\n\taemk: ");
    print_hex_buffer(aemk.value, sizeof(aemk.value));
    printf("\n\trpi: ");
    print_hex_buffer(rpi.value, sizeof(rpi.value));
    printf("\n\tmetadata: ");
    print_hex_buffer(meta.value, sizeof(meta.value));
    printf("\n\taem: ");
    print_hex_buffer(aem.value, sizeof(aem.value));
    printf("\n\tble payload: ");
    print_hex_buffer(payload.value, sizeof(payload.value));
    printf("\n");

    tracer_datapair datapair;

    printf("testing tracer lib parsing... ");
    if (tracer_parse_ble_payload(payload, &datapair)) {
        printf("OK!\n\tparsed rpi: ");
        print_hex_buffer(datapair.rpi.value, sizeof(datapair.rpi.value));
        printf("\n\tparsed aem: ");
        print_hex_buffer(datapair.aem.value, sizeof(datapair.aem.value));
        printf("\n\toriginal rpi: ");
        print_hex_buffer(rpi.value, sizeof(rpi.value));
        printf("\n\toriginal aem: ");
        print_hex_buffer(aem.value, sizeof(aem.value));
        printf("\n");
    } else {
        printf("FAIL!\n");
        return false;
    }

    tracer_ble_payload random_payload;
    random_payload.len = esp_random() % 31;
    rng_gen(sizeof(random_payload.len), random_payload.value);

    // no way in heck the parser should be able to parse a completely random payload...
    printf("testing tracer lib parsing against random payload... ");
    if (tracer_parse_ble_payload(random_payload, NULL)) {
        printf("FAIL!\n\tpayload: ");
        print_hex_buffer(random_payload.value, random_payload.len);
        printf("\n");
        return false;
    } else printf("OK!\n");

    tracer_metadata out_meta;
    printf("testing tracer lib verification... ");
    if (tracer_verify(rpi, aem, *tek, &out_meta)) {
        printf("OK!\n\tdecrypted metadata: ");
        print_hex_buffer(&out_meta, sizeof(out_meta.value));
        printf("\n\toriginal metadata: ");
        print_hex_buffer(&meta, sizeof(meta.value));
        printf("\n");
    } else {
        printf("FAIL!\n");
        return false;
    }

    return true;
}*/

#endif