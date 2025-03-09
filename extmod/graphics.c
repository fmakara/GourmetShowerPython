#include "py/builtin.h"
#include "py/runtime.h"
#include "extmod/graphics.h"


static const mp_rom_map_elem_t graphics_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_Graphics) },

    { MP_ROM_QSTR(MP_QSTR_Sprite), MP_ROM_PTR(&mp_graphics_sprite_type) },
};
static MP_DEFINE_CONST_DICT(graphics_module_globals, graphics_module_globals_table);

const mp_obj_module_t mp_module_graphics = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&graphics_module_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_Graphics, mp_module_graphics);
