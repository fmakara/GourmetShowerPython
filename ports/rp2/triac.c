#include "py/builtin.h"
#include "py/runtime.h"
#include "triac.h"


static const mp_rom_map_elem_t triac_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_Triac) },

    { MP_ROM_QSTR(MP_QSTR_Controller), MP_ROM_PTR(&mp_triac_controller_type) },
};
static MP_DEFINE_CONST_DICT(triac_module_globals, triac_module_globals_table);

const mp_obj_module_t mp_module_triac = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&triac_module_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_Triac, mp_module_triac);
