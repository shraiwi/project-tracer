#include "stdio.h"
#include "string.h"

#include "esp_log.h"

#ifndef _UTILS_H_
#define _UTILS_H_

#define GET_BIT(n) (1<<n)

// prints a hex buffer to stdout.
void print_hex_buffer(void * data, size_t data_len) {
    for (int i = 0; i < data_len; i++) {
        printf("%02x", ((uint8_t*)data)[i]);
    }
}


// concatenates 2 strings
char * string_concat(char * a, char * b) {
    size_t size_a = strlen(a), size_b = strlen(b);
    char * out = (char *)malloc(size_a + size_b + 1);
    memcpy(out, a, size_a);
    memcpy(out + size_a, b, size_b);
    return out;
}

#endif