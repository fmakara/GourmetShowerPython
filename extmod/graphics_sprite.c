

// #include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "graphics.h"
#include "py/obj.h"
#include "py/objstr.h"

// General configs ======================================================================================
static void mp_graphics_sprite_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_graphics_sprite_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Graphics.sprite(w=%d, h=%d, s=%d)",
        self->width, self->height, self->stride);
}

static void mp_graphics_sprite_init_helper(mp_obj_base_t* self_obj, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_width, ARG_height, ARG_stride, ARG_raw, ARG_buffer};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_width, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_height, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_stride, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_raw, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_buffer, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t *)self_obj;
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    self->width = args[ARG_width].u_int;
    self->height = args[ARG_height].u_int;
    self->stride = args[ARG_stride].u_int;
    self->offsetX = 0;
    self->offsetY = 0;
    self->raw = NULL;
    self->buffer = NULL;
    self->buffer_is_internal = 0;
    
    if(args[ARG_raw].u_obj != MP_OBJ_NULL){
        mp_buffer_info_t raw_buffer_info;
        mp_get_buffer_raise(args[ARG_raw].u_obj, &raw_buffer_info, MP_BUFFER_READ);
        if(raw_buffer_info.len<4) mp_raise_ValueError(MP_ERROR_TEXT("raw buffer too small"));
        uint8_t *tb = (uint8_t*)raw_buffer_info.buf;
        uint8_t tw = tb[0];
        uint8_t th = tb[1];
        uint8_t ts = tb[2];
        if(tw==0 || th==0 || tw>200 || th>200 || ts<(th/8) || (ts*(tw-1)+(th+7)/8)>raw_buffer_info.len){
            mp_raise_ValueError(MP_ERROR_TEXT("Invalid object metadata"));
        }
        self->raw = tb;
        self->buffer = self->raw+3;
        self->width = tw;
        self->height = th;
        self->stride = ts;
    }

    if(args[ARG_buffer].u_obj != MP_OBJ_NULL){
        mp_buffer_info_t raw_buffer_info;
        mp_get_buffer_raise(args[ARG_buffer].u_obj, &raw_buffer_info, MP_BUFFER_READ);
        if((self->stride*(self->width-1)+(self->height+7)/8)>raw_buffer_info.len){
            mp_raise_ValueError(MP_ERROR_TEXT("Invalid Buffer size"));
        }
        self->buffer = raw_buffer_info.buf;
    }

    if(self->width<0 || self->width>200 || self->height<0 || self->height>200 || self->stride<(self->height/8)){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid sprite size"));
    }
    if(self->width>0 && self->buffer==NULL){
        // allocating internally
        self->buffer = malloc(self->width&self->stride);
        self->buffer_is_internal = 1;
    }
}

static mp_obj_t mp_graphics_sprite_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    mp_obj_base_t *self_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[0]);
    mp_graphics_sprite_init_helper(self_obj, n_args-1, args+1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(graphics_sprite_init_obj, 1, mp_graphics_sprite_init);

static mp_obj_t mp_graphics_sprite_deinit(mp_obj_t self_obj) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t *)MP_OBJ_TO_PTR(self_obj);
    if(self->buffer_is_internal && self->buffer!=NULL){
        free(self->buffer);
        self->buffer = NULL;
        self->buffer_is_internal = 0;
    }
    self->raw = NULL;
    self->buffer = NULL;
    self->width = 0;
    self->height = 0;
    self->stride = 0;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(graphics_sprite_deinit_obj, mp_graphics_sprite_deinit);

static mp_obj_t mp_graphics_sprite_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // create new Graphicssprite object
    mp_graphics_sprite_obj_t *self = mp_obj_malloc(mp_graphics_sprite_obj_t, &mp_graphics_sprite_type);
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_graphics_sprite_init_helper(&self->base, n_args, args, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

// Getters ===================================================================================
static mp_obj_t graphics_sprite_get_width(mp_obj_t self_obj) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(self_obj);
    return MP_OBJ_NEW_SMALL_INT(self->width);
}
static MP_DEFINE_CONST_FUN_OBJ_1(graphics_sprite_get_width_obj, graphics_sprite_get_width);

static mp_obj_t graphics_sprite_get_height(mp_obj_t self_obj) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(self_obj);
    return MP_OBJ_NEW_SMALL_INT(self->height);
}
static MP_DEFINE_CONST_FUN_OBJ_1(graphics_sprite_get_height_obj, graphics_sprite_get_height);

static mp_obj_t graphics_sprite_get_stride(mp_obj_t self_obj) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(self_obj);
    return MP_OBJ_NEW_SMALL_INT(self->stride);
}
static MP_DEFINE_CONST_FUN_OBJ_1(graphics_sprite_get_stride_obj, graphics_sprite_get_stride);

static mp_obj_t graphics_sprite_get_buffer(mp_obj_t self_obj) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(self_obj);
    vstr_t vstr;
    vstr_init_len(&vstr, self->stride*self->width);
    memcpy(vstr.buf, self->buffer, self->stride*self->width);
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_1(graphics_sprite_get_buffer_obj, graphics_sprite_get_buffer);


// Main methods =====================================================================
static mp_obj_t graphics_sprite_clear(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->raw!=NULL) return mp_const_none;
    int color = 0;
    if(n_args==2){
        if(args[1]==mp_const_true) color = 1;
        else if(args[1]==mp_const_false) color = 0;
        else color = 2;
    }
    for(int x=0; x<self->width; x++){
        int position = -1;
        for(int y=0; y<self->height; y+=8){
            position = (x+self->offsetX)*self->stride+((y+self->offsetY)>>3);
            if(color==0) self->buffer[position] = 0;
            else if(color==1) self->buffer[position] = 0xFF;
            else self->buffer[position] ^= 0xFF;
        }
        int lastPos = (x+self->offsetX)*self->stride+((self->height-1+self->offsetY)>>3);
        if(position!=lastPos){
            if(color==0) self->buffer[lastPos] = 0;
            else if(color==1) self->buffer[lastPos] = 0xFF;
            else self->buffer[lastPos] ^= 0xFF; // if lastpos==position, this step is applied twice
        }
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_clear_obj, 1, 2, graphics_sprite_clear);

static inline void setPixel(mp_graphics_sprite_obj_t* self, int x, int y, mp_obj_t* color){
    if(x<0 || y<0 || x>=self->width || y>=self->height) return;
    int ny = y+self->offsetY;
    uint8_t bit = 1<<(ny&7);
    uint8_t *col = self->buffer + self->stride*(self->offsetX+x) + (ny>>3);
    if(color==mp_const_true) *col |= bit;
    else if (color==mp_const_false) *col &= ~bit;
    else *col ^= bit;
}
static mp_obj_t graphics_sprite_set_pixel(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->width==0) return mp_const_none;
    setPixel(self, mp_obj_get_int(args[1]), mp_obj_get_int(args[2]), args[3]);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_set_pixel_obj, 4, 4, graphics_sprite_set_pixel);

static mp_obj_t graphics_sprite_get_pixel(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->width==0) return mp_const_false;
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    if(x<0 || y<0 || x>=self->width || y>=self->height) return mp_const_none;
    int ny = y+self->offsetY;
    uint8_t bit = 1<<(ny&7);
    uint8_t *col = self->buffer + self->stride*(self->offsetX+x) + (ny>>3);
    if(*col & bit) return mp_const_true;
    return mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_get_pixel_obj, 3, 3, graphics_sprite_get_pixel);

static mp_obj_t graphics_sprite_vert_line(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->width==0) return mp_const_false;
    int x = mp_obj_get_int(args[1]);
    int y0 = mp_obj_get_int(args[2]);
    int y1 = mp_obj_get_int(args[3]);
    if(y0>y1){
        int b = y0;
        y0 = y1;
        y1 = b;
    }
    if(x<0 || x>=self->width || y0>=self->height || y1<0) return mp_const_none;
    if(y0<0) y0=0;
    if(y1>=self->height) y1=self->height-1;
    uint64_t bits = ((1ULL<<(y1+self->offsetY+1))-1) & ~((1ULL<<(y0+self->offsetY))-1);
    uint64_t col;
    uint8_t *ptr = self->buffer + self->stride*(x+self->offsetX);
    memcpy(&col, ptr, self->stride<8 ? self->stride : 8);
    if(args[4]==mp_const_true) col |= bits;
    else if (args[4]==mp_const_false) col &= ~bits;
    else col ^= bits;
    memcpy(ptr, &col, self->stride<8 ? self->stride : 8);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_vert_line_obj, 5, 5, graphics_sprite_vert_line);

static mp_obj_t graphics_sprite_horz_line(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->width==0) return mp_const_false;
    int x0 = mp_obj_get_int(args[1]);
    int x1 = mp_obj_get_int(args[2]);
    int y = mp_obj_get_int(args[3]);
    if(x0>x1){
        int b = x0;
        x0 = x1;
        x1 = b;
    }
    if(y<0 || y>=self->height || x0>=self->width || x1<0) return mp_const_none;
    if(x0<0) x0=0;
    if(x1>=self->width) x1=self->width-1;
    int ny = y+self->offsetY;
    uint8_t bit = 1<<(ny&7);
    uint8_t *col = self->buffer + self->stride*(self->offsetX+x0) + (ny>>3);
    for(int i=x0; i<=x1; i++, col+=self->stride){
        if(args[3]==mp_const_true) *col |= bit;
        else if (args[3]==mp_const_false) *col &= ~bit;
        else *col ^= bit;
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_horz_line_obj, 5, 5, graphics_sprite_horz_line);

static mp_obj_t graphics_sprite_rect(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->width==0) return mp_const_false;
    int x0 = mp_obj_get_int(args[1]);
    int y0 = mp_obj_get_int(args[2]);
    int x1 = mp_obj_get_int(args[3]);
    int y1 = mp_obj_get_int(args[4]);
    if(x0>x1){
        int b = x0;
        x0 = x1;
        x1 = b;
    }
    if(y0>y1){
        int b = y0;
        y0 = y1;
        y1 = b;
    }
    if(x0>=self->width || x1<0 || y0>=self->height || y1<0) return mp_const_none;
    if(x0<0) x0=0;
    if(x1>=self->width) x1=self->width-1;
    if(y0<0) y0=0;
    if(y1>=self->height) y1=self->height-1;
    uint64_t bits = ((1ULL<<(y1+self->offsetY+1))-1) & ~((1ULL<<(y0+self->offsetY))-1);
    uint64_t col;
    uint8_t *ptr = self->buffer + self->stride*(x0+self->offsetX);
    for(int i=x0; i<=x1; i++, ptr+=self->stride){
        memcpy(&col, ptr, self->stride<8 ? self->stride : 8);
        if(args[5]==mp_const_true) col |= bits;
        else if (args[5]==mp_const_false) col &= ~bits;
        else col ^= bits;
        memcpy(ptr, &col, self->stride<8 ? self->stride : 8);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_rect_obj, 6, 6, graphics_sprite_rect);

// https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm#:~:text=All%20cases
// static inline int abs(int a){ return a>0?a:-a; }
static void plotLineLow(mp_graphics_sprite_obj_t* self, int x0, int y0, int x1, int y1, mp_obj_t* color){
    int dx = x1 - x0;
    int dy = y1 - y0;
    int yi = 1;
    if(dy < 0){
        yi = -1;
        dy = -dy;
    }
    int D = (2 * dy) - dx;
    int y = y0;

    for(int x=x0; x<x1; x++){
        setPixel(self, x, y, color);
        if(D > 0){
            y = y + yi;
            D = D + (2 * (dy - dx));
        } else {
            D = D + 2*dy;
        }
    }
}

static void plotLineHigh(mp_graphics_sprite_obj_t* self, int x0, int y0, int x1, int y1, mp_obj_t* color){
    int dx = x1 - x0;
    int dy = y1 - y0;
    int xi = 1;
    if(dx < 0){
        xi = -1;
        dx = -dx;
    }
    int D = (2 * dx) - dy;
    int x = x0;

    for(int y=y0; y<y1; y++){
        setPixel(self, x, y, color);
        if(D > 0){
            x = x + xi;
            D = D + (2 * (dx - dy));
        } else {
            D = D + 2*dx;
        }
    }
}

static mp_obj_t graphics_sprite_line(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->width==0) return mp_const_false;
    int x0 = mp_obj_get_int(args[1]);
    int y0 = mp_obj_get_int(args[2]);
    int x1 = mp_obj_get_int(args[3]);
    int y1 = mp_obj_get_int(args[4]);
    if(abs(y1 - y0) < abs(x1 - x0)){
        if(x0 > x1){
            plotLineLow(self, x1, y1, x0, y0, args[5]);
        } else {
            plotLineLow(self, x0, y0, x1, y1, args[5]);
        }
    } else {
        if(y0 > y1){
            plotLineHigh(self, x1, y1, x0, y0, args[5]);
        } else {
            plotLineHigh(self, x0, y0, x1, y1, args[5]);
        }
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_line_obj, 6, 6, graphics_sprite_line);

void graphics_sprite_copy_from_helper(
    uint8_t x, uint8_t y,
    uint8_t destWidth, uint8_t destHeight, uint8_t destOffX, uint8_t destOffY, uint8_t destStride, uint8_t* destBuffer,
    uint8_t srcWidth, uint8_t srcHeight, uint8_t srcOffX, uint8_t srcOffY, uint8_t srcStride, uint8_t* srcBuffer
){
    if(x>destWidth || y>destHeight || (-x)>srcWidth || (-y)>srcHeight) return;

    int dy0 = (y+destOffY+srcHeight);
    int dy1 = (y+destOffY);
    if(dy0<0) dy0 = 0;
    if(dy0>=64) dy0 = 63;
    if(dy1<0) dy1 = 0;
    if(dy1>=64) dy1 = 63;
    uint64_t destMask = ~(((1ULL<<dy0)-1) & ~((1ULL<<dy1)-1));
    uint64_t srcMask = ((1ULL<<(srcHeight))-1);
    for(int i=0; i<srcWidth; i++){
        int dx = i + destOffX + x;
        if(dx>=0 && dx<destWidth){
            uint64_t colSrc, colDest;
            uint8_t *ptrDest = destBuffer + destStride*dx;
            memcpy(&colSrc, srcBuffer + srcStride*(i+srcOffX), srcStride>8?8:srcStride);
            memcpy(&colDest, ptrDest, destStride>8?8:destStride);
            int shifty = y+destOffY;
            if(shifty>0) colSrc = ((colSrc>>srcOffY)&srcMask)<<shifty;
            else colSrc = ((colSrc>>srcOffY)&srcMask)>>(-shifty);
            colDest = (colDest&destMask) | colSrc;
            memcpy(ptrDest, &colDest, destStride>8?8:destStride);
        }
    }
}

static mp_obj_t graphics_sprite_copy_from(size_t n_args, const mp_obj_t *args) {
    mp_graphics_sprite_obj_t *self = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->width==0) return mp_const_false;
    mp_obj_t *src_obj = args[1];
    int x = mp_obj_get_int(args[2]);
    int y = mp_obj_get_int(args[3]);

    if(x>self->width || y>self->height) return mp_const_none;

    uint8_t tw = 0, th, tx, ty, ts, *tbuf; // width, height, offsetx, offsety, stride and buffer
    if(mp_obj_is_type(src_obj, &mp_graphics_sprite_type)){
        mp_graphics_sprite_obj_t *src = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(src_obj);
        if(src->width==0 || x>self->width || y>self->height || (-x)>src->width || (-y)>src->height) return mp_const_none;
        tw = src->width;
        th = src->height;
        tx = src->offsetX;
        ty = src->offsetY;
        ts = src->stride;
        tbuf = src->buffer;
        if(tw==0) return mp_const_none;
    } else if(mp_obj_is_str_or_bytes(src_obj)){
        mp_buffer_info_t raw_buffer_info;
        mp_get_buffer_raise(src_obj, &raw_buffer_info, MP_BUFFER_READ);
        if(raw_buffer_info.len<4) mp_raise_ValueError(MP_ERROR_TEXT("raw buffer too small"));
        uint8_t *tb = (uint8_t*)raw_buffer_info.buf;
        tw = tb[0]; // width
        th = tb[1]; // height
        ts = tb[2]; // stride
        tbuf = tb+3; // buffer
        tx = 0;
        ty = 0;
        if(tw==0 || th==0 || tw>200 || th>200 || ts<(th/8) || ts>127 || (ts*(tw-1)+(th+7)/8)>raw_buffer_info.len){
            mp_raise_ValueError(MP_ERROR_TEXT("Invalid object metadata"));
        }
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid source object"));
    }

    if(tw==0 || (-x)>tw || (-y)>th) return mp_const_none;
    graphics_sprite_copy_from_helper(x, y,
        self->width, self->height, self->offsetX, self->offsetY, self->stride, self->buffer,
        tw, th, tx, ty, ts, tbuf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_sprite_copy_from_obj, 4, 4, graphics_sprite_copy_from);



static const mp_rom_map_elem_t graphics_sprite_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&graphics_sprite_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&graphics_sprite_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&graphics_sprite_deinit_obj) },
    // Getters
    { MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&graphics_sprite_get_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&graphics_sprite_get_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_stride), MP_ROM_PTR(&graphics_sprite_get_stride_obj) },
    { MP_ROM_QSTR(MP_QSTR_buffer), MP_ROM_PTR(&graphics_sprite_get_buffer_obj) },
    // Main methods
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&graphics_sprite_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_setPixel), MP_ROM_PTR(&graphics_sprite_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_getPixel), MP_ROM_PTR(&graphics_sprite_get_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_vertLine), MP_ROM_PTR(&graphics_sprite_vert_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_horzLine), MP_ROM_PTR(&graphics_sprite_horz_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&graphics_sprite_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&graphics_sprite_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_copyFrom), MP_ROM_PTR(&graphics_sprite_copy_from_obj) },
};
MP_DEFINE_CONST_DICT(mp_graphics_sprite_locals_dict, graphics_sprite_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_graphics_sprite_type,
    MP_QSTR_Sprite,
    MP_TYPE_FLAG_NONE,
    make_new, mp_graphics_sprite_make_new,
    print, mp_graphics_sprite_print,
    locals_dict, &mp_graphics_sprite_locals_dict
    );
