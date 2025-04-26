

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
    {&mp_triac_power_analyzer_type}, INVALIDPIN, INVALIDPIN, 0, 0, 0, 1.0f, 1.0f, 1.0f,
    0,   NULL, NULL, 0,    0, 0,    0, 0, 0,    0, 0, 0,    0, 0, 0,    0, INVALID_DMA_CHANNEL
};


// Interrupt stuff
static uint16_t *triac_power_analyzer_main_buffer = NULL;

static bool mp_triac_power_analyzer_timer_tick(struct repeating_timer *rt) {
    adc_hw->cs |= ADC_CS_START_ONCE_BITS;
    return true;
}

static void mp_triac_power_analyzer_dma_complete_irq(){
    dma_hw->ints0 = 1u << tpa_singleton.dma_chan;
    dma_channel_set_read_addr(tpa_singleton.dma_chan, &adc_hw->fifo, true);

    tpa_singleton.interrupt_count++; 
    // Lasso de inicialização:
    for(uint i=0; i<tpa_singleton.histo_size; i++){
        tpa_singleton.histo_voltage[i] = 0;
        tpa_singleton.histo_current[i] = 0;
    }

    // Primeiro lasso: calculando apenas a média, pois todas as outras operações dependem dela
    uint32_t sumV, sumC;
    uint16_t *ptr = triac_power_analyzer_main_buffer;
    if(tpa_singleton.voltage_pin<tpa_singleton.current_pin){
        sumV = *ptr;
        ptr++;
        sumC = *ptr;
        ptr++;
    } else {
        sumV = ptr[2*tpa_singleton.analized_samples-1];
        sumC = *ptr;
        ptr++;
    }
    for(uint i=1; i<tpa_singleton.analized_samples; i++){
        sumV += *ptr;
        ptr++;
        sumC += *ptr;
        ptr++;
    }
    uint32_t offsetV = sumV/tpa_singleton.analized_samples;
    uint32_t offsetC = sumC/tpa_singleton.analized_samples;
    tpa_singleton.offset_voltage = offsetV;
    tpa_singleton.offset_current = offsetC;

    // Segundo lasso: Calculando todas as outras estatisticas
    ptr = triac_power_analyzer_main_buffer;
    int lastV, maxV, minV, maxC, minC, maxP, minP;
    uint32_t sqsumV=0, sqsumC=0;
    int32_t sumP=0;
    uint16_t histoPos = 0;
    uint8_t histoCount = 0;
    if(tpa_singleton.voltage_pin<tpa_singleton.current_pin){
        lastV = triac_power_analyzer_main_buffer[0]-offsetV;
        maxC = triac_power_analyzer_main_buffer[1]-offsetC;
    } else {
        lastV = triac_power_analyzer_main_buffer[1]-offsetV;
        maxC = triac_power_analyzer_main_buffer[0]-offsetC;
    }
    maxV = lastV;
    minV = lastV;
    minC = maxC;
    maxP = maxV*maxC;
    minP = maxP;

    for(uint i=1; i<tpa_singleton.analized_samples; i++){
        int v, c, p;
        if(tpa_singleton.voltage_pin<tpa_singleton.current_pin){
            v = *ptr-offsetV;
            ptr++;
            c = *ptr-offsetC;
            ptr++;
        } else {
            c = *ptr-offsetC;
            ptr++;
            v = *ptr-offsetV;
            ptr++;
        }
        p = v*c;
        sqsumV += v*v;
        sqsumC += c*c;
        sumP += p;
        if(v>maxV) maxV=v;
        if(v<minV) minV=v;
        if(c>maxC) maxC=c;
        if(c<minC) minC=c;
        if(p>maxP) maxP=p;
        if(p<minP) minP=p;
        if(tpa_singleton.histo_size>0){
            if(histoPos==0){
                if(lastV<0 && v>0){
                    tpa_singleton.histo_voltage[0] += v;
                    tpa_singleton.histo_current[0] += c;
                    histoPos = 1;
                    histoCount++;
                }
            } else {
                if(histoPos>=tpa_singleton.histo_size-1){
                    tpa_singleton.histo_voltage[histoPos] += v;
                    tpa_singleton.histo_current[histoPos] += c;
                    histoPos++;
                } else {
                    histoPos = 0;
                }
            }
        }
        lastV = v;
    }

    tpa_singleton.histo_count = histoCount;
    tpa_singleton.offset_voltage = offsetV;
    tpa_singleton.offset_current = offsetC;
    tpa_singleton.pos_peak_voltage = maxV;
    tpa_singleton.pos_peak_current = maxC;
    tpa_singleton.pos_peak_power = maxP;
    tpa_singleton.neg_peak_voltage = minV;
    tpa_singleton.neg_peak_current = minC;
    tpa_singleton.neg_peak_power = minP;
    tpa_singleton.squaresum_voltage = sqsumV;
    tpa_singleton.squaresum_current = sqsumC;
    tpa_singleton.sum_power = sumP;
}


// General configs ======================================================================================

static void mp_triac_power_analyzer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_triac_power_analyzer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "Triac.PowerAnalyzer(volt=%d,amp=%d)", self->voltage_pin, self->current_pin);
}

static void mp_triac_power_analyzer_init_helper(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_voltage_pin, ARG_current_pin, ARG_sample_rate, ARG_analized_samples, ARG_histo_size};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_voltage_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_int = 0} },
        { MP_QSTR_current_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_int = 1} },
        { MP_QSTR_sample_rate, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 6000} },
        { MP_QSTR_analized_samples, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 600} },
        { MP_QSTR_histo_size, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 120} },
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
    if(args[ARG_analized_samples].u_int<10 || args[ARG_analized_samples].u_int>=4000){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid analized sample count!"));
    }
    if(args[ARG_histo_size].u_int<0 || args[ARG_histo_size].u_int>=args[ARG_analized_samples].u_int){
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid histo size!"));
    }

    if(triac_power_analyzer_main_buffer==NULL){
        triac_power_analyzer_main_buffer = m_malloc(2*2*args[ARG_analized_samples].u_int);
        tpa_singleton.analized_samples = args[ARG_analized_samples].u_int;
    }
    if(args[ARG_analized_samples].u_int!=tpa_singleton.analized_samples){
        if(triac_power_analyzer_main_buffer!=NULL){
            m_free(triac_power_analyzer_main_buffer);
        }
        triac_power_analyzer_main_buffer = m_malloc(2*2*args[ARG_analized_samples].u_int);
        tpa_singleton.analized_samples = args[ARG_analized_samples].u_int;
    }

    if(tpa_singleton.histo_voltage==NULL || tpa_singleton.histo_current){
        tpa_singleton.histo_voltage = m_malloc(4*args[ARG_histo_size].u_int);
        tpa_singleton.histo_current = m_malloc(4*args[ARG_histo_size].u_int);
        tpa_singleton.analized_samples = args[ARG_histo_size].u_int;
    }
    if(args[ARG_histo_size].u_int!=tpa_singleton.histo_size){
        if(tpa_singleton.histo_voltage!=NULL){
            m_free(tpa_singleton.histo_voltage);
        }
        if(tpa_singleton.histo_current!=NULL){
            m_free(tpa_singleton.histo_current);
        }
        tpa_singleton.histo_voltage = m_malloc(4*args[ARG_histo_size].u_int);
        tpa_singleton.histo_current = m_malloc(4*args[ARG_histo_size].u_int);
        tpa_singleton.analized_samples = args[ARG_histo_size].u_int;
    }

    tpa_singleton.voltage_pin = args[ARG_voltage_pin].u_int;
    tpa_singleton.current_pin = args[ARG_current_pin].u_int;

    adc_init();
    const uint8_t pins[4] = {26, 27, 28, 29};
    adc_gpio_init(pins[tpa_singleton.voltage_pin]);
    adc_gpio_init(pins[tpa_singleton.current_pin]);

    adc_set_round_robin((1<<tpa_singleton.voltage_pin)|(1<<tpa_singleton.current_pin));

    adc_fifo_setup(
        /* en= */ true,     // enable FIFO
        /* dreq_en= */ true,// pace DMA with DREQ when >=1 word in FIFO
        /* thresh= */ 1,    // DREQ when >=1 sample
        /* err= */ false,   // don't include error bit
        /* byte_shift= */ false
    );

    if (tpa_singleton.dma_chan >= 0) {
        dma_channel_cleanup(tpa_singleton.dma_chan);
        dma_channel_unclaim(tpa_singleton.dma_chan);
        tpa_singleton.dma_chan = INVALID_DMA_CHANNEL;
    }

    tpa_singleton.dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(tpa_singleton.dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16); // 16-bit reads
    channel_config_set_read_increment(&cfg, false);           // FIFO register
    channel_config_set_write_increment(&cfg, true);           // buffer indexing
    channel_config_set_dreq(&cfg, DREQ_ADC);                  // pace by ADC FIFO

    dma_channel_configure(
        tpa_singleton.dma_chan,
        &cfg,
        triac_power_analyzer_main_buffer,
        &adc_hw->fifo,
        2*tpa_singleton.analized_samples,
        true
    );

    dma_channel_set_irq0_enabled(tpa_singleton.dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, mp_triac_power_analyzer_dma_complete_irq);
    irq_set_enabled(DMA_IRQ_0, true);

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

    if(tpa_singleton.dma_chan>=0){
        irq_set_enabled(DMA_IRQ_0, false);
        dma_channel_cleanup(tpa_singleton.dma_chan);
        dma_channel_unclaim(tpa_singleton.dma_chan);
        tpa_singleton.dma_chan = INVALID_DMA_CHANNEL;
    }
    adc_init();

    if(tpa_singleton.histo_voltage!=NULL){
        m_free(tpa_singleton.histo_voltage);
    }
    if(tpa_singleton.histo_current!=NULL){
        m_free(tpa_singleton.histo_current);
    }
    tpa_singleton.analized_samples = 0;

    if(triac_power_analyzer_main_buffer!=NULL){
        m_free(triac_power_analyzer_main_buffer);
    }
    tpa_singleton.analized_samples = 0;

    return mp_const_none;
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
    return mp_obj_new_int(tpa_singleton.analized_samples);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_analized_samples_obj,  triac_power_analyzer_analized_samples);

static mp_obj_t triac_power_analyzer_histo_size(mp_obj_t self_obj) {
    return mp_obj_new_int(tpa_singleton.histo_size);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_histo_size_obj,  triac_power_analyzer_histo_size);

static mp_obj_t triac_power_analyzer_running(mp_obj_t self_obj) {
    return tpa_singleton.running ? mp_const_true : mp_const_false;
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_running_obj,  triac_power_analyzer_running);

static mp_obj_t triac_power_analyzer_voltage_mult(size_t n_args, const mp_obj_t *args) {
    if(n_args==2){
        tpa_singleton.voltage_multiplier = mp_obj_get_float(args[1]);
    }
    return mp_obj_new_float(tpa_singleton.voltage_multiplier);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(triac_power_analyzer_voltage_mult_obj, 1, 2, triac_power_analyzer_voltage_mult);

static mp_obj_t triac_power_analyzer_current_mult(size_t n_args, const mp_obj_t *args) {
    if(n_args==2){
        tpa_singleton.current_multiplier = mp_obj_get_float(args[1]);
    }
    return mp_obj_new_float(tpa_singleton.current_multiplier);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(triac_power_analyzer_current_mult_obj, 1, 2, triac_power_analyzer_current_mult);

static mp_obj_t triac_power_analyzer_get_offsets(mp_obj_t self_obj) {
    uint8_t initial_interrupt_count, final_interrupt_count;
    uint16_t offset_voltage, offset_current;
    do{
        initial_interrupt_count = tpa_singleton.interrupt_count;
        offset_voltage = tpa_singleton.offset_voltage;
        offset_current = tpa_singleton.offset_current;
        final_interrupt_count = tpa_singleton.interrupt_count;
    }while(initial_interrupt_count!=final_interrupt_count);

    mp_obj_t result_dict[2 * 2];
    result_dict[0] = MP_ROM_QSTR(MP_QSTR_v);
    result_dict[1] = mp_obj_new_int(offset_voltage);
    result_dict[2] = MP_ROM_QSTR(MP_QSTR_c);
    result_dict[3] = mp_obj_new_int(offset_current);
    return mp_obj_dict_make_new(&mp_type_dict, 0, 2, result_dict);
}
MP_DEFINE_CONST_FUN_OBJ_1(triac_power_analyzer_get_offsets_obj,  triac_power_analyzer_get_offsets);

static mp_obj_t triac_power_analyzer_get_peaks(mp_obj_t self_obj) {
    uint8_t initial_interrupt_count, final_interrupt_count;
    uint16_t pos_peak_voltage, pos_peak_current, neg_peak_voltage, neg_peak_current;
    int32_t pos_peak_power, neg_peak_power;
    do{
        initial_interrupt_count = tpa_singleton.interrupt_count;
        pos_peak_voltage = tpa_singleton.pos_peak_voltage;
        pos_peak_current = tpa_singleton.pos_peak_current;
        pos_peak_power = tpa_singleton.pos_peak_power;
        neg_peak_voltage = tpa_singleton.neg_peak_voltage;
        neg_peak_current = tpa_singleton.neg_peak_current;
        neg_peak_power = tpa_singleton.neg_peak_power;
        final_interrupt_count = tpa_singleton.interrupt_count;
    }while(initial_interrupt_count!=final_interrupt_count);

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
    uint8_t initial_interrupt_count, final_interrupt_count;
    int32_t sum_power;
    uint32_t squaresum_voltage, squaresum_current;
    do{
        initial_interrupt_count = tpa_singleton.interrupt_count;
        squaresum_voltage = tpa_singleton.squaresum_voltage;
        squaresum_current = tpa_singleton.squaresum_current;
        sum_power = tpa_singleton.sum_power;
        final_interrupt_count = tpa_singleton.interrupt_count;
    }while(initial_interrupt_count!=final_interrupt_count);
    float samplenr = (float)tpa_singleton.analized_samples;
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
