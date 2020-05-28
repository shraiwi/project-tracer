#include "stdlib.h"
#include "string.h"

// take a look at the sds library to see the smart tricks that are the backbone of this library :)
// https://github.com/antirez/sds

#ifndef _CVEC_H_
#define _CVEC_H_

#define CVEC_INITIAL_SIZE 4

// creates a new cvec array given a type
#define cvec_arrayof(type) _cvec_allocate(sizeof(type))

// appends an item to the end of an array
#define cvec_append(array, item) do { typeof(item) _temp = item; _cvec_append((void **)&array, &_temp); } while(0)

// deletes the last item of an array
#define cvec_shrink(array) _cvec_shrink((void**)&array)

// resizes an array
#define cvec_resize(array, new_len) _cvec_resize((void **)&array, new_len)

// reallocates an array so that it takes up the minimum space required to hold its items in memory
#define cvec_crunch(array) _cvec_crunch((void**)&array)

// frees a given array
#define cvec_free(array) _cvec_free((void**)&array)

typedef struct {
    size_t item_len;
    size_t item_capacity;
    size_t head;
} cvec_header;

// allocates a new cvec array and returns a pointer to the first item
void * _cvec_allocate(size_t item_len) {
    cvec_header * header = (cvec_header *)malloc(sizeof(cvec_header) + item_len * CVEC_INITIAL_SIZE);
    header->item_len = item_len;
    header->item_capacity = CVEC_INITIAL_SIZE;
    header->head = 0;
    return &header[1];
}

// appends an item to the end of a cvec but eeEEeWW pOInTErss
void _cvec_append(void ** cvec, void * item) {
    cvec_header * header = (cvec_header *)(*cvec - sizeof(cvec_header));
    if (header->head == header->item_capacity) {
        header->item_capacity = (header->item_capacity * 3) >> 1;
        header = (cvec_header *)realloc(header, sizeof(cvec_header) + header->item_len * header->item_capacity);
        *cvec = &header[1];
    }
    memcpy(*cvec + header->item_len * header->head++, item, header->item_len);
}

// reallocates an array so that it takes up the minimum amount of memory space possible
void _cvec_crunch(void **cvec) {
    cvec_header * header = (cvec_header *)(*cvec - sizeof(cvec_header));
    
    header->item_capacity = header->head;
    header = (cvec_header *)realloc(header, sizeof(cvec_header) + header->item_len * header->item_capacity);
    *cvec = &header[1];
}

// resizes a cvec to a different size
void _cvec_resize(void ** cvec, size_t new_len) {
    cvec_header * header = (cvec_header *)(*cvec - sizeof(cvec_header));

    header->item_capacity = new_len;
    header = (cvec_header *)realloc(header, sizeof(cvec_header) + header->item_len * header->item_capacity);
    *cvec = &header[1];
}

// shrinks an array by one
void _cvec_shrink(void ** cvec) {
    cvec_header * header = (cvec_header *)(*cvec - sizeof(cvec_header));

    header->item_capacity--;
    header = (cvec_header *)realloc(header, sizeof(cvec_header) + header->item_len * header->item_capacity);
    *cvec = &header[1];
}

// get the allocated memory in an array
size_t cvec_get_allocated(void * cvec) {
    return ((cvec_header *)(cvec - sizeof(cvec_header)))->item_capacity;
}

// gets the number of items in an array
size_t cvec_sizeof(void * cvec) {
    return ((cvec_header *)(cvec - sizeof(cvec_header)))->head;
}

// deletes a given array
void _cvec_free(void ** cvec) {
    free(*cvec - sizeof(cvec_header));
    *cvec = NULL;
}

// deletes an item from an array.
void cvec_delete(void * cvec, size_t index) {
    cvec_header * header = (cvec_header *)(cvec - sizeof(cvec_header));
    memmove(cvec + header->item_len * index, cvec + header->item_len * (index + 1), header->item_len * (header->head - index - 1));
    header->head--;
}

#endif