#ifndef MICROPY_INCLUDED_GRAPHICS_H
#define MICROPY_INCLUDED_GRAPHICS_H

#include <stdint.h>
#include "py/mphal.h"
#include "py/obj.h"

typedef struct _mp_graphics_sprite_obj_t {
    mp_obj_base_t base;
    uint8_t width;
    uint8_t height;
    uint8_t stride;
    uint8_t offsetX;
    uint8_t offsetY;
    uint8_t* raw;
    uint8_t* buffer;
} mp_graphics_sprite_obj_t;

extern const mp_obj_type_t mp_graphics_sprite_type;

#endif // MICROPY_INCLUDED_EXTMOD_MODMACHINE_H
