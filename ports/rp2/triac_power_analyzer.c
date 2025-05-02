

// #include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/mpprint.h"
#include "triac.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/regs/adc.h"
#include "hardware/timer.h"
#include "hardware/structs/iobank0.h"
#include "hardware/regs/intctrl.h"

#define INVALIDPIN (255)
#define MAX_NUM_CHANNELS (4)
#define INVALID_DMA_CHANNEL (-1)
static mp_triac_power_analyzer_obj_t tpa_singleton = {
    {&mp_triac_power_analyzer_type}, INVALIDPIN, INVALIDPIN, 0, /*0, 0,*/ 1.0f, 1.0f, 1.0f,
    0, 0, 1, 0,
    {{{0}, {0},   {0}, {0}, 0,   0, 0, 0,  0, 0, 0,  0, 0, 0,  0,   0, 0, 0},
    {{0}, {0},   {0}, {0}, 0,   0, 0, 0,  0, 0, 0,  0, 0, 0,  0,   0, 0, 0}},
    0, 0,   0, 0, 0,   0, 0, 0,   0, 0, 0,   0
};


// Interrupt stuff
static uint32_t volatile intcount = 0;

static bool mp_triac_power_analyzer_timer_tick(struct repeating_timer *rt) {
    // Interrupt logic:
    // Main part: ADC sampling
    // results from current_reads[i-1] are in the result.
    // This interrupt shall read voltage_reads[i] and prepare for current_reads[i], for next interruption
    // Meanwhile we wait for voltage_read, we can calculate partial results of do a full calculation
    // Also: We can lose a few samples while preparing the results, the
    // more important is the cadence between reads in a set of samples
    // Secondary part: Double buffering and interrupt processing
    // Since the end of sampling is taking too long and potentially interfering with
    // I2C display communication timing, the processing is beeing split per index
    // and being processed with double buffering.
    uint16_t last_current_reading = adc_hw->result;
    hw_write_masked(&adc_hw->cs, tpa_singleton.voltage_pin << ADC_CS_AINSEL_LSB, ADC_CS_AINSEL_BITS);
    hw_set_bits(&adc_hw->cs, ADC_CS_START_ONCE_BITS);

    uint64_t start = time_us_64();

    mp_triac_power_analyzer_doublebuffer_obj_t *iobj = &tpa_singleton.db[tpa_singleton.i_phase];
    mp_triac_power_analyzer_doublebuffer_obj_t *uobj = &tpa_singleton.db[tpa_singleton.u_phase];
    tpa_singleton.interrupt_count++;
    if(tpa_singleton.mbp==0xFFFF){
        // First time running, or if running right after a sample.
        // last_current_reading is probably invalid... only doing initializations
        // at least i_phase and u_phase should be already be in the right places
        memset(iobj->histo_voltage, 0, sizeof(int32_t)*POWER_ANALYZER_HISTO_SIZE);
        memset(iobj->histo_current, 0, sizeof(int32_t)*POWER_ANALYZER_HISTO_SIZE);
        iobj->histo_count = 0;

        iobj->i_sumV = 0;
        iobj->i_sumC = 0;
        iobj->i_sumP = 0;
        iobj->i_maxV = 0;
        iobj->i_maxC = 0;
        iobj->i_maxP = 0;
        iobj->i_minV = 0;
        iobj->i_minC = 0;
        iobj->i_minP = 0;
        iobj->i_histoPos = 0;
        iobj->i_lastV = 0;
        iobj->i_sqsumV = 0;
        iobj->i_sqsumC = 0;

        tpa_singleton.mbp = 0;
    } else if(tpa_singleton.mbp < POWER_ANALYZER_BUFFER_SIZE){
        // Registering and getting the last position in the interrupt buffer
        int16_t v = iobj->voltage_buffer[tpa_singleton.mbp];
        int16_t c = last_current_reading&0x0FFF;
        iobj->current_buffer[tpa_singleton.mbp] = c;

        // Dealing only with "current sample" first
        if(tpa_singleton.mbp==0){
            iobj->i_sumV = v;
            iobj->i_sumC = c;
            iobj->i_maxV = v;
            iobj->i_maxC = c;
            iobj->i_minV = v;
            iobj->i_minC = c;
        } else {
            iobj->i_sumV += v;
            iobj->i_sumC += c;
            if(v>iobj->i_maxV)iobj->i_maxV = v;
            if(v<iobj->i_minV)iobj->i_minV = v;
            if(c>iobj->i_maxC)iobj->i_maxC = c;
            if(c<iobj->i_minC)iobj->i_minC = c;
        }
        // And then dealing with last sampling period
        v = uobj->voltage_buffer[tpa_singleton.mbp]-tpa_singleton.offset_voltage;
        c = uobj->current_buffer[tpa_singleton.mbp]-tpa_singleton.offset_current;
        int32_t p = v*c;
        if(tpa_singleton.mbp==0){
            iobj->i_maxP = p;
            iobj->i_minP = p;
            iobj->i_lastV = v;
        } else {
            if(p>iobj->i_maxP) iobj->i_maxP = p;
            if(p<iobj->i_minP) iobj->i_minP = p;
        }
        iobj->i_sqsumV += v*v;
        iobj->i_sqsumC += c*c;
        iobj->i_sumP += p;

        if(iobj->i_histoPos==0){
            if(iobj->i_lastV<0 && v>0 && ((tpa_singleton.mbp+POWER_ANALYZER_HISTO_SIZE)<POWER_ANALYZER_BUFFER_SIZE)){
                iobj->histo_voltage[0] += v;
                iobj->histo_current[0] += c;
                iobj->i_histoPos = 1;
                iobj->histo_count++;
            }
        } else {
            if(iobj->i_histoPos>=POWER_ANALYZER_HISTO_SIZE){
                iobj->i_histoPos = 0;
            } else {
                iobj->histo_voltage[iobj->i_histoPos] += v;
                iobj->histo_current[iobj->i_histoPos] += c;
                iobj->i_histoPos++;
            }
        }
        iobj->i_lastV = v;

        tpa_singleton.mbp++;
    }
    if(tpa_singleton.mbp>=POWER_ANALYZER_BUFFER_SIZE){
        tpa_singleton.offset_voltage = iobj->i_sumV/POWER_ANALYZER_BUFFER_SIZE;
        tpa_singleton.offset_current = iobj->i_sumC/POWER_ANALYZER_BUFFER_SIZE;
        tpa_singleton.pos_peak_voltage = iobj->i_maxV - tpa_singleton.offset_voltage;
        tpa_singleton.pos_peak_current = iobj->i_minV - tpa_singleton.offset_current;
        tpa_singleton.neg_peak_voltage = iobj->i_maxC - tpa_singleton.offset_voltage;
        tpa_singleton.neg_peak_current = iobj->i_minC - tpa_singleton.offset_current;
        tpa_singleton.pos_peak_power = iobj->i_maxP;
        tpa_singleton.neg_peak_power = iobj->i_minP;
        tpa_singleton.squaresum_voltage = iobj->i_sqsumV;
        tpa_singleton.squaresum_current = iobj->i_sqsumC;
        tpa_singleton.sum_power = iobj->i_sumP;

        tpa_singleton.mbp=0xFFFF;
        // switching user and interrupt buffer contexts
        tpa_singleton.u_phase = tpa_singleton.i_phase;
        tpa_singleton.i_phase = (tpa_singleton.i_phase)?0:1;
        intcount--;
    } else {
        // Done everything we could, so waiting for the prepared ADC sample...
        while (!(adc_hw->cs & ADC_CS_READY_BITS))
            tight_loop_contents();
        uint16_t voltage_reading = adc_hw->result;
        // As fast as possible, prepare the current sampling
        hw_write_masked(&adc_hw->cs, tpa_singleton.current_pin << ADC_CS_AINSEL_LSB, ADC_CS_AINSEL_BITS);
        hw_set_bits(&adc_hw->cs, ADC_CS_START_ONCE_BITS);
        iobj->voltage_buffer[tpa_singleton.mbp] = voltage_reading&0x0FFF;
    }
    uint32_t dt = time_us_64()-start;
    if(dt>intcount) intcount = dt;
    return true;
}

// General configs ======================================================================================

static void mp_triac_power_analyzer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_triac_power_analyzer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "PowerAnalyzer(volt=%d,amp=%d)", self->voltage_pin, self->current_pin);
}

static void mp_triac_power_analyzer_init_helper(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_voltage_pin, ARG_current_pin, ARG_sample_rate, ARG_analized_samples, ARG_histo_size};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_voltage_pin,      MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_current_pin,      MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_sample_rate,      MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 6000} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    if(args[ARG_voltage_pin].u_int<0 || args[ARG_voltage_pin].u_int>=MAX_NUM_CHANNELS){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid Voltage ADC input!"));
    }
    if(args[ARG_current_pin].u_int<0 || args[ARG_current_pin].u_int>=MAX_NUM_CHANNELS){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid Current ADC input!"));
    }
    if(args[ARG_voltage_pin].u_int==args[ARG_current_pin].u_int){
        mp_raise_ValueError(MP_ERROR_TEXT("Voltage and Current channels must be different!"));
    }
    if(args[ARG_sample_rate].u_int<0 || args[ARG_sample_rate].u_int>=100000){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid sample rate!"));
    }
    tpa_singleton.mbp = 0xFFFF;

    tpa_singleton.db[0].histo_count = 0;
    tpa_singleton.db[1].histo_count = 0;

    tpa_singleton.voltage_pin = args[ARG_voltage_pin].u_int;
    tpa_singleton.current_pin = args[ARG_current_pin].u_int;

    adc_gpio_init(26+tpa_singleton.voltage_pin);
    adc_gpio_init(26+tpa_singleton.current_pin);
    adc_init();
    adc_set_clkdiv(0);

    if(tpa_singleton.sample_rate!=0){
        tpa_singleton.sample_rate = 0;
        cancel_repeating_timer(&tpa_singleton.timer);
    }
    if(add_repeating_timer_us(-(int)(1000000U / args[ARG_sample_rate].u_int), mp_triac_power_analyzer_timer_tick, NULL, &tpa_singleton.timer)){
        tpa_singleton.sample_rate = args[ARG_sample_rate].u_int;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Error starting timer!"));
    }
    tpa_singleton.voltage_multiplier = 1.0f;
    tpa_singleton.current_multiplier = 1.0f;
    tpa_singleton.power_multiplier = tpa_singleton.voltage_multiplier*tpa_singleton.current_multiplier;
    tpa_singleton.running = 1;
}

static mp_obj_t mp_triac_power_analyzer_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    mp_triac_power_analyzer_init_helper(n_args-1, args+1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(triac_power_analyzer_init_obj, 1, mp_triac_power_analyzer_init);

static mp_obj_t mp_triac_power_analyzer_close(mp_obj_t self_in) {
    tpa_singleton.running = 0;

    if(tpa_singleton.sample_rate!=0){
        cancel_repeating_timer(&tpa_singleton.timer);
        tpa_singleton.sample_rate = 0;
    }

    adc_init();
    tpa_singleton.voltage_pin = INVALIDPIN;
    tpa_singleton.current_pin = INVALIDPIN;

    return mp_const_none;
}
void triac_power_analyzer_deinit(){
    mp_triac_power_analyzer_close(mp_const_none);
}

MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_close_obj, mp_triac_power_analyzer_close);


static mp_obj_t mp_triac_power_analyzer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // create new Graphicscontroller object
    if(n_args!=0){
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        mp_triac_power_analyzer_init_helper(n_args, args, &kw_args);
    }
    return MP_OBJ_FROM_PTR(&tpa_singleton);
}

// Getters/Setters ===================================================================================
static mp_obj_t triac_power_analyzer_voltage_pin(mp_obj_t self_obj) {
    return mp_obj_new_int(tpa_singleton.voltage_pin);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_voltage_pin_obj,  triac_power_analyzer_voltage_pin);

static mp_obj_t triac_power_analyzer_current_pin(mp_obj_t self_obj) {
    return mp_obj_new_int(tpa_singleton.current_pin);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_current_pin_obj,  triac_power_analyzer_current_pin);

static mp_obj_t triac_power_analyzer_sample_rate(mp_obj_t self_obj) {
    return mp_obj_new_int(tpa_singleton.sample_rate);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_sample_rate_obj,  triac_power_analyzer_sample_rate);

static mp_obj_t triac_power_analyzer_analized_samples(mp_obj_t self_obj) {
    return mp_obj_new_int(POWER_ANALYZER_BUFFER_SIZE);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_analized_samples_obj,  triac_power_analyzer_analized_samples);

static mp_obj_t triac_power_analyzer_histo_size(mp_obj_t self_obj) {
    return mp_obj_new_int(POWER_ANALYZER_HISTO_SIZE);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_histo_size_obj,  triac_power_analyzer_histo_size);

static mp_obj_t triac_power_analyzer_running(mp_obj_t self_obj) {
    // mp_printf(&mp_sys_stdout_print, "running\n");
    return tpa_singleton.running ? mp_const_true : mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_running_obj,  triac_power_analyzer_running);

static mp_obj_t triac_power_analyzer_voltage_mult(size_t n_args, const mp_obj_t *args) {
    if(n_args==2){
        tpa_singleton.voltage_multiplier = mp_obj_get_float(args[1]);
        tpa_singleton.power_multiplier = tpa_singleton.voltage_multiplier*tpa_singleton.current_multiplier;
    }
    return mp_obj_new_float(tpa_singleton.voltage_multiplier);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(triac_power_analyzer_voltage_mult_obj, 1, 2, triac_power_analyzer_voltage_mult);

static mp_obj_t triac_power_analyzer_current_mult(size_t n_args, const mp_obj_t *args) {
    if(n_args==2){
        tpa_singleton.current_multiplier = mp_obj_get_float(args[1]);
        tpa_singleton.power_multiplier = tpa_singleton.voltage_multiplier*tpa_singleton.current_multiplier;
    }
    return mp_obj_new_float(tpa_singleton.current_multiplier);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(triac_power_analyzer_current_mult_obj, 1, 2, triac_power_analyzer_current_mult);

static mp_obj_t triac_power_analyzer_get_offsets(mp_obj_t self_obj) {
    uint8_t initial_phase, final_phase;
    uint16_t offset_voltage, offset_current;
    do{
        initial_phase = tpa_singleton.interrupt_count;
        offset_voltage = tpa_singleton.offset_voltage;
        offset_current = tpa_singleton.offset_current;
        final_phase = tpa_singleton.interrupt_count;
    }while(initial_phase!=final_phase);

    mp_obj_t result_dict[3 * 2];
    result_dict[0] = MP_ROM_QSTR(MP_QSTR_v);
    result_dict[1] = mp_obj_new_int(offset_voltage);
    result_dict[2] = MP_ROM_QSTR(MP_QSTR_c);
    result_dict[3] = mp_obj_new_int(offset_current);
    result_dict[4] = MP_ROM_QSTR(MP_QSTR_i);
    result_dict[5] = mp_obj_new_int(intcount);
    return mp_obj_dict_make_new(&mp_type_dict, 0, 3, result_dict);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_get_offsets_obj,  triac_power_analyzer_get_offsets);

static mp_obj_t triac_power_analyzer_get_peaks(mp_obj_t self_obj) {
    uint8_t initial_phase, final_phase;
    uint16_t pos_peak_voltage, pos_peak_current, neg_peak_voltage, neg_peak_current;
    int32_t pos_peak_power, neg_peak_power;
    do{
        initial_phase = tpa_singleton.u_phase;
        pos_peak_voltage = tpa_singleton.pos_peak_voltage;
        pos_peak_current = tpa_singleton.pos_peak_current;
        pos_peak_power = tpa_singleton.pos_peak_power;
        neg_peak_voltage = tpa_singleton.neg_peak_voltage;
        neg_peak_current = tpa_singleton.neg_peak_current;
        neg_peak_power = tpa_singleton.neg_peak_power;
        final_phase = tpa_singleton.u_phase;
    }while(initial_phase!=final_phase);

    mp_obj_t result_dict[6 * 2];
    result_dict[0] = MP_ROM_QSTR(MP_QSTR_p_v);
    result_dict[1] = mp_obj_new_float(pos_peak_voltage*tpa_singleton.voltage_multiplier);
    result_dict[2] = MP_ROM_QSTR(MP_QSTR_p_c);
    result_dict[3] = mp_obj_new_float(pos_peak_current*tpa_singleton.current_multiplier);
    result_dict[4] = MP_ROM_QSTR(MP_QSTR_p_p);
    result_dict[5] = mp_obj_new_float(pos_peak_power*tpa_singleton.power_multiplier);
    result_dict[6] = MP_ROM_QSTR(MP_QSTR_n_v);
    result_dict[7] = mp_obj_new_float(neg_peak_voltage*tpa_singleton.voltage_multiplier);
    result_dict[8] = MP_ROM_QSTR(MP_QSTR_n_c);
    result_dict[9] = mp_obj_new_float(neg_peak_current*tpa_singleton.current_multiplier);
    result_dict[10] = MP_ROM_QSTR(MP_QSTR_n_p);
    result_dict[11] = mp_obj_new_float(neg_peak_power*tpa_singleton.power_multiplier);
    return mp_obj_dict_make_new(&mp_type_dict, 0, 6, result_dict);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_get_peaks_obj,  triac_power_analyzer_get_peaks);

static mp_obj_t triac_power_analyzer_get_rms(mp_obj_t self_obj) {
    uint8_t initial_phase, final_phase;
    int32_t sum_power;
    uint32_t squaresum_voltage, squaresum_current;
    do{
        initial_phase = tpa_singleton.u_phase;
        squaresum_voltage = tpa_singleton.squaresum_voltage;
        squaresum_current = tpa_singleton.squaresum_current;
        sum_power = tpa_singleton.sum_power;
        final_phase = tpa_singleton.u_phase;
    }while(initial_phase!=final_phase);
    float samplenr = (float)POWER_ANALYZER_BUFFER_SIZE;
    mp_obj_t result_dict[3 * 2];
    result_dict[0] = MP_ROM_QSTR(MP_QSTR_v);
    result_dict[1] = mp_obj_new_float( sqrt(squaresum_voltage/samplenr)*tpa_singleton.voltage_multiplier );
    result_dict[2] = MP_ROM_QSTR(MP_QSTR_c);
    result_dict[3] = mp_obj_new_float( sqrt(squaresum_current/samplenr)*tpa_singleton.current_multiplier );
    result_dict[4] = MP_ROM_QSTR(MP_QSTR_p);
    result_dict[5] = mp_obj_new_float( (sum_power/samplenr)*tpa_singleton.power_multiplier );

    return mp_obj_dict_make_new(&mp_type_dict, 0, 3, result_dict);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_get_rms_obj,  triac_power_analyzer_get_rms);

static mp_obj_t triac_power_analyzer_voltage_histo(mp_obj_t self_obj) {
    uint8_t initial_phase, final_phase;
    int32_t *voltage_histo;
    uint8_t histo_count;
    voltage_histo = m_malloc(4*POWER_ANALYZER_HISTO_SIZE);
    do{
        initial_phase = tpa_singleton.u_phase;
        for(uint i=0; i<POWER_ANALYZER_HISTO_SIZE; i++){
            voltage_histo[i] = tpa_singleton.db[tpa_singleton.u_phase].histo_voltage[i];
        }
        histo_count = tpa_singleton.db[tpa_singleton.u_phase].histo_count;
        final_phase = tpa_singleton.u_phase;
    }while(initial_phase!=final_phase);

    if(histo_count==0) return mp_const_none;

    mp_obj_list_t *objlist = m_new_obj(mp_obj_list_t);
    mp_obj_list_init(objlist, POWER_ANALYZER_HISTO_SIZE);
    mp_obj_t list = MP_OBJ_FROM_PTR(objlist);
    for(uint i=0; i<POWER_ANALYZER_HISTO_SIZE; i++){
        objlist->items[i] = mp_obj_new_float(voltage_histo[i]*tpa_singleton.voltage_multiplier/histo_count);
    }
    m_free(voltage_histo);
    return list;
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_voltage_histo_obj,  triac_power_analyzer_voltage_histo);

static mp_obj_t triac_power_analyzer_current_histo(mp_obj_t self_obj) {
    uint8_t initial_phase, final_phase;
    int32_t *current_histo;
    uint8_t histo_count;
    current_histo = m_malloc(sizeof(int32_t)*POWER_ANALYZER_HISTO_SIZE);
    do{
        initial_phase = tpa_singleton.u_phase;
        for(uint i=0; i<POWER_ANALYZER_HISTO_SIZE; i++){
            current_histo[i] = tpa_singleton.db[tpa_singleton.u_phase].histo_current[i];
        }
        histo_count = tpa_singleton.db[tpa_singleton.u_phase].histo_count;
        final_phase = tpa_singleton.u_phase;
    }while(initial_phase!=final_phase);

    if(histo_count==0) return mp_const_none;

    mp_obj_list_t *objlist = m_new_obj(mp_obj_list_t);
    mp_obj_list_init(objlist, POWER_ANALYZER_HISTO_SIZE);
    mp_obj_t list = MP_OBJ_FROM_PTR(objlist);
    for(uint i=0; i<POWER_ANALYZER_HISTO_SIZE; i++){
        objlist->items[i] = mp_obj_new_float(current_histo[i]*tpa_singleton.current_multiplier/histo_count);
    }
    m_free(current_histo);

    return list;
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_current_histo_obj,  triac_power_analyzer_current_histo);


// Main methods =====================================================================


static const mp_rom_map_elem_t triac_power_analyzer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&triac_power_analyzer_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&triac_power_analyzer_close_obj) },
    // Getters / Setters
    { MP_ROM_QSTR(MP_QSTR_voltage_pin), MP_ROM_PTR(&triac_power_analyzer_voltage_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_current_pin), MP_ROM_PTR(&triac_power_analyzer_current_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_sample_rate), MP_ROM_PTR(&triac_power_analyzer_sample_rate_obj) },
    { MP_ROM_QSTR(MP_QSTR_analized_samples), MP_ROM_PTR(&triac_power_analyzer_analized_samples_obj) },
    { MP_ROM_QSTR(MP_QSTR_histo_size), MP_ROM_PTR(&triac_power_analyzer_histo_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_running), MP_ROM_PTR(&triac_power_analyzer_running_obj) },
    { MP_ROM_QSTR(MP_QSTR_voltage_mult), MP_ROM_PTR(&triac_power_analyzer_voltage_mult_obj) },
    { MP_ROM_QSTR(MP_QSTR_current_mult), MP_ROM_PTR(&triac_power_analyzer_current_mult_obj) },
    // Main methods
    { MP_ROM_QSTR(MP_QSTR_get_offsets), MP_ROM_PTR(&triac_power_analyzer_get_offsets_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_peaks), MP_ROM_PTR(&triac_power_analyzer_get_peaks_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_rms), MP_ROM_PTR(&triac_power_analyzer_get_rms_obj) },
    { MP_ROM_QSTR(MP_QSTR_voltage_histo), MP_ROM_PTR(&triac_power_analyzer_voltage_histo_obj) },
    { MP_ROM_QSTR(MP_QSTR_current_histo), MP_ROM_PTR(&triac_power_analyzer_current_histo_obj) },
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
