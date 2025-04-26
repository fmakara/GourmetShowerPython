#ifndef MICROPY_INCLUDED_GRAPHICS_H
#define MICROPY_INCLUDED_GRAPHICS_H

#include <stdint.h>
#include "py/mphal.h"
#include "py/obj.h"
#include "pico/time.h"

typedef struct _mp_triac_controller_obj_t {
    mp_obj_base_t base;
    uint8_t sense_pin;
    uint8_t percent;
    uint32_t watchdog;
} mp_triac_controller_obj_t;

typedef struct _mp_triac_power_analyzer_obj_t {
    mp_obj_base_t base;
    uint8_t voltage_pin;
    uint8_t current_pin;
    uint16_t sample_rate;
    uint16_t analized_samples;
    uint16_t histo_size;
    float voltage_multiplier;
    float current_multiplier;
    float power_multiplier;

    uint8_t interrupt_count;
    // Histo: from a zero-crossing, sums the waveforms (minus offset)
    int32_t *histo_voltage;
    int32_t *histo_current;
    uint8_t histo_count;
    // Offset: avg(readings)
    uint16_t offset_voltage;
    uint16_t offset_current;
    // Positive peaks: max( (reading-offset) )
    int16_t pos_peak_voltage;
    int16_t pos_peak_current;
    int32_t pos_peak_power;
    // Negative peaks: max( -(reading-offset) )
    int16_t neg_peak_voltage;
    int16_t neg_peak_current;
    int32_t neg_peak_power;
    // Squaresum: sum( (readings-offset)^2 )
    uint32_t squaresum_voltage;
    uint32_t squaresum_current;
    int32_t sum_power;

    uint8_t running;
    int dma_chan;
    struct repeating_timer timer;
    
} mp_triac_power_analyzer_obj_t;

extern const mp_obj_type_t mp_triac_controller_type;
extern const mp_obj_type_t mp_triac_power_analyzer_type;

void triac_global_init(void);

#endif // MICROPY_INCLUDED_EXTMOD_MODMACHINE_H
