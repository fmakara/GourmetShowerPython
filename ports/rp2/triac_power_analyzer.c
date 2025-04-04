

// #include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/mpprint.h"
#include "triac.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/iobank0.h"
#include "hardware/regs/intctrl.h"

#define INVALIDPIN 255


// General configs ======================================================================================

static void mp_triac_power_analyzer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_triac_power_analyzer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Triac.PowerAnalyzer(volt=%d,amp=%d)", self->voltage_pin, self->current_pin);
}

static void mp_triac_power_analyzer_init_helper(mp_obj_base_t* self_obj, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_voltage_pin, ARG_current_pin };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_voltage_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_current_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        // { MP_QSTR_polarity, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_int = 0} }, ARG_polarity, ARG_percent, ARG_watchdogUs, ARG_onTimeUs, ARG_ignoreTimeUs
        // { MP_QSTR_percent, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        // { MP_QSTR_watchdogUs, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 500000} },
        // { MP_QSTR_onTimeUs, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 300} },
        // { MP_QSTR_ignoreTimeUs, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 4000} },
    };

    mp_triac_power_analyzer_obj_t *self = (mp_triac_power_analyzer_obj_t *)self_obj;
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    self->voltage_pin = mp_hal_get_pin_obj(args[ARG_voltage_pin].u_obj);
    self->current_pin = mp_hal_get_pin_obj(args[ARG_current_pin].u_obj);

}

static mp_obj_t mp_triac_power_analyzer_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    mp_obj_base_t *self_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[0]);
    mp_triac_power_analyzer_init_helper(self_obj, n_args-1, args+1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(triac_power_analyzer_init_obj, 1, mp_triac_power_analyzer_init);

static mp_obj_t mp_triac_power_analyzer_close(mp_obj_t self_in) {
    mp_triac_power_analyzer_obj_t *self = (mp_triac_power_analyzer_obj_t*) MP_OBJ_TO_PTR(self_in);
    if(self->voltage_pin==INVALIDPIN) return mp_const_none;
    
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_close_obj, mp_triac_power_analyzer_close);


static mp_obj_t mp_triac_power_analyzer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // create new Graphicscontroller object
    mp_triac_power_analyzer_obj_t *self = mp_obj_malloc(mp_triac_power_analyzer_obj_t, &mp_triac_power_analyzer_type);
    self->voltage_pin = INVALIDPIN;
    self->current_pin = INVALIDPIN;
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_triac_power_analyzer_init_helper(&self->base, n_args, args, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

// Getters/Setters ===================================================================================
// static mp_obj_t triac_power_analyzer_percent(size_t n_args, const mp_obj_t *args) {
//     return MP_OBJ_NEW_SMALL_INT(self->percent);
// }
// MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(triac_power_analyzer_percent_obj, 1, 2, triac_power_analyzer_percent);


// static mp_obj_t triac_power_analyzer_half_period(mp_obj_t self_obj) {
//     mp_triac_power_analyzer_obj_t *self = (mp_triac_power_analyzer_obj_t*) MP_OBJ_TO_PTR(self_obj);
//     return mp_obj_new_int(triac_power_analyzer_read_average_timings(self->sense_pin, NULL));
// }
// MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_half_period_obj,  triac_power_analyzer_half_period);


// static mp_obj_t triac_power_analyzer_get_height(mp_obj_t self_obj) {
//     mp_triac_power_analyzer_obj_t *self = (mp_triac_power_analyzer_obj_t*) MP_OBJ_TO_PTR(self_obj);
//     return MP_OBJ_NEW_SMALL_INT(self->height);
// }
// static MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_get_height_obj, triac_power_analyzer_get_height);

/// mp_obj_is_type

// Main methods =====================================================================


static const mp_rom_map_elem_t triac_power_analyzer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&triac_power_analyzer_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&triac_power_analyzer_close_obj) },
    // Getters / Setters
    // { MP_ROM_QSTR(MP_QSTR_percent), MP_ROM_PTR(&triac_power_analyzer_percent_obj) },
    // { MP_ROM_QSTR(MP_QSTR_halfPeriod), MP_ROM_PTR(&triac_power_analyzer_half_period_obj) },
    // { MP_ROM_QSTR(MP_QSTR_frequency), MP_ROM_PTR(&triac_power_analyzer_frequency_obj) },
    // Main methods
    
};
MP_DEFINE_CONST_DICT(mp_triac_power_analyzer_locals_dict, triac_power_analyzer_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_triac_power_analyzer_type,
    MP_QSTR_PowerAnalyzer,
    MP_TYPE_FLAG_NONE,
    make_new, mp_triac_power_analyzer_make_new,
    print, mp_triac_power_analyzer_print,
    locals_dict, &mp_triac_power_analyzer_locals_dict
    );
