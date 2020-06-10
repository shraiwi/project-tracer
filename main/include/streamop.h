#include "string.h"

// simple stream operations (can be used for matching streaming datatypes)

#ifndef _STREAMOP_H_
#define _STREAMOP_H_

typedef struct {
    size_t data_head;
    size_t data_len;
    void * data;
} streamop_match_token;

typedef enum {
    STREAMOP_NULL = 0,  // means nothing, still searching
    STREAMOP_MATCH,     // token match
    STREAMOP_MISMATCH,  // token mismatch
} streamop_match_result;

// feeds one character to a matcher
streamop_match_result streamop_match_character(streamop_match_token * token, char c) {
    int match = ((char*)token->data)[token->data_head] == c;
    if (!match) {
        token->data_head = 0;
        return STREAMOP_MISMATCH;
    }
    token->data_head += match;
    if (token->data_head == token->data_len) {
        token->data_head = 0;
        return STREAMOP_MATCH;
    }
    return STREAMOP_NULL;
}

// creates a streamop matcher given a null-termiated string. the streamop matcher will not inlude the null character.
streamop_match_token streamop_token_from_str(const char * str) {
    streamop_match_token out = { 0 };
    out.data = (void*)str;
    out.data_len = strlen(str);
    return out;
}

#endif