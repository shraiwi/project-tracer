#include "esp_system.h"
#include "esp_random.h"

#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/base64.h"

#include "string.h"
#include "stdlib.h"
#include "stdint.h"

#ifndef _TRACER_CRYPTO_H_
#define _TRACER_CRYPTO_H_

/**
 * @file 
 * @brief A standard API for all of the cryptographic functions outlined in the preliminary standard (v1.2)
**/

#define SHA256_HASH_SIZE 32
#define AES128_BLOCK_SIZE 16
#define AES128_KEY_SIZE 16

/**
 * @brief Fills or allocates a buffer with zeroes.
 * 
 * @param len The length of the buffer, in bytes.
 * @param output  If NULL, the function will allocate a buffer of size @p len and perform the operation on it.
 * @return A pointer to the given/allocated buffer
 */
uint8_t * zeroes_gen(size_t len, void * output) {
    uint8_t * out;
    if (output == NULL) return (uint8_t*)malloc(len);
    else out = (uint8_t*)output;

    memset(out, 0, len);
    return out;
}

/**
 * @brief Fills or allocates a buffer with crypotgraphically-secure random numbers.
 * 
 * Defined as CRNG(OutputLength) in the standard.
 * 
 * @param len The length of the buffer, in bytes
 * @param output A pointer to where the buffer is located. If NULL, the function will allocate a buffer of size  @p len and perform the operation on it.
 * @return A pointer to the given/allocated buffer
 */
uint8_t * rng_gen(size_t len, void * output) {

    uint8_t * out;
    if (output == NULL) out = (uint8_t*)malloc(len);
    else out = (uint8_t*)output;

    esp_fill_random(out, len);
    return out;
}


/**
 * @brief A simple key-derivation function.
 * 
 * Defined as HKDF(Key, Salt, Info, OutputLength) in the standard. Derives an encryption key from a master key.
 * 
 * @param key A pointer to a buffer containing the key.
 * @param key_len The size of the key buffer, in bytes.
 * @param salt A pointer to a buffer containing the cryptographic salt.
 * @param salt_len The size of the salt buffer, in bytes.
 * @param info A pointer to a buffer containing the information to be encoded in the key.
 * @param info_len The size of the info buffer, in bytes.
 * @param out_len The size of the output buffer, in bytes.
 * @param output A pointer to where the output buffer is located. If NULL, the function will allocate a buffer of size @p out_len and write the key there.
 * @return A pointer to a buffer containing the output key.
 */
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

/**
 * @brief Encrypts a single block in AES mode.
 * 
 * Defined as AES128(Key, Data) in the standard. It uses ECB mode because the IVs are not shared between devices.
 * 
 * @param key A pointer to a buffer containing the key.
 * @param key_len The size of the key buffer, in bytes.
 * @param data A pointer to a 16-byte long buffer containing the data to encrypt.
 * @param output A pointer to where the output buffer is located. If NULL, the function will allocate a buffer of size 16 bytes and write the encrypted data there.
 * @return A pointer to a buffer containing the encrypted block.
 */
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

/**
 * @brief Decrypts a single block in AES mode.
 * 
 * Defined as AES128(Key, Data) in the standard. It uses ECB mode because the IVs are not shared between devices.
 * 
 * @param key A pointer to a buffer containing the key.
 * @param key_len The size of the key buffer, in bytes.
 * @param data A pointer to a 16-byte long buffer containing the data to decrypt.
 * @param output A pointer to where the output buffer is located. If NULL, the function will allocate a buffer of size 16 bytes and write the decrypted data there.
 * @return A pointer to a buffer containing the decrypted block.
 */
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


/**
 * @brief Performs a AES-CTR operation on a variable-length block.
 * 
 * Defined as AES128-CTR(key, iv, data) in the standard. This operation is symmetrical, like XORing. 
 * If the operation is performed twice on the same data (with the same arguments) the original data
 * can be recovered.
 * 
 * @param key A pointer to a buffer containing the key.
 * @param key_len The size of the key buffer, in bytes.
 * @param iv A pointer to a 16-byte buffer containing the initialization variable
 * @param data A pointer to a buffer containing the data to flip.
 * @param data_len The size of the data buffer, in bytes.
 * @param output A pointer to where the output buffer is located. If NULL, the function will allocate a buffer of size @p data_len and write the decrypted data there.
 * @return A pointer to a buffer containing the flipped data.
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

/**
 * @brief Calculates the size of a null-terminated base64-encoded string
 * 
 * @param src_size The size of the source binary, in bytes.
 * @return The expected size of the null-terminated base64-encoded string, in bytes.
 */
size_t b64_encoded_size(size_t src_size) {
    return ((src_size + 2) / 3) * 4 + 1;    // +1 for null-termination
}

/**
 * @brief Calculates the size of a base64-decoded buffer
 * 
 * @param src_size The size of the null-terminated source string, in bytes.
 * @return The expected size of the base64-decoded buffer, in bytes.
 */
size_t b64_decoded_size(size_t src_size) {
    return ((src_size + 3) / 4) * 3;
}

/**
 * @brief Converts a binary buffer to a null-terminated base64 string.
 * 
 * @param data A pointer to a buffer containing the data to encode.
 * @param data_len The size of the data buffer, in bytes.
 * @return A pointer to a null-terminated string containing the encoded base64 string. This is allocated on the heap and should be freed after use.
 */
char * b64_encode(void * data, size_t data_len) {

    size_t output_len = b64_encoded_size(data_len);

    char * out = (char*)malloc(output_len);

    size_t written_bytes;

    mbedtls_base64_encode((unsigned char *)out, output_len, &written_bytes, (const unsigned char *)data, data_len);

    out = realloc(out, written_bytes + 1);
    out[written_bytes] = '\0';

    return out;
}

/**
 * @brief Converts a null-teriminated base64 string into a binary buffer.
 * 
 * @param data A pointer to a buffer containing the null-terminated base64 string to decode.
 * @param out_len A pointer to a size_t to write the size of the output buffer to.
 * @return A pointer to a buffer containing the decoded output data. This is allocated on the heap and should be freed after use.
 */
uint8_t * b64_decode(void * data, size_t * out_len) {

    size_t data_len = strlen(data);
    size_t output_len = b64_decoded_size(data_len);

    uint8_t * out = malloc(output_len);

    size_t written_bytes;

    mbedtls_base64_decode((unsigned char *)out, output_len, &written_bytes, (const unsigned char *)data, data_len);

    out = realloc(out, written_bytes);

    if (out_len) *out_len = written_bytes;

    return out;
}

#endif