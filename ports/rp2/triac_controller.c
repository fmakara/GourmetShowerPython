

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
#define ALARM_ID_INVALID (-1)

#define TRIAC_TIMING_SIZE (32)
#define TRIAC_MAX_PINS (32)
#define TRIAC_MAX_DELTA (0x0FFFFFFFULL)

static const uint16_t TRIAC_POWERLINE[101] = {65534, 57933, 55906, 54461, 53296,
    52299, 51418, 50620, 49888, 49207, 48567, 47963, 47389, 46840, 46313, 45806,
    45316, 44841, 44380, 43932, 43494, 43067, 42649, 42239, 41837, 41442, 41054,
    40671, 40294, 39922, 39555, 39192, 38833, 38478, 38126, 37777, 37431, 37088,
    36747, 36408, 36071, 35736, 35402, 35070, 34739, 34409, 34079, 33751, 33423,
    33095, 32767, 32439, 32111, 31783, 31455, 31125, 30795, 30464, 30132, 29798,
    29463, 29126, 28787, 28446, 28103, 27757, 27408, 27056, 26701, 26342, 25979,
    25612, 25240, 24863, 24480, 24092, 23697, 23295, 22885, 22467, 22040, 21602,
    21154, 20693, 20218, 19728, 19221, 18694, 18145, 17571, 16967, 16327, 15646,
    14914, 14116, 13235, 12238, 11073, 9628, 7601, 0};

typedef struct {
    volatile uint8_t active;
    volatile uint32_t sense_pin;
    volatile uint32_t trigger_pins;
    volatile uint8_t polarity;
    // activation variables, interruption-side
    volatile uint64_t interrupt_watchdog_limit;
    volatile uint32_t interrupt_on_time;
    volatile uint32_t interrupt_time_to_activate[2];
    volatile alarm_id_t alarm_activate;
    volatile alarm_id_t alarm_deactivate;
    // activation variables, user-side
    volatile uint8_t user_beeing_written;
    volatile uint64_t user_watchdog_limit;
    volatile uint32_t user_on_time;
    volatile uint32_t user_time_to_activate[2];
    // detection statistics
    volatile uint32_t ignore_timing;
    volatile uint8_t timing_index;
    volatile uint64_t last_crosses[2];
    volatile uint32_t last_timings[TRIAC_TIMING_SIZE];
    volatile uint32_t max_dt;
} TriacData;
static volatile TriacData triac_data[TRIAC_MAX_PINS];
static alarm_pool_t *triac_alarm_pool; 

// Interrupt... stuff =================================================================================

static int64_t triac_timer_irq_deactivate(alarm_id_t id, void *user_data){
    TriacData *data = (TriacData*)user_data;
    data->alarm_deactivate = ALARM_ID_INVALID;
    gpio_put_masked(data->trigger_pins, data->polarity?0:0xFFFFFFFF); //0:0xFFFFFFFF
    return 0;
}

static int64_t triac_timer_irq_activate(alarm_id_t id, void *user_data){
    TriacData *data = (TriacData*)user_data;
    data->alarm_activate = ALARM_ID_INVALID;
    gpio_put_masked(data->trigger_pins, data->polarity?0xFFFFFFFF:0); // 0xFFFFFFFF:0
    
    absolute_time_t t;
    update_us_since_boot(&t, time_us_64()+data->interrupt_on_time);
    data->alarm_deactivate = alarm_pool_add_alarm_at(triac_alarm_pool, t, triac_timer_irq_deactivate, (void*)data, true);
    return 0;
}


inline static void triac_gpio_irq_handler(uint8_t gpio, uint8_t events){
    uint64_t now = time_us_64();
    volatile TriacData *data = &triac_data[gpio];
    uint64_t delta = now - data->last_crosses[data->timing_index&1];
    if(delta<data->ignore_timing) return; // too soon, probably another zero cross
    if(delta>data->max_dt) delta = data->max_dt;
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
    gpio_put_masked(data->trigger_pins, data->polarity?0:0xFFFFFFFF); // just in case...

    if(!data->user_beeing_written){
        data->interrupt_watchdog_limit = data->user_watchdog_limit;
        data->interrupt_on_time = data->user_on_time;
        data->interrupt_time_to_activate[0] = data->user_time_to_activate[0];
        data->interrupt_time_to_activate[1] = data->user_time_to_activate[1];
    }

    if(data->interrupt_watchdog_limit<now) return;
    if(data->interrupt_time_to_activate[0]==0 || data->interrupt_time_to_activate[1]==0 || data->interrupt_on_time==0) return;

    absolute_time_t t;
    update_us_since_boot(&t, now+data->interrupt_time_to_activate[data->timing_index&1]);
    data->alarm_activate = alarm_pool_add_alarm_at(triac_alarm_pool, t, triac_timer_irq_activate, (void*)data, true);
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
    triac_data[pin].interrupt_watchdog_limit = 0;
    triac_data[pin].interrupt_on_time = 1000;
    triac_data[pin].interrupt_time_to_activate[0] = 0;
    triac_data[pin].interrupt_time_to_activate[1] = 0;
    triac_data[pin].user_beeing_written = 0;
    triac_data[pin].user_watchdog_limit = 0;
    triac_data[pin].user_on_time = 1000;
    triac_data[pin].user_time_to_activate[0] = 0;
    triac_data[pin].user_time_to_activate[1] = 0;
    triac_data[pin].ignore_timing = 4000;
    triac_data[pin].timing_index = 0;
    triac_data[pin].last_crosses[0] = 0;
    triac_data[pin].last_crosses[1] = 0;
    triac_data[pin].max_dt = 100*1000;
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

static uint32_t triac_controller_read_average_timings(uint8_t trigger, uint32_t *phases){
    if(!triac_data[trigger].active) return 0;
    uint8_t initial_timing_index, final_timing_index;
    uint64_t last_crosses[2];
    uint32_t last_timings[TRIAC_TIMING_SIZE];
    do{
        initial_timing_index = triac_data[trigger].timing_index;
        last_crosses[0] = triac_data[trigger].last_crosses[0];
        last_crosses[1] = triac_data[trigger].last_crosses[1];
        for(uint i=0; i<TRIAC_TIMING_SIZE; i++) last_timings[i] = triac_data[trigger].last_timings[1];
        final_timing_index = triac_data[trigger].timing_index;
    }while(initial_timing_index!=final_timing_index);
    uint64_t now = time_us_64();
    if(last_crosses[0]+triac_data[trigger].max_dt<now || last_crosses[1]+triac_data[trigger].max_dt<now) return 0;
    uint64_t sum0 = 0, sum1 = 0;
    for(uint i=0; i<TRIAC_TIMING_SIZE; i++){
        if(last_timings[i]==0 || last_timings[i]>=triac_data[trigger].max_dt) return 0;
        if(i&1) sum1 += last_timings[i];
        else sum0 += last_timings[i];
    }
    if(phases!=NULL){
        phases[0] = sum0/(TRIAC_TIMING_SIZE/2);
        phases[1] = sum1/(TRIAC_TIMING_SIZE/2);
    }
    return (sum0+sum1)/TRIAC_TIMING_SIZE;
}


// General configs ======================================================================================

static void mp_triac_controller_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_triac_controller_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Triac.Controller(sense=%d,trigger=%d)", self->sense_pin, triac_data[self->sense_pin].trigger_pins);
}

static void mp_triac_controller_init_helper(mp_obj_base_t* self_obj, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_sense_pin, ARG_trigger_pins, ARG_polarity, ARG_percent, ARG_watchdogUs, ARG_onTimeUs, ARG_ignoreTimeUs};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_sense_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_trigger_pins, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_polarity, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_int = 0} },
        { MP_QSTR_percent, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_watchdogUs, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 500000} },
        { MP_QSTR_onTimeUs, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 300} },
        { MP_QSTR_ignoreTimeUs, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 4000} },
    };

    mp_triac_controller_obj_t *self = (mp_triac_controller_obj_t *)self_obj;
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    int sense = mp_hal_get_pin_obj(args[ARG_sense_pin].u_obj);
    // int trigger = mp_hal_get_pin_obj(args[ARG_trigger_pin].u_obj);
    
    int percent = args[ARG_percent].u_int;
    if(percent<0) percent = 0;
    if(percent>100) percent = 100;
    int watchdogUs = args[ARG_watchdogUs].u_int;
    int onTimeUs = args[ARG_onTimeUs].u_int;
    int ignoreTimeUs = args[ARG_ignoreTimeUs].u_int;
    if(watchdogUs<0) mp_raise_ValueError(MP_ERROR_TEXT("invalid watchdog time limit"));
    if(onTimeUs<0 || onTimeUs>50000) mp_raise_ValueError(MP_ERROR_TEXT("invalid on-time"));
    if(ignoreTimeUs<0 || ignoreTimeUs>50000) mp_raise_ValueError(MP_ERROR_TEXT("invalid double-cross ignore time"));

    uint32_t trigger_pins = 0;
    if(mp_obj_is_type(args[ARG_trigger_pins].u_obj, &mp_type_list)){
        mp_obj_list_t *list = MP_OBJ_TO_PTR(args[ARG_trigger_pins].u_obj);
        for(uint i=0; i<list->len; i++){
            int pin = mp_hal_get_pin_obj(list->items[i]);
            if(sense==pin) mp_raise_TypeError(MP_ERROR_TEXT("sense and trigger pins need to be different"));
            trigger_pins |= 1UL<<pin;
        }
    } else if(mp_obj_is_type(args[ARG_trigger_pins].u_obj, &mp_type_tuple)){
        // mp_printf(&mp_sys_stdout_print, "tuple\n");
        mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(args[ARG_trigger_pins].u_obj);
        for(uint i=0; i<tuple->len; i++){
            int pin = mp_hal_get_pin_obj(tuple->items[i]);
            if(sense==pin) mp_raise_TypeError(MP_ERROR_TEXT("sense and trigger pins need to be different"));
            trigger_pins |= 1UL<<pin;
        }
    } else {
        int trigger = mp_hal_get_pin_obj(args[ARG_trigger_pins].u_obj);
        if(sense==trigger) mp_raise_TypeError(MP_ERROR_TEXT("sense and trigger pins need to be different"));
        trigger_pins = 1UL<<trigger;
    }

    if(self->sense_pin!=INVALIDPIN){
        // Disable all interruptions, reset the data
        gpio_set_irq_enabled(self->sense_pin,0xF, false);
        if(triac_data[self->sense_pin].alarm_activate!=ALARM_ID_INVALID){
            alarm_pool_cancel_alarm(triac_alarm_pool, triac_data[self->sense_pin].alarm_activate);
        }
        if(triac_data[self->sense_pin].alarm_deactivate!=ALARM_ID_INVALID){
            alarm_pool_cancel_alarm(triac_alarm_pool, triac_data[self->sense_pin].alarm_deactivate);
        }
        gpio_put_masked(triac_data[self->sense_pin].trigger_pins, triac_data[self->sense_pin].polarity?0:0xFFFFFFFF);
        triac_gpio_irq_handler(self->sense_pin, 0xF);
        reset_triac_data(self->sense_pin);
    }

    self->percent = percent;
    self->sense_pin = sense;
    self->watchdog = watchdogUs;

    reset_triac_data(self->sense_pin);
    triac_data[self->sense_pin].trigger_pins = trigger_pins;
    triac_data[self->sense_pin].active = 1;
    triac_data[self->sense_pin].polarity = (args[ARG_polarity].u_int!=0) ? 1 : 0;
    triac_data[self->sense_pin].user_watchdog_limit = 0;
    triac_data[self->sense_pin].user_on_time = onTimeUs;
    triac_data[self->sense_pin].ignore_timing = ignoreTimeUs;

    gpio_init_mask(triac_data[self->sense_pin].trigger_pins);
    gpio_put_masked(triac_data[self->sense_pin].trigger_pins, triac_data[self->sense_pin].polarity ? 0:0xFFFFFFFF);
    gpio_set_dir_masked(triac_data[self->sense_pin].trigger_pins, 0xFFFFFFFFUL);

    gpio_init(self->sense_pin);
    gpio_set_dir(self->sense_pin, false);
    gpio_set_pulls(self->sense_pin, false, false);
    gpio_set_irq_enabled(self->sense_pin, GPIO_IRQ_EDGE_FALL|GPIO_IRQ_EDGE_RISE, true);

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

        if(percent==0){
            triac_data[self->sense_pin].user_beeing_written = 1;
            triac_data[self->sense_pin].user_time_to_activate[0] = 0;
            triac_data[self->sense_pin].user_time_to_activate[1] = 0;
            triac_data[self->sense_pin].user_watchdog_limit = time_us_64()+self->watchdog;
            triac_data[self->sense_pin].user_beeing_written = 0;
        } else if(percent==100){
            triac_data[self->sense_pin].user_beeing_written = 1;
            triac_data[self->sense_pin].user_time_to_activate[0] = 1;
            triac_data[self->sense_pin].user_time_to_activate[1] = 1;
            triac_data[self->sense_pin].user_watchdog_limit = time_us_64()+self->watchdog;
            triac_data[self->sense_pin].user_beeing_written = 0;
        } else {
            uint32_t times[2];
            triac_controller_read_average_timings(self->sense_pin, times);
            times[0] = 1+((times[0]*TRIAC_POWERLINE[percent])>>16);
            times[1] = 1+((times[1]*TRIAC_POWERLINE[percent])>>16);

            triac_data[self->sense_pin].user_beeing_written = 1;
            triac_data[self->sense_pin].user_time_to_activate[0] = times[0];
            triac_data[self->sense_pin].user_time_to_activate[1] = times[1];
            triac_data[self->sense_pin].user_watchdog_limit = time_us_64()+self->watchdog;
            triac_data[self->sense_pin].user_beeing_written = 0;
        }
    }
    return MP_OBJ_NEW_SMALL_INT(self->percent);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(triac_controller_percent_obj, 1, 2, triac_controller_percent);


static mp_obj_t triac_controller_half_period(mp_obj_t self_obj) {
    mp_triac_controller_obj_t *self = (mp_triac_controller_obj_t*) MP_OBJ_TO_PTR(self_obj);
    return mp_obj_new_int(triac_controller_read_average_timings(self->sense_pin, NULL));
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_controller_half_period_obj,  triac_controller_half_period);


static mp_obj_t triac_controller_frequency(mp_obj_t self_obj) {
    mp_triac_controller_obj_t *self = (mp_triac_controller_obj_t*) MP_OBJ_TO_PTR(self_obj);
    int timing = triac_controller_read_average_timings(self->sense_pin, NULL);
    if(timing<=0) return mp_obj_new_float(0.0);
    return mp_obj_new_float(500000.0 / (float)timing);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_controller_frequency_obj,  triac_controller_frequency);

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
    { MP_ROM_QSTR(MP_QSTR_halfPeriod), MP_ROM_PTR(&triac_controller_half_period_obj) },
    { MP_ROM_QSTR(MP_QSTR_frequency), MP_ROM_PTR(&triac_controller_frequency_obj) },
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
