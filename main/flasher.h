#include "cvec.h"
#include "stdint.h"
#include "limits.h"
#include "stdio.h"

#ifndef _FLASHER_H_
#define _FLASHER_H_

#define FLASHER_BUF_SIZE 7  // 1<<7 bits (128 samples). uses powers of two for maximum performance!

int8_t flasher_last_value;

uint16_t flasher_adc_buf[1 << FLASHER_BUF_SIZE];
uint16_t flasher_adc_buf_head = 0;  // the head of the adc buffer

uint16_t flasher_min_range;         // the minimum allowable range

int64_t flasher_last_bit_flip;      // the epoch of the last bit flip
int64_t flasher_last_sample_epoch;  // the epoch of the last sample
int64_t flasher_min_bit_period;     // the minimum bit period
int64_t flasher_bin_size;           // the time divisions per bin
int64_t flasher_sample_period;      // the epoch time interval

// configures the flasher api.
void flasher_config(uint16_t min_range, int64_t min_bit_period, int64_t bin_size, int64_t sample_period, int64_t epoch) {
    
    flasher_last_value = -1;
    
    memset(flasher_adc_buf, 0, sizeof(flasher_adc_buf));    // clear the flasher buffer.

    flasher_min_range = min_range;

    flasher_min_bit_period = min_bit_period;
    flasher_bin_size = bin_size;
    flasher_sample_period = sample_period;

    flasher_last_bit_flip = epoch;
    flasher_last_sample_epoch = epoch;
}

// feed data to the flasher reader and get a bit out of it. a -1 is a null bit and can be ignored.
int8_t flasher_feed(uint16_t adc_sample, int64_t epoch) {

    if (epoch - flasher_last_sample_epoch < flasher_sample_period) return -1;

    flasher_adc_buf[flasher_adc_buf_head++] = adc_sample;
    flasher_adc_buf_head &= (1 << FLASHER_BUF_SIZE) - 1;

    flasher_last_sample_epoch = epoch;

    uint32_t adc_buf_sum = 0, adc_buf_avg;
    uint16_t adc_buf_min = UINT16_MAX, adc_buf_max = 0, adc_buf_range;

    for (uint16_t i = 0; i < (1 << FLASHER_BUF_SIZE); i++) {
        adc_buf_sum += flasher_adc_buf[i];

        if (flasher_adc_buf[i] < adc_buf_min) adc_buf_min = flasher_adc_buf[i];
        if (flasher_adc_buf[i] > adc_buf_max) adc_buf_max = flasher_adc_buf[i];
    }

    adc_buf_avg = adc_buf_sum >> FLASHER_BUF_SIZE;
    adc_buf_range = adc_buf_max - adc_buf_min;

    int8_t current_value = adc_sample > adc_buf_avg;

    if (flasher_last_value < 0) flasher_last_value = current_value;

    int8_t out_bit = -1;

    if (flasher_last_value != current_value) {
        int64_t dt = epoch - flasher_last_bit_flip;
        if (dt > flasher_min_bit_period) {
            out_bit = ((dt + (flasher_bin_size / 2)) / flasher_bin_size) - 1;
            flasher_last_bit_flip = epoch;
        }
    }

    flasher_last_value = current_value;

    return out_bit;
}

#endif