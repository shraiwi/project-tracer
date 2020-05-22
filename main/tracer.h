#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"

#define MBEDTLS_HKDF_C

#include "mbedtls/hkdf.h"
//#include "mbedtls/md.h"
#include "stdlib.h"

#ifndef _TRACER_H_
#define _TRACER_H_

/**
 * --- cryptographic functions ---
 * probably should move these to a separate file
 * 
 * these basically provide all of the cryptographic functions outlined in the preliminary standard (v1.2)
**/

#define SHA256_HASH_SIZE 32
#define AES128_BLOCK_SIZE 16
#define AES128_KEY_SIZE 16

// not defined in the standard, but it's implemented.
uint8_t * zeroes_gen(size_t len, void * output) {
    uint8_t * out;
    if (output == NULL) return (uint8_t*)calloc(len, 1);
    else out = output;

    memset(out, 0, len);
    return out;
}

// defined as CRNG(OutputLength) in the standard.
uint8_t * rng_gen(size_t len, void * output) {

    uint8_t * out;
    if (output == NULL) out = (uint8_t*)malloc(len);
    else out = output;

    esp_fill_random(out, len);
    return out;
}

// passed tests!

// not defined in the standerd, but it's implemented.
uint8_t * sha256(void * data, size_t data_len, void * output) {
    uint8_t * hash;
    if (output == NULL) hash = (uint8_t*)malloc(SHA256_HASH_SIZE);
    else hash = output;

    mbedtls_sha256_ret((const unsigned char *)data, data_len, hash, false);

    return hash;
}


// passed tests!

// defined as HKDF(Key, Salt, Info, OutputLength) in the standard.
uint8_t * hkdf(void * key, size_t key_len, void * salt, size_t salt_len, void * info, size_t info_len, size_t out_len, void * output) {

    uint8_t * hash;
    if (output == NULL) hash = (uint8_t*)malloc(SHA256_HASH_SIZE);
    else hash = output;

    mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 
        (const unsigned char *)salt, salt_len, 
        (const unsigned char *)key, key_len, 
        (const unsigned char *)info, info_len, 
        hash, out_len);

    return hash;
}
// passed tests!

// defined as AES128(Key, Data) in the standard. assumes iv is always an array of zeroes, because it doesnt define what mode the AES is running in.
uint8_t * encrypt_aes_block(void * key, size_t key_len, void * data, void * output) {

    uint8_t * block;
    if (output == NULL) block = (uint8_t*)malloc(AES128_BLOCK_SIZE);
    else block = output;
    
    //uint8_t * iv = rng_gen(16, NULL);

    mbedtls_aes_context ctx;

    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key, key_len*8);
    //mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, AES128_BLOCK_SIZE, iv, data, block);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, data, block);
    mbedtls_aes_free(&ctx);

    //free(iv);
    
    return block;
}


// passed tests!

// defined as AES128-CTR(key, iv, data) in the standard.
uint8_t * encrypt_aes_block_ctr(void * key, size_t key_len, uint8_t iv[16], void * data, size_t data_len, void * output) {
    
    uint8_t * block;
    if (output == NULL) block = (uint8_t*)malloc(data_len);
    else block = output;

    mbedtls_aes_context ctx;

    size_t nc_off = 0;
    uint8_t stream_block[16];

    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key, key_len*8);
    mbedtls_aes_crypt_ctr(&ctx, data_len, &nc_off, iv, stream_block, data, block);
    mbedtls_aes_free(&ctx);
    
    return block;
}

/**
 * tracer API
 */

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

// remember: TEK + epoch => RPIK => RPI + AEM

uint32_t tracer_tek_rolling_period = 144;
tracer_tek tracer_tek_array[TEK_STORE_PERIOD];
size_t tracer_tek_array_head = 0;

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

    memcpy(padded_data + sizeof(padded_data) - sizeof(enin), &enin, sizeof(enin));   // set the last 4 bytes of the padded date to the epoch

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

    hkdf(tek.value, sizeof(tek.value), NULL, 0, info, sizeof(info), sizeof(out.value), out.value);

    return out;
}

tracer_aem tracer_derive_aem(tracer_aemk aemk, tracer_rpi rpi, tracer_metadata metadata) {
    tracer_aem out;

    encrypt_aes_block_ctr(aemk.value, sizeof(aemk.value), rpi.value, metadata.value, sizeof(metadata.value), out.value);

    return out;
}

// returns the address of a newly generated temporary exposure key
tracer_tek * tracer_gen_tek(uint32_t epoch) {
    tracer_tek * out = &tracer_tek_array[tracer_tek_array_head++];
    tracer_tek_array_head %= TEK_STORE_PERIOD;
    out->en_interval_num = tracer_en_interval_number(epoch);
    rng_gen(sizeof(out->value), out->value);
    return out;
}

#endif