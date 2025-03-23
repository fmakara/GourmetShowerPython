#ifndef MICROPY_INCLUDED_GRAPHICS_H
#define MICROPY_INCLUDED_GRAPHICS_H

#include <stdint.h>
#include "py/mphal.h"
#include "py/obj.h"
#include "pico/time.h"

typedef struct _mp_triac_controller_obj_t {
    mp_obj_base_t base;
    uint8_t sense_pin;
    uint8_t trigger_pin;
    uint8_t percent;
} mp_triac_controller_obj_t;

extern const mp_obj_type_t mp_triac_controller_type;

void triac_global_init(void);

#endif // MICROPY_INCLUDED_EXTMOD_MODMACHINE_H
