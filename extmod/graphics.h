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
    uint8_t buffer_is_internal;
} mp_graphics_sprite_obj_t;

typedef struct {
    uint8_t pre_off;
    uint8_t post_off;
    uint8_t* obj;
} graphics_typer_ascii_offset_t;

typedef struct {
    uint32_t utf8;
    uint8_t pre_off;
    uint8_t post_off;
    uint8_t* obj;
} graphics_typer_utf8_offset_t;

typedef struct _mp_graphics_typer_obj_t {
    mp_obj_base_t base;
    uint8_t *buffer;
    uint8_t line_height;
    uint8_t ascii_count;
    graphics_typer_ascii_offset_t ascii_table[96]; // ignoring 32 null values from start of table
    uint16_t utf8_count; // if they are defined, they can be put/accessed on utf8_table
    graphics_typer_utf8_offset_t *utf8_table;
    mp_graphics_sprite_obj_t *target;
    uint8_t height;
    uint8_t stride;
} mp_graphics_typer_obj_t;

extern const mp_obj_type_t mp_graphics_typer_type;
extern const mp_obj_type_t mp_graphics_sprite_type;

void graphics_sprite_copy_from_helper(
    uint8_t x, uint8_t y,
    uint8_t destWidth, uint8_t destHeight, uint8_t destOffX, uint8_t destOffY, uint8_t destStride, uint8_t* destBuffer,
    uint8_t srcWidth, uint8_t srcHeight, uint8_t srcOffX, uint8_t srcOffY, uint8_t srcStride, uint8_t* srcBuffer
);

#endif // MICROPY_INCLUDED_EXTMOD_MODMACHINE_H
