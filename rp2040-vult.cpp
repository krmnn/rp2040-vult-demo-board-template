#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/interp.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "project_config.h"
#include "midi_input.h"
#include "audio_subsystem.h"
#include "vult.h"

static const uint32_t PIN_DCDC_PSM_CTRL = 23;
audio_buffer_pool_t *ap;

Dsp_process_type ctx;
MIDIInput midi_input;

typedef struct {
    uint16_t ch0;
    uint16_t ch1;
    uint16_t ch2;
    uint16_t ch3;
} cv_channels_t;

cv_channels_t cv;

static inline uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

void process_cv(void)
{
    static uint32_t last_millis = 0;
    uint32_t millis = _millis();
    if (millis - last_millis > 5) {
        last_millis = millis;
        
        adc_select_input(0);
        cv.ch0 = adc_read();

        adc_select_input(1);
        cv.ch1 = adc_read();
    }
}

// MIDI callbacks
void note_on_callback(uint8_t note, uint8_t level)
{
    Dsp_noteOn(ctx, note, level, 1);
    printf("note on: %d %d\n", note, level);
}

void note_off_callback(uint8_t note, uint8_t level)
{
    Dsp_noteOff(ctx, note, 1);
    printf("note off: %d %d\n", note, level);
}

void cc_callback(uint8_t cc, uint8_t value)
{
    Dsp_controlChange(ctx, cc, value, 1);
    printf("cc: %d %d\n", cc, value);
}


int main()
{
    stdio_init_all();

    // Initialize Vult DSP. This must match the DSP code.
    Dsp_process_init(ctx);

    if (!set_sys_clock_khz(270000, false))
        printf("system clock 270MHz failed\n");
    else
        printf("system clock now 270MHz\n");

    // Initialize all Trigger inputs
    for(int pin=PIN_TRIG_IN_0; pin <= PIN_TRIG_BTN; pin++){
        gpio_init(pin);
        gpio_pull_up(pin);
        gpio_set_dir(pin, 0);
    }

    // DCDC PSM control
    // 0: PFM mode (best efficiency)
    // 1: PWM mode (improved ripple)
    gpio_init(PIN_DCDC_PSM_CTRL);
    gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1); // PWM mode for less Audio noise

    adc_init();

    for(int pin=PIN_CV_IN_0; pin <= PIN_POT_1; pin++){
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        adc_gpio_init(pin);
    }

    midi_input.setCCCallback(cc_callback);
    midi_input.setNoteOnCallback(note_on_callback);
    midi_input.setNoteOffCallback(note_off_callback);

    
    // Add your background UI processing or midi etc.
    while(true){
        midi_input.process();
    }
}

#ifdef __cplusplus
extern "C"
{
#endif

    void i2s_callback_func()
    {
        audio_buffer_t *buffer = take_audio_buffer(ap, false);
        if (buffer == NULL)
        {
            return;
        }
        int32_t *samples = (int32_t *)buffer->buffer->bytes;

        // process CV each round of samples
        process_cv();

        for (uint i = 0; i < buffer->max_sample_count; i++)
        {
            // do your audio processing here
            //int32_t smp = Dsp_process(ctx, 10240, 10240, 0);

            int32_t smp = Dsp_process(ctx, cv.ch0, cv.ch1, gpio_get(PIN_TRIG_IN_0), gpio_get(PIN_TRIG_IN_1), gpio_get(PIN_TRIG_IN_2), gpio_get(PIN_TRIG_IN_3));
            samples[i * 2 + 0] = smp; // LEFT
            samples[i * 2 + 1] = smp; // RIGHT
        }
        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(ap, buffer);
        return;
    }

#ifdef __cplusplus
}
#endif