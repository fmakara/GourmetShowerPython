

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "graphics.h"
#include "py/obj.h"
#include "py/objstr.h"

// General configs ======================================================================================
static void mp_graphics_typer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_graphics_typer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Graphics.Typer(ascii=%u, utf8=%u, %ux%u)", 
        self->ascii_count, self->utf8_count, self->first_width, self->first_height);
}

uint8_t utf8_decode(uint8_t *ptr, uint32_t *utf8){
    if( (ptr[0]&0x80)==0 ){
        if(utf8!=NULL){
            *utf8 = ptr[0];
        }
        return 1;
    }
    if( (ptr[0]&0xE0)==0xC0) {
        if(utf8!=NULL){
            *utf8 = (ptr[0]<<8)|ptr[1];
        }
        return 2;
    }
    if( (ptr[0]&0xF0)==0xE0) {
        if(utf8!=NULL){
            *utf8 = (ptr[0]<<16)|(ptr[1]<<8)|ptr[2];
        }
        return 3;
    }
    if( (ptr[0]&0xF8)==0xF0) {
        if(utf8!=NULL){
            *utf8 = (ptr[0]<<24)|(ptr[1]<<16)|(ptr[2]<<8)|ptr[3];
        }
        return 4;
    }
    return 0;
}

uint32_t mp_graphics_typer_checkbuffer_helper(mp_obj_t buffer, mp_buffer_info_t* rbi, uint32_t *utf8Cnt, char* ebuf, uint8_t ebuflen){
    uint32_t objCnt = 0;
    mp_buffer_info_t innerRbi;
    if(rbi==NULL)rbi = &innerRbi;
    if(utf8Cnt!=NULL){
        *utf8Cnt = 0;
    }
    if(!mp_get_buffer(buffer, rbi, MP_BUFFER_READ)) {
        snprintf(ebuf, ebuflen, "object is not readable");
        return 0;
    }
    if(rbi->len<5) {
        snprintf(ebuf, ebuflen, "buffer is too small");
        return 0;
    }
    uint8_t *ptr;
    uint32_t off=0, tempLen;
    while(off<rbi->len){
        // Format: <UTF8 char(1-4 bytes)><4bit pre, 4bit post offset><width><height><stride><data (width*stride bytes)>
        tempLen = 0;
        ptr = rbi->buf + off;
        tempLen = utf8_decode(ptr, NULL);
        if(tempLen==0) {
            snprintf(ebuf, ebuflen, "invalid utf8 at %lu", off);
            return 0;
        }
        if((off+tempLen+4)>rbi->len){
            snprintf(ebuf, ebuflen, "no len for header at %lu", off);
            return 0;
        }
        uint8_t tw = ptr[tempLen+1];
        uint8_t th = ptr[tempLen+2];
        uint8_t ts = ptr[tempLen+3];
        if(tw==0 || th==0 || tw>128 || th>64 || (ts<((th+7)/8))) {
            snprintf(ebuf, ebuflen, "invalid header at %lu", off);
            return 0;
        }
        tempLen += ts*tw+4;
        if((off+tempLen)>rbi->len) {
            snprintf(ebuf, ebuflen, "no len for data at %lu", off);
            return 0;
        }
        off += tempLen;
        objCnt++;
        if(utf8Cnt!=NULL){
            if(ptr[0]<32 || ptr[0]>127) (*utf8Cnt)++;
        }
    }
    return objCnt;
}

static void mp_graphics_typer_init_helper(mp_obj_base_t* self_obj, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_buffer, ARG_lineHeight, ARG_target };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_buffer, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_lineHeight, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_target, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t *)self_obj;
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    if(args[ARG_lineHeight].u_int<=0 || args[ARG_lineHeight].u_int>200) mp_raise_ValueError(MP_ERROR_TEXT("invalid line height"));
    
    if(args[ARG_target].u_obj!=MP_OBJ_NULL){
        if(!mp_obj_is_type(args[ARG_target].u_obj, &mp_graphics_sprite_type)){
            mp_obj_t obj = mp_obj_cast_to_native_base(args[ARG_target].u_obj, &mp_graphics_sprite_type);
            if(obj!=MP_OBJ_NULL){
                self->target = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(obj);
            } else {
                mp_raise_TypeError(MP_ERROR_TEXT("target must be a sprite"));
            }
        } else {
            self->target = (mp_graphics_sprite_obj_t*) MP_OBJ_TO_PTR(args[ARG_target].u_obj);
        }
    } else {
        self->target = NULL;
    }

    mp_buffer_info_t rbi;
    uint32_t objCount, utf8Cnt;
    char msg[50];
    objCount = mp_graphics_typer_checkbuffer_helper(args[ARG_buffer].u_obj, &rbi, &utf8Cnt, msg, 50);
    if(objCount==0) mp_raise_ValueError(msg);
    
    self->line_height = args[ARG_lineHeight].u_int;
    self->buffer = rbi.buf;
    self->utf8_count = utf8Cnt;
    self->ascii_count = objCount-utf8Cnt;
    self->utf8_table = utf8Cnt==0 ? NULL : (graphics_typer_utf8_offset_t*)malloc(sizeof(graphics_typer_utf8_offset_t)*utf8Cnt);
    for(int i=0; i<96; i++){
        self->ascii_table[i].obj = NULL;
        self->ascii_table[i].pre_off = 0;
        self->ascii_table[i].post_off = 0;
    }
    // Buffer is checked, everything else is initialized, populating the lists...
    uint8_t *ptr;
    uint32_t off=0, tempLen, utf8Count=0, utf8;
    while(off<rbi.len){
        // Format: <UTF8 char(1-4 bytes)><4bit pre, 4bit post offset><width><height><stride><data (width*stride bytes)>
        tempLen = 0;
        ptr = self->buffer + off;
        tempLen = utf8_decode(ptr, &utf8);
        if(utf8>=32 && utf8<=128){
            self->ascii_table[utf8-32].obj = self->buffer+off+2;
            self->ascii_table[utf8-32].pre_off = ptr[1]>>4;
            self->ascii_table[utf8-32].post_off = ptr[1]&0xF;
        } else {
            self->utf8_table[utf8Count].utf8 = utf8;
            self->utf8_table[utf8Count].obj = self->buffer+off+1+tempLen;
            self->utf8_table[utf8Count].pre_off = ptr[1+tempLen]>>4;
            self->utf8_table[utf8Count].post_off = ptr[1+tempLen]&0xF;
            utf8Count++;
        }
        uint8_t tw = ptr[tempLen+1];
        uint8_t th = ptr[tempLen+2];
        uint8_t ts = ptr[tempLen+3];
        if(off==0){
            self->first_width = tw;
            self->first_height = th;
        }
        tempLen += ts*tw+4;
        off += tempLen;
    }
}

// static mp_obj_t mp_graphics_typer_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
//     mp_obj_base_t *self_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[0]);
//     mp_graphics_typer_init_helper(self_obj, n_args-1, args+1, kw_args);
//     return mp_const_none;
// }
// MP_DEFINE_CONST_FUN_OBJ_KW(graphics_typer_init_obj, 1, mp_graphics_typer_init);

static mp_obj_t mp_graphics_typer_deinit(mp_obj_t self_obj) {
    mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t *)MP_OBJ_TO_PTR(self_obj);
    if(self->utf8_table!=NULL){
        free(self->utf8_table); 
        self->utf8_table = NULL;
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(graphics_typer_deinit_obj, mp_graphics_typer_deinit);

static mp_obj_t mp_graphics_typer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // create new GraphicsTyper object
    mp_graphics_typer_obj_t *self = mp_obj_malloc(mp_graphics_typer_obj_t, &mp_graphics_typer_type);
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_graphics_typer_init_helper(&self->base, n_args, args, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

// Globals ===================================================================================
static mp_obj_t graphics_typer_check_buffer(mp_obj_t buffer_obj) {
    char msg[50];
    uint32_t objCount = mp_graphics_typer_checkbuffer_helper(buffer_obj, NULL, NULL, msg, 50);
    if(objCount==0) return mp_obj_new_str_from_cstr(msg);
    return MP_OBJ_NEW_SMALL_INT(objCount);
}
static MP_DEFINE_CONST_FUN_OBJ_1(graphics_typer_check_buffer_obj, graphics_typer_check_buffer);

// Getters ===================================================================================
static mp_obj_t graphics_typer_get_width(mp_obj_t self_obj) {
    mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t*) MP_OBJ_TO_PTR(self_obj);
    return MP_OBJ_NEW_SMALL_INT(self->target->width);
}
static MP_DEFINE_CONST_FUN_OBJ_1(graphics_typer_get_width_obj, graphics_typer_get_width);

static mp_obj_t graphics_typer_get_height(mp_obj_t self_obj) {
    mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t*) MP_OBJ_TO_PTR(self_obj);
    return MP_OBJ_NEW_SMALL_INT(self->target->height);
}
static MP_DEFINE_CONST_FUN_OBJ_1(graphics_typer_get_height_obj, graphics_typer_get_height);

// static mp_obj_t graphics_typer_get_stride(mp_obj_t self_obj) {
//     mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t*) MP_OBJ_TO_PTR(self_obj);
//     return MP_OBJ_NEW_SMALL_INT(self->stride);
// }
// static MP_DEFINE_CONST_FUN_OBJ_1(graphics_typer_get_stride_obj, graphics_typer_get_stride);

// static mp_obj_t graphics_typer_get_buffer(mp_obj_t self_obj) {
//     mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t*) MP_OBJ_TO_PTR(self_obj);
//     vstr_t vstr;
//     vstr_init_len(&vstr, self->stride*self->width);
//     memcpy(vstr.buf, self->buffer, self->stride*self->width);
//     return mp_obj_new_bytes_from_vstr(&vstr);
// }
// static MP_DEFINE_CONST_FUN_OBJ_1(graphics_typer_get_buffer_obj, graphics_typer_get_buffer);


// Main methods =====================================================================

static mp_obj_t graphics_typer_print(size_t n_args, const mp_obj_t *args) {
    mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->target==NULL) mp_raise_TypeError(MP_ERROR_TEXT("typer has no target, use printInto"));
    mp_buffer_info_t rbi;
    mp_get_buffer_raise(args[1], &rbi, MP_BUFFER_READ);
    int x = mp_obj_get_int(args[2]);
    int y = mp_obj_get_int(args[3]);
    int starting_x = x;
    uint32_t off = 0, utf8;
    while(off<rbi.len){
        uint8_t tempOff = utf8_decode(rbi.buf+off, &utf8);
        if(tempOff==0){// error decoding UTF8 char... jumping 1 position, not printing
            off++;
        } else {
            uint8_t* ptr = NULL;
            uint8_t pre=0, post=0;
            if(utf8>=32 && utf8<=128){ // ascii range
                if(self->ascii_table[utf8-32].obj!=NULL){
                    ptr = self->ascii_table[utf8-32].obj;
                    pre = self->ascii_table[utf8-32].pre_off;
                    post = self->ascii_table[utf8-32].post_off;
                }
            } else {
                for(uint16_t i=0; i<self->utf8_count && ptr==NULL; i++){
                    if(utf8==self->utf8_table[i].utf8){
                        ptr = self->utf8_table[i].obj;
                        pre = self->utf8_table[i].pre_off;
                        post = self->utf8_table[i].post_off;
                    }
                }
            }
            if(ptr!=NULL){
                x += pre;
                graphics_sprite_copy_from_helper(
                    x, y,
                    self->target->width, self->target->height,
                    self->target->offsetX, self->target->offsetY, 
                    self->target->stride, self->target->buffer,
                    ptr[0], ptr[1], 0, 0, ptr[2], ptr+3
                );
                x += ptr[0]+post;
            }
            if(utf8=='\n'){
                y+=self->line_height;
                x = starting_x;
            }
            off += tempOff;
        }
    }
    mp_obj_t ret[2] = {MP_OBJ_NEW_SMALL_INT(x), MP_OBJ_NEW_SMALL_INT(y)};
    return mp_obj_new_tuple(2, ret);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(graphics_typer_print_obj, 4, 4, graphics_typer_print);


static mp_obj_t graphics_typer_calculate_size(mp_obj_t self_obj, mp_obj_t str_obj) {
    mp_graphics_typer_obj_t *self = (mp_graphics_typer_obj_t*) MP_OBJ_TO_PTR(self_obj);
    mp_buffer_info_t rbi;
    mp_get_buffer_raise(str_obj, &rbi, MP_BUFFER_READ);
    int x=0, y=0, maxx=0;
    uint32_t off = 0, utf8;
    while(off<rbi.len){
        uint8_t tempOff = utf8_decode(rbi.buf+off, &utf8);
        if(tempOff==0){// error decoding UTF8 char... jumping 1 position, not printing
            off++;
        } else {
            uint8_t* ptr = NULL;
            uint8_t pre=0, post=0;
            if(utf8>=32 && utf8<=128){ // ascii range
                if(self->ascii_table[utf8-32].obj!=NULL){
                    ptr = self->ascii_table[utf8-32].obj;
                    pre = self->ascii_table[utf8-32].pre_off;
                    post = self->ascii_table[utf8-32].post_off;
                }
            } else {
                for(uint16_t i=0; i<self->utf8_count && ptr==NULL; i++){
                    if(utf8==self->utf8_table[i].utf8){
                        ptr = self->utf8_table[i].obj;
                        pre = self->utf8_table[i].pre_off;
                        post = self->utf8_table[i].post_off;
                    }
                }
            }
            if(ptr!=NULL){
                x += pre+ptr[0]+post;
            }
            if(utf8=='\n'){
                y+=self->line_height;
                if(x>maxx) maxx = x;
                x = 0;
            }
            off += tempOff;
        }
    }
    mp_obj_t ret[3] = {MP_OBJ_NEW_SMALL_INT(x), MP_OBJ_NEW_SMALL_INT(y), MP_OBJ_NEW_SMALL_INT(maxx)};
    return mp_obj_new_tuple(3, ret);
}
MP_DEFINE_CONST_FUN_OBJ_2(graphics_typer_calculate_size_obj, graphics_typer_calculate_size);


static const mp_rom_map_elem_t graphics_typer_locals_dict_table[] = {
    // { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&graphics_typer_init_obj) },
    // { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&graphics_typer_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&graphics_typer_deinit_obj) },
    // Globals
    { MP_ROM_QSTR(MP_QSTR_checkBuffer), MP_ROM_PTR(&graphics_typer_check_buffer_obj) },
    // Getters
    { MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&graphics_typer_get_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&graphics_typer_get_height_obj) },
    // { MP_ROM_QSTR(MP_QSTR_stride), MP_ROM_PTR(&graphics_typer_get_stride_obj) },
    // { MP_ROM_QSTR(MP_QSTR_buffer), MP_ROM_PTR(&graphics_typer_get_buffer_obj) },
    // Main methods
    { MP_ROM_QSTR(MP_QSTR_print), MP_ROM_PTR(&graphics_typer_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_calculateSize), MP_ROM_PTR(&graphics_typer_calculate_size_obj) },
};
MP_DEFINE_CONST_DICT(mp_graphics_typer_locals_dict, graphics_typer_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_graphics_typer_type,
    MP_QSTR_Typer,
    MP_TYPE_FLAG_NONE,
    make_new, mp_graphics_typer_make_new,
    print, mp_graphics_typer_print,
    locals_dict, &mp_graphics_typer_locals_dict
    );
