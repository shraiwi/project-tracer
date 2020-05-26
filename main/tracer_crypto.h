#include "esp_system.h"

#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"
#include "mbedtls/hkdf.h"

#include "string.h"
#include "stdlib.h"
#include "stdint.h"

#ifndef _TRACER_CRYPTO_H_
#define _TRACER_CRYPTO_H_

/**
 * --- cryptographic functions ---
 * this file provides a standard api for all of the cryptographic functions outlined in the preliminary standard (v1.2)
**/

#define SHA256_HASH_SIZE 32
#define AES128_BLOCK_SIZE 16
#define AES128_KEY_SIZE 16

// not defined in the standard, but it's implemented.
uint8_t * zeroes_gen(size_t len, void * output) {
    uint8_t * out;
    if (output == NULL) return (uint8_t*)calloc(len, 1);
    else out = (uint8_t*)output;

    memset(out, 0, len);
    return out;
}

// defined as CRNG(OutputLength) in the standard.
uint8_t * rng_gen(size_t len, void * output) {

    uint8_t * out;
    if (output == NULL) out = (uint8_t*)malloc(len);
    else out = (uint8_t*)output;

    esp_fill_random(out, len);
    return out;
}


// passed tests!

// defined as HKDF(Key, Salt, Info, OutputLength) in the standard.
uint8_t * hkdf(void * key, size_t key_len, void * salt, size_t salt_len, void * info, size_t info_len, size_t out_len, void * output) {

    uint8_t * hash;
    if (output == NULL) hash = (uint8_t*)malloc(SHA256_HASH_SIZE);
    else hash = (uint8_t*)output;

    mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 
        (const unsigned char *)salt, salt_len, 
        (const unsigned char *)key, key_len, 
        (const unsigned char *)info, info_len, 
        hash, out_len);

    return hash;
}
// passed tests!

// defined as AES128(Key, Data) in the standard. assumes iv is always an array of zeroes, because the standard doesnt define what mode the AES engine is running in.
uint8_t * encrypt_aes_block(void * key, size_t key_len, void * data, void * output) {

    uint8_t * block;
    if (output == NULL) block = (uint8_t*)malloc(AES128_BLOCK_SIZE);
    else block = (uint8_t*)output;
    
    //uint8_t * iv = rng_gen(16, NULL);

    mbedtls_aes_context ctx;

    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key, key_len*8);
    //mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, AES128_BLOCK_SIZE, iv, data, block);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, (const unsigned char *)data, block);
    mbedtls_aes_free(&ctx);

    //free(iv);
    
    return block;
}

// decrypts a single aes block in ecb mode
uint8_t * decrypt_aes_block(void * key, size_t key_len, void * data, void * output) {

    uint8_t * block;
    if (output == NULL) block = (uint8_t*)malloc(AES128_BLOCK_SIZE);
    else block = (uint8_t*)output;
    
    //uint8_t * iv = rng_gen(16, NULL);

    mbedtls_aes_context ctx;

    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key, key_len*8);
    //mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, AES128_BLOCK_SIZE, iv, data, block);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, (const unsigned char *)data, block);
    mbedtls_aes_free(&ctx);

    //free(iv);
    
    return block;
}


// passed tests!

/** defined as AES128-CTR(key, iv, data) in the standard. 
 * this operation is "flippable", meaning if you put the output from a prior function into the input of this function, you will get the original data back (assuming your key & iv are the same).
*/
uint8_t * flip_aes_block_ctr(void * key, size_t key_len, uint8_t iv[16], void * data, size_t data_len, void * output) {
    
    uint8_t * block;
    if (output == NULL) block = (uint8_t*)malloc(data_len);
    else block = (uint8_t*)output;

    mbedtls_aes_context ctx;

    size_t nc_off = 0;
    uint8_t stream_block[16] = "";

    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key, key_len*8);
    mbedtls_aes_crypt_ctr(&ctx, data_len, &nc_off, iv, stream_block, (const unsigned char *)data, block);
    mbedtls_aes_free(&ctx);
    
    return block;
}

#endif