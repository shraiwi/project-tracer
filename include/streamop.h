#include "string.h"

// simple stream operations (can be used for matching streaming datatypes)

#ifndef _STREAMOP_H_
#define _STREAMOP_H_

typedef struct {
    size_t data_head;   // the data head of the token
    size_t data_len;    // the length of the streamop's data
    void * data;        // a pointer representing the streamop's data
} streamop_token;

typedef enum {
    STREAMOP_NULL = 0,  // means nothing, still searching
    STREAMOP_MATCH,     // token match
    STREAMOP_CHUNK_OK,  // the chunk is valid
} streamop_result;

// creates a streamop matcher given a null-termiated string. the streamop matcher will not inlude the null character.
streamop_token streamop_create_token_from_str(const char * str) {
    streamop_token out = { 0 };
    out.data = (void*)str;
    out.data_len = strlen(str);
    return out;
}

// creates a streamop token which can be used to split a stream into chunks of len bytes wide.
streamop_token streamop_create_chunk_token(void * data, size_t len) {
    streamop_token out = { 0 };
    out.data = data;
    out.data_len = len;
    return out;
}

// feeds one character to a token matcher.
streamop_result streamop_match_character(streamop_token * token, char c) {
    int match = ((char*)token->data)[token->data_head] == c;
    if (!match) {
        token->data_head = 0;
        return STREAMOP_NULL;
    }
    token->data_head += match;
    if (token->data_head == token->data_len) {
        token->data_head = 0;
        return STREAMOP_MATCH;
    }
    return STREAMOP_NULL;
}

// splits a stream into chunks
streamop_result streamop_chunk_character(streamop_token * token, char c) {
    ((char *)token->data)[token->data_head++] = c;
    if (token->data_head == token->data_len) {
        token->data_head = 0;
        return STREAMOP_CHUNK_OK;
    }
    return STREAMOP_NULL;
}

#endif