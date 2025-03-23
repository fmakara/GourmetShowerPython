

// #include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "triac.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/iobank0.h"
#include "hardware/regs/intctrl.h"

#define INVALIDPIN 255
#define ALARM_ID_INVALID (-1)

#define TRIAC_TIMING_SIZE (8)
#define TRIAC_MAX_PINS (32)

typedef struct {
    uint8_t active;
    uint32_t sense_pin;
    uint32_t trigger_pins;
    // activation variables
    uint64_t watchdog_limit;
    uint32_t on_time;
    uint32_t time_to_activate;
    alarm_id_t alarm_activate;
    alarm_id_t alarm_deactivate;
    uint8_t polarity;
    // detection statistics
    uint32_t ignore_timing;
    uint8_t timing_index;
    uint64_t last_crosses[2];
    uint32_t last_timings[TRIAC_TIMING_SIZE];
} TriacData;
static volatile TriacData triac_data[TRIAC_MAX_PINS];
static alarm_pool_t *triac_alarm_pool; 

// Interrupt... stuff =================================================================================
static int64_t triac_timer_irq_activate(alarm_id_t id, void *user_data){
    TriacData *data = (TriacData*)user_data;
    data->alarm_activate = ALARM_ID_INVALID;
    gpio_put_masked(data->trigger_pins, data->polarity?0:0xFFFFFFFF);
    return 0;
}

static int64_t triac_timer_irq_deactivate(alarm_id_t id, void *user_data){
    TriacData *data = (TriacData*)user_data;
    data->alarm_activate = ALARM_ID_INVALID;
    gpio_put_masked(data->trigger_pins, data->polarity?0xFFFFFFFF:0);
    return 0;
}

inline static void triac_gpio_irq_handler(uint8_t gpio, uint8_t events){
    uint64_t now = time_us_64();
    volatile TriacData *data = &triac_data[gpio];
    uint64_t delta = now - data->last_crosses[data->timing_index&1];
    if(delta<data->ignore_timing) return; // too soon, probably another zero cross
    if(delta>0x0FFFFFFFULL) delta = 0x0FFFFFFFULL;
    data->timing_index = (data->timing_index+1)%TRIAC_TIMING_SIZE;
    data->last_crosses[data->timing_index&1] = now;
    data->last_timings[data->timing_index] = delta;

    if(data->alarm_activate!=ALARM_ID_INVALID){
        alarm_pool_cancel_alarm(triac_alarm_pool, data->alarm_activate);
        data->alarm_activate = ALARM_ID_INVALID;
    }
    if(data->alarm_deactivate!=ALARM_ID_INVALID){
        alarm_pool_cancel_alarm(triac_alarm_pool, data->alarm_deactivate);
        data->alarm_deactivate = ALARM_ID_INVALID;
    }
    gpio_put_masked(data->trigger_pins, data->polarity?0xFFFFFFFF:0); // just in case...

    // if(data->watchdog_limit<now) return;
    if(data->time_to_activate==0 || data->on_time==0) return;

    absolute_time_t t;
    update_us_since_boot(&t, now+data->time_to_activate);
    data->alarm_activate = alarm_pool_add_alarm_at(triac_alarm_pool, t, triac_timer_irq_activate, (void*)data, true);
    update_us_since_boot(&t, now+data->time_to_activate+data->on_time);
    data->alarm_deactivate = alarm_pool_add_alarm_at(triac_alarm_pool, t, triac_timer_irq_deactivate, (void*)data, true);
    // gpio_put(25, false);
    // if(data->alarm_activate==ALARM_ID_INVALID){
    //     gpio_put(25, true);
    // }
}

static void triac_gpio_irq_listener(void) {
    uint8_t core = get_core_num();
    io_bank0_irq_ctrl_hw_t *irq_ctrl_base = core ? &io_bank0_hw->proc1_irq_ctrl : &io_bank0_hw->proc0_irq_ctrl;
    for (uint8_t gpio = 0; gpio < NUM_BANK0_GPIOS; gpio+=8) {
        uint32_t events8 = irq_ctrl_base->ints[gpio >> 3u];
        for(uint8_t i=gpio;events8 && i<gpio+8;i++) {
            uint32_t events = events8 & 0xfu;
            if (events && triac_data[i].active){
                gpio_acknowledge_irq(i, events);
                triac_gpio_irq_handler(i, events);
            }
            events8 >>= 4;
        }
    }
}

static inline void reset_triac_data(uint8_t pin){
    triac_data[pin].active = 0;
    triac_data[pin].sense_pin = pin;
    triac_data[pin].polarity = 0;
    triac_data[pin].trigger_pins = 0;
    triac_data[pin].watchdog_limit = 0;
    triac_data[pin].on_time = 1000;
    triac_data[pin].time_to_activate = 0;
    triac_data[pin].ignore_timing = 4000;
    triac_data[pin].timing_index = 0;
    triac_data[pin].last_crosses[0] = 0;
    triac_data[pin].last_crosses[1] = 0;
    triac_data[pin].alarm_activate = ALARM_ID_INVALID;
    triac_data[pin].alarm_deactivate = ALARM_ID_INVALID;
    for(uint8_t i=0; i<TRIAC_TIMING_SIZE; i++){
        triac_data[pin].last_timings[i] = 0;
    }
}

void triac_global_init(void) {
    for(uint8_t i=0; i<TRIAC_MAX_PINS; i++){
        reset_triac_data(i);
    }
    triac_alarm_pool = alarm_pool_get_default();
    irq_add_shared_handler(IO_IRQ_BANK0, triac_gpio_irq_listener, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY+10);
    irq_set_enabled(IO_IRQ_BANK0, true);
    #if MICROPY_HW_PIN_EXT_COUNT
    machine_pin_ext_init();
    #endif
}

// General configs ======================================================================================

static void mp_triac_controller_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_triac_controller_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Triac.Controller(sense=%d,trigger=%d)", self->sense_pin, self->trigger_pin);
    for(uint8_t i=0; i<8; i++){
        mp_printf(print, "%lu\n", triac_data[self->sense_pin].last_timings[i]);
    }
}

static void mp_triac_controller_init_helper(mp_obj_base_t* self_obj, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_sense_pin, MP_trigger_pin, ARG_percent};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_sense_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_trigger_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_percent, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    };

    mp_triac_controller_obj_t *self = (mp_triac_controller_obj_t *)self_obj;
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    int sense = mp_hal_get_pin_obj(args[ARG_sense_pin].u_obj);
    int trigger = mp_hal_get_pin_obj(args[MP_trigger_pin].u_obj);
    if(sense==trigger) mp_raise_TypeError(MP_ERROR_TEXT("pins need to be different"));
    
    int percent = args[ARG_percent].u_int;
    if(percent<0) percent = 0;
    if(percent>100) percent = 100;

    if(self->sense_pin!=INVALIDPIN){
        // Disable all interruptions, reset the data
        gpio_set_irq_enabled(self->sense_pin,0xF, false);
        if(triac_data[self->sense_pin].alarm_activate!=ALARM_ID_INVALID){
            alarm_pool_cancel_alarm(triac_alarm_pool, triac_data[self->sense_pin].alarm_activate);
        }
        if(triac_data[self->sense_pin].alarm_deactivate!=ALARM_ID_INVALID){
            alarm_pool_cancel_alarm(triac_alarm_pool, triac_data[self->sense_pin].alarm_deactivate);
        }
        gpio_put_masked(triac_data[self->sense_pin].trigger_pins, triac_data[self->sense_pin].polarity?0xFFFFFFFF:0);
        triac_gpio_irq_handler(self->sense_pin, 0xF);
        reset_triac_data(self->sense_pin);
    }

    self->percent = percent;
    self->sense_pin = sense;
    self->trigger_pin = trigger;

    reset_triac_data(self->sense_pin);
    triac_data[self->sense_pin].trigger_pins = (1<<self->trigger_pin);
    triac_data[self->sense_pin].active = 1;

    gpio_init(self->sense_pin);
    gpio_set_dir(self->sense_pin, false);
    gpio_set_pulls(self->sense_pin, false, true);
    gpio_set_irq_enabled(self->sense_pin, GPIO_IRQ_EDGE_FALL|GPIO_IRQ_EDGE_RISE, true);

    gpio_init(self->trigger_pin);
    gpio_put(self->trigger_pin, false);
    gpio_set_dir(self->trigger_pin, true);

    gpio_init(25);
    gpio_put(25, false);
    gpio_set_dir(25, true);
}

static mp_obj_t mp_triac_controller_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    mp_obj_base_t *self_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[0]);
    mp_triac_controller_init_helper(self_obj, n_args-1, args+1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(triac_controller_init_obj, 1, mp_triac_controller_init);

static mp_obj_t mp_triac_controller_close(mp_obj_t self_in) {
    mp_triac_controller_obj_t *self = (mp_triac_controller_obj_t*) MP_OBJ_TO_PTR(self_in);
    if(self->sense_pin==INVALIDPIN) return mp_const_none;
    
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_controller_close_obj, mp_triac_controller_close);


static mp_obj_t mp_triac_controller_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // create new Graphicscontroller object
    mp_triac_controller_obj_t *self = mp_obj_malloc(mp_triac_controller_obj_t, &mp_triac_controller_type);
    self->sense_pin = INVALIDPIN;
    self->trigger_pin = INVALIDPIN;
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_triac_controller_init_helper(&self->base, n_args, args, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

// Getters/Setters ===================================================================================
static mp_obj_t triac_controller_percent(size_t n_args, const mp_obj_t *args) {
    mp_triac_controller_obj_t *self = (mp_triac_controller_obj_t*) MP_OBJ_TO_PTR(args[0]);
    if(self->sense_pin==INVALIDPIN) mp_raise_TypeError(MP_ERROR_TEXT("object closed. re-init first!"));
    if(n_args==2){
        mp_int_t percent = 0;
        if(!mp_obj_get_int_maybe(args[1], &percent)){
            mp_raise_TypeError(MP_ERROR_TEXT("percent needs to be integer"));
        }
        if(percent<0) percent = 0;
        if(percent>100) percent = 100;
        self->percent = percent;
        triac_data[self->sense_pin].time_to_activate = percent*100;
        triac_data[self->sense_pin].on_time = 1000;
        triac_data[self->sense_pin].watchdog_limit = time_us_64()+(10*1000*1000);
    }
    return MP_OBJ_NEW_SMALL_INT(self->percent);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(triac_controller_percent_obj, 1, 2, triac_controller_percent);

// static mp_obj_t triac_controller_get_height(mp_obj_t self_obj) {
//     mp_triac_controller_obj_t *self = (mp_triac_controller_obj_t*) MP_OBJ_TO_PTR(self_obj);
//     return MP_OBJ_NEW_SMALL_INT(self->height);
// }
// static MP_DEFINE_CONST_FUN_OBJ_1(triac_controller_get_height_obj, triac_controller_get_height);

/// mp_obj_is_type

// Main methods =====================================================================


static const mp_rom_map_elem_t triac_controller_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&triac_controller_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&triac_controller_close_obj) },
    // Getters / Setters
    { MP_ROM_QSTR(MP_QSTR_percent), MP_ROM_PTR(&triac_controller_percent_obj) },
    // Main methods
    
};
MP_DEFINE_CONST_DICT(mp_triac_controller_locals_dict, triac_controller_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_triac_controller_type,
    MP_QSTR_Controller,
    MP_TYPE_FLAG_NONE,
    make_new, mp_triac_controller_make_new,
    print, mp_triac_controller_print,
    locals_dict, &mp_triac_controller_locals_dict
    );
