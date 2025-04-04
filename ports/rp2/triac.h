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
    uint32_t percent;
    uint32_t watchdog;
} mp_triac_power_analyzer_obj_t;

extern const mp_obj_type_t mp_triac_controller_type;
extern const mp_obj_type_t mp_triac_power_analyzer_type;

void triac_global_init(void);

#endif // MICROPY_INCLUDED_EXTMOD_MODMACHINE_H
