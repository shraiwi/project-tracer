#include "tracer_crypto.h"

#ifndef _TRACER_H_
#define _TRACER_H_

#define TAG "tracer_api"

/**
 * @file
 * @brief Contains all of the user-facing interactions in the Tracer API.
 */

// gets size (in bytes) of an object's member.
#define sizeof_member(type, member) sizeof(((type*)0)->member)

// settings (timing)
#define TRACER_SCAN_INTERVAL        1       // how many minutes in between scans
#define TRACER_TEK_INTERVAL         2       // how many minutes until a new tek is generated. must be divisible by TRACER_ENIN_INTERVAL.
#define TRACER_ENIN_INTERVAL        1       // how many minutes each eninterval is.
#define TRACER_SCAN_STORE_PERIOD    28      // how many days to store scans for
#define TRACER_TEK_STORE_PERIOD     14      // how many tek intervals to store the keys for.
#define TRACER_ENINS_PER_DAY        (TRACER_TEK_INTERVAL / TRACER_ENIN_INTERVAL)                                // how many enintervals are in a day
#define TRACER_MINUTES_PER_DAY      (24 * 60)                                                                   // how many minutes there are in a day
#define TRACER_SCAN_EXPIRY          (TRACER_MINUTES_PER_DAY * TRACER_SCAN_STORE_PERIOD / TRACER_SCAN_INTERVAL)  // how many scan intervals to store scans for
#define TRACER_ENIN_EXPIRY          (TRACER_TEK_STORE_PERIOD * TRACER_ENINS_PER_DAY)                            // how many enintervalnumbers to store data for

// settings (metadata)
#define TRACER_MAJOR_VERSION    1       // standard major version x.0
#define TRACER_MINOR_VERSION    0       // standard minor version 0.x

// constants
#define RPI_STRING  "EN-RPI"
#define RPIK_STRING "EN-RPIK"
#define AEM_STRING  "EN-AEM"
#define AEMK_STRING "EN-AEMK"

#if TRACER_TEK_INTERVAL % TRACER_ENIN_INTERVAL !=  0
#error The tracer tek interval must be divisible by the tracer enin interval!
#endif

/**
 * @brief A structure containing a Temporary Exposure Key and a UNIX Epoch signifying its creation time.
 */
typedef struct {
    uint32_t epoch;     /** The UNIX epoch time at the moment the TEK was generated. */
    uint8_t value[16];  /** The 16-byte value of the TEK */
} tracer_tek;

/**
 * @brief A Rolling Proximity Identifier
 */
typedef struct {
    uint8_t value[16];  /** The 16-byte value of the RPI */
} tracer_rpi;

/**
 * @brief A Rolling Proximity Identifier Key; used to encrypt Rolling Proximity Identifiers
 */
typedef struct {
    uint8_t value[16];  /** The 16-byte value of the RPIK */
} tracer_rpik;

/**
 * @brief The Associated Encrypted Metadata to an RPI
 */
typedef struct {
    uint8_t value[4];   /** The 4-byte value of the AEM */
} tracer_aem;

/**
 * @brief An Associated Encrypted Metadata Key; used to encrypt Associated Encrypted Metadata
 */
typedef struct {
    uint8_t value[16];  /** The 16-byte value of the RPIK */
} tracer_aemk;

/**
 * @brief A container for unencrypted metadata
 */
typedef struct {
    uint8_t value[4];   /** The unencrypted metadata. Typically includes the TX power and the API version */
} tracer_metadata;

/**
 * @brief An RPI and AEM pair
 */
typedef struct {
    tracer_rpi rpi;     /** The Rolling Proximity Identifier */
    tracer_aem aem;     /** The Rolling Proximity Identifier's associated metadata */
} tracer_datapair;

/**
 * @brief An RPIK and AEMK pair
 */
typedef struct {
    tracer_rpik rpik;   /** The Rolling Proximity Identifier Key */
    tracer_aemk aemk;   /** The Associated Encrypted Metadata Key */
} tracer_keypair;

/**
 * @brief A raw BLE advertising payload
 */
typedef struct {
    uint8_t value[31];  /** The raw data to be advertised */
    size_t len;         /** The length of the BLE payload */
} tracer_ble_payload;

tracer_tek tracer_tek_array[TRACER_TEK_STORE_PERIOD];
size_t tracer_tek_array_head = 0;

tracer_keypair tracer_current_keypair;

/**
 * @brief Adds a record to the BLE payload
 * 
 * @param ble_payload A pointer to the target BLE playload
 * @param type A byte representing the type of the BLE record
 * @param data A pointer to the raw record data
 * @param data_len The length of the record data
 */
void tracer_ble_payload_add_record(tracer_ble_payload * ble_payload, uint8_t type, void * data, size_t data_len) {
    ble_payload->value[ble_payload->len++] = data_len + 1;
    ble_payload->value[ble_payload->len++] = type;
    memcpy(&ble_payload->value[ble_payload->len], data, data_len);
    ble_payload->len += data_len;
}

/**
 * @brief Gets the ENIntervalNumber given an epoch
 * 
 * @param epoch The current UNIX epoch time
 * @return The ENIntervalNumber, expressed as a 32-bit unsigned integer
 */
uint32_t tracer_en_interval_number(uint32_t epoch) {
    return (uint32_t)(epoch / (60 * TRACER_ENIN_INTERVAL));
}

/**
 * @brief Gets the Scan Interval Number given an epoch
 * 
 * @param epoch The current UNIX epoch time
 * @return The Scan Interval Number, expressed as a 32-bit unsigned integer
 */
uint32_t tracer_scan_interval_number(uint32_t epoch) {
    return (uint32_t)(epoch / (60 * TRACER_SCAN_INTERVAL));
}

/**
 * @brief Derives a new Rolling Proximity Identifier Key from a Temporary Exposure Key
 * 
 * @param tek The Temporary Exposure Key to derive the RPI from
 * @return The output Rolling Proximity Identifier Key
 */
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

/**
 * @brief Encrypts a Rolling Proximity Identifier from an RPIK and UNIX epoch
 * 
 * @param rpik The Temporary Exposure Key to encrypt the RPI
 * @param epoch The current UNIX epoch time
 * @return An encrypted Tracer Rolling Proximity Identifier
 */
tracer_rpi tracer_derive_rpi(tracer_rpik rpik, uint32_t epoch) {
    tracer_rpi out;

    uint8_t padded_data[AES128_BLOCK_SIZE] = RPI_STRING;                             // just a note, the rest of the char array will be initialized to 0

    uint32_t enin = tracer_en_interval_number(epoch);                                // get the enintervalnumber

    memcpy(&padded_data[sizeof(padded_data) - sizeof(enin)], &enin, sizeof(enin));   // set the last 4 bytes of the padded date to the epoch

    encrypt_aes_block(rpik.value, AES128_KEY_SIZE, &padded_data, out.value);

    return out;
}

/**
 * @brief Compares the value of two datapairs
 * 
 * @param a A datapair to compare
 * @param b A datapair to compare
 * @return Whether or not the datapairs match
 */
bool tracer_compare_datapairs(tracer_datapair a, tracer_datapair b) {
    return memcmp(&a.aem.value, &b.aem.value, sizeof(a.aem.value)) == 0 && memcmp(&a.rpi.value, &b.rpi.value, sizeof(a.rpi.value)) == 0;
}

/**
 * @brief Derives unencrypted metadata
 * 
 * @param tx_power The transmitting power of the BLE radio, in dBm
 * @return Unenecrypted metadata
 */
tracer_metadata tracer_derive_metadata(int8_t tx_power) {
    tracer_metadata out;
    memset(out.value, 0, sizeof(out.value));

    out.value[0] =                              // major & minor versioning
        (TRACER_MAJOR_VERSION & 0b11) << 6 |
        (TRACER_MINOR_VERSION & 0b11) << 4;
    out.value[1] = tx_power;                    // transmit tx_power

    return out;
}

/**
 * @brief Derives an corresponding Associated Encrypted Metadata Key from a Temporary Exposure Key
 * 
 * @param tek The TEK to derive the AEMK from
 * @return The TEK's corresponding AEMK
 */
tracer_aemk tracer_derive_aemk(tracer_tek tek) {
    tracer_aemk out;

    uint8_t info[AES128_BLOCK_SIZE] = AEMK_STRING;

    hkdf(tek.value, sizeof(tek.value),
        NULL, 0, 
        info, sizeof(info), sizeof(out.value), out.value);

    return out;
}

/**
 * @brief Encrypts metadata given an Associated Encrypted Metadata Key and Rolling Proximity Identifier
 * 
 * @param aemk The AEMK to encrypt the metadata with
 * @param rpi The RPI to associate the metadata with
 * @param metadata The source metadata
 * @return The 4-byte encrypted metadata
 */
tracer_aem tracer_derive_aem(tracer_aemk aemk, tracer_rpi rpi, tracer_metadata metadata) {
    tracer_aem out;

    flip_aes_block_ctr(aemk.value, AES128_KEY_SIZE, rpi.value, metadata.value, sizeof(metadata.value), out.value);

    return out;
}

/**
 * @brief Derives a tracer keypair for generating an RPI and AEM
 * 
 * @param tek The Temporary Exposure Key to derive the keypair from
 * @return A keypair containing both an Rolling Proximity Identifier Key and Associated Encrypted Metadata Key
 */
tracer_keypair tracer_derive_keypair(tracer_tek tek) {
    tracer_keypair out;
    out.rpik = tracer_derive_rpik(tek);
    out.aemk = tracer_derive_aemk(tek);
    return out;
}

/**
 * @brief Derives a new raw BLE payload given a datapair.
 * 
 * @param datapair The datapair to generate the BLE payload from
 * @return A raw bluetooth payload containing all required data
 */
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

/**
 * @brief Generates a new Temporary Exposure Key and updates the latest keypair
 * 
 * @param epoch The current UNIX epoch time
 * @return A pointer to the latest TEK
 */
tracer_tek * tracer_derive_tek(uint32_t epoch) {
    tracer_tek * out = &tracer_tek_array[tracer_tek_array_head++];
    tracer_tek_array_head %= TRACER_TEK_STORE_PERIOD;
    out->epoch = epoch;
    rng_gen(sizeof(out->value), out->value);

    tracer_current_keypair = tracer_derive_keypair(*out);

    return out;
}

/**
 * @brief Gets the latest Temporary Exposure Key generated
 * 
 * @return The the latest Temporary Exposure Key
 */
tracer_tek tracer_get_latest_tek() {
    return tracer_tek_array[(tracer_tek_array_head ? tracer_tek_array_head : TRACER_TEK_STORE_PERIOD) - 1];
}

/**
 * @brief Attempts to parse a raw BLE adverising payload
 * 
 * @param payload The raw bluetooth payload to parse
 * @param datapair A pointer to a datapair which will be overwritten with the drived datapair in the case that the data is valid.
 * @return Whether or not the parsing was successful
 */
bool tracer_parse_ble_payload(tracer_ble_payload payload, tracer_datapair * datapair) {

    bool output_valid = false, payload_valid = false;
    for (uint8_t i = 0; i < payload.len;) {
        uint8_t record_len = payload.value[i++];

        if (i + record_len > payload.len) return false; // segfault bad. >:( this protects the parser from them.

        uint8_t type = payload.value[i++];

        uint8_t * data = payload.value + i;
        uint8_t data_len = record_len - 1;

        switch (type) {
            case 0x03:  // service uuid
                payload_valid = *(uint16_t *)(data) == 0xFD6F; // check if the service id matches the contact tracing standard
            break;
            case 0x16:  // service data
                if (data_len == 2 + sizeof(datapair->rpi.value) + sizeof(datapair->aem.value)) {    // check if the package is the right size
                    if (datapair) {
                        memcpy(datapair->rpi.value, data + 2, sizeof(datapair->rpi.value));
                        memcpy(datapair->aem.value, data + 2 + sizeof(datapair->rpi.value), sizeof(datapair->aem.value));
                    }
                    output_valid = true;
                }
            break;
        }
        i += data_len;
    }

    return output_valid && payload_valid;
}

/**
 * @brief Detects if there has been an ENIntervalNumber rollover
 * 
 * @param last_epoch The last time the ENIntervalNumber changed, as a 32-bit unsigned UNIX epoch
 * @param current_epoch The current UNIX epoch time
 * @return Whether or not there should be a new datapair derivation
 */
bool tracer_detect_enin_rollover(uint32_t last_epoch, uint32_t current_epoch) {
    return (tracer_en_interval_number(last_epoch) < tracer_en_interval_number(current_epoch));
}

/**
 * @brief Detects whether or not there should be a BLE scan performed.
 * 
 * @param last_epoch The last time a scan was performed, as a 32-bit unsigned UNIX epoch
 * @param current_epoch The current UNIX epoch time
 * @return Whether or not there should be a BLE scan performed.
 */
bool tracer_detect_scanin_rollover(uint32_t last_epoch, uint32_t current_epoch) {
    return (tracer_scan_interval_number(last_epoch) < tracer_scan_interval_number(current_epoch));
}

/**
 * @brief Detects if there needs to be a new TEK generated
 * 
 * @param last_epoch The last time a TEK was generated, as a 32-bit unsigned UNIX epoch
 * @param current_epoch The current UNIX epoch time
 * @return Whether or not a new TEK should be generated.
 */
bool tracer_detect_tek_rollover(uint32_t last_epoch, uint32_t current_epoch) {
    uint32_t last_enin = tracer_en_interval_number(last_epoch), current_enin = tracer_en_interval_number(current_epoch);
    return ((current_enin > last_enin) && ((current_enin % TRACER_ENINS_PER_DAY) == 0)) || (current_enin - last_enin >= TRACER_ENINS_PER_DAY);
}

/**
 * @brief Checks if a scanned RPI and AEM pair matches a downloaded TEK.
 * 
 * @param datapair The input scanned datapair, which can derived from tracer_parse_ble_payload().
 * @param tek The Temporary Exposure Key to test the datapair against.
 * @param enin A pointer to a 32-bit unsigned integer which will be overwritten with the datapair's generation ENIntervalNumber in the case of a TEK match
 * @param output_metadata A pointer to a metadata object which will be overwritten with the datapair's decrypted metadata in the case of a TEK match
 * @return Whether or not the datapair was successfully decrypted
 */
bool tracer_verify(tracer_datapair datapair, tracer_tek tek, uint32_t * enin, tracer_metadata * output_metadata) {
    tracer_rpik rpik = tracer_derive_rpik(tek);

    uint8_t * decrypted_rpi = (uint8_t *)decrypt_aes_block(rpik.value, sizeof(rpik.value), datapair.rpi.value, NULL);

    bool valid = memcmp(decrypted_rpi, RPI_STRING, sizeof(RPI_STRING)) == 0;

    if (valid) {
        if (enin) *enin = *(uint32_t*)(decrypted_rpi + AES128_BLOCK_SIZE - sizeof(uint32_t));
        if (output_metadata) {
            tracer_aemk aemk = tracer_derive_aemk(tek);
            flip_aes_block_ctr(aemk.value, AES128_KEY_SIZE, datapair.rpi.value, datapair.aem.value, sizeof(datapair.aem.value), output_metadata->value);
        }
    }

    free(decrypted_rpi);

    return valid;
}

/**
 * @brief Derives a RPI-AEM pair given a unix epoch and tx power. This can be used to create bluetooth payloads.
 * 
 * @param epoch The UNIX epoch time, expressed as a 32-bit unsigned integer
 * @param tx_power The transmitting power of the transmitter in dBm, expressed as an 8-bit signed integer
 */
tracer_datapair tracer_derive_datapair(uint32_t epoch, int8_t tx_power) {
    tracer_datapair out;

    tracer_metadata meta = tracer_derive_metadata(tx_power);

    out.rpi = tracer_derive_rpi(tracer_current_keypair.rpik, epoch);
    out.aem = tracer_derive_aem(tracer_current_keypair.aemk, out.rpi, meta);

    return out;
}

#undef TAG

#endif