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

#define POWER_ANALYZER_BUFFER_SIZE (600)
#define POWER_ANALYZER_HISTO_SIZE (100)

typedef struct _mp_triac_power_analyzer_doublebuffer_obj_t {
    // for db[i_phase], this structure is modified constantly inside the interruption
    // for db[u_phase], this structure can be accessed in userspace, but only
    // while u_phase remains constant during the copy

    // Raw data buffers. Only to be used while in interruption
    int16_t voltage_buffer[POWER_ANALYZER_BUFFER_SIZE];
    int16_t current_buffer[POWER_ANALYZER_BUFFER_SIZE];
    // Histo: from a zero-crossing, sums the waveforms (minus offset)
    // for db[phase], it reflects ready data. For out-of-phase, it will be modified by interruption
    int32_t histo_voltage[POWER_ANALYZER_HISTO_SIZE];
    int32_t histo_current[POWER_ANALYZER_HISTO_SIZE];
    uint8_t histo_count;
    // Partial statistic calculated inside the interruption, while the data is still beeing sampled
    uint32_t i_sumV;
    uint32_t i_sumC;
    int32_t i_sumP;
    int16_t i_maxV;
    int16_t i_maxC;
    int32_t i_maxP;
    int16_t i_minV;
    int16_t i_minC;
    int32_t i_minP;
    uint16_t i_histoPos;
    uint16_t i_histoCount;
    int16_t i_lastV;
    uint32_t i_sqsumV;
    uint32_t i_sqsumC;
} mp_triac_power_analyzer_doublebuffer_obj_t;

typedef struct _mp_triac_power_analyzer_obj_t {
    mp_obj_base_t base;
    uint8_t voltage_pin;
    uint8_t current_pin;
    uint16_t sample_rate;
    float voltage_multiplier;
    float current_multiplier;
    float power_multiplier;

    uint8_t interrupt_count;
    uint8_t i_phase;
    uint8_t u_phase;
    uint16_t mbp; // Main Buffer Position
    mp_triac_power_analyzer_doublebuffer_obj_t db[2];
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
    struct repeating_timer timer;
} mp_triac_power_analyzer_obj_t;

extern const mp_obj_type_t mp_triac_controller_type;
extern const mp_obj_type_t mp_triac_power_analyzer_type;

void triac_global_init(void);

#endif // MICROPY_INCLUDED_EXTMOD_MODMACHINE_H
