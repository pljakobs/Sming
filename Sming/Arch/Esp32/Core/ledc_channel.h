#ifndef LEDC_CHANNEL_H
#define LEDC_CHANNEL_H
#include <debug_progmem.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "singleton.h"
#include <hal/ledc_types.h>

#ifndef LEDC_TIMERS_H
#import "ledc_timer.h"
#endif

class ledc_channel{
    public:
        esp_err_t ledc_channel(ledc_speedmode_t mode, int gpio, ledc_timer_t timer, uint32_t duty, int hpoint);
        esp_err_t ledc_channel(ledc_speedmode_t mode, ledc_timer_t timer, uint32_t duty);
        esp_err_t ledc_channel(ledc_speedmode_t mode, ledc_timer_t timer);
        esp_err_t ledc_channel(ledc_speedmode_t mode);
        ~ledc_channel();

        //esp_err_t channelConfig(const ledc_channel_config_t *ledc_conf);
        esp_err_t updateDuty();
        esp_err_t setPin(int gpio_num);
        esp_err_t stop(){
            return stop(0);
        };
        esp_err_t stop(uint32_t idle_level);
        //esp_err_t setFreq(uint32_t freq);
        //uint32_t getFreq(){
            return channel_config.freq_hz;
        };
        esp_err_t setDutyWithHpoint(uint32_t duty, uint32_t hpoint);
        esp_err_t setDuty(uint32_t duty);
        int getHpoint(){
            return channel_config.hpoint;
        };
        uint32_t getDuty(){
            return channel_config.duty;
        };
        esp_err_t bindChannelTimer(ledc_timer_t timer);
        /* omit fades for now...
        esp_err_t setFade(uint32_t duty, ledc_duty_direction_t fade_direction, uint32_t step_num, uint32_t duty_cycle_num, uint32_t duty_scale);
        esp_err_t setFadeWithStep(uint32_t target_duty, uint32_t scale, uint32_t cycle_num);
        esp_err_t setFadeWithTime(uint32_t target_duty, int max_fade_time_ms);
        esp_err_t fadeStart(ledc_fade_mode_t fade_mode)
        esp_err_t fadeFuncInstall(int intr_alloc_flags);
        esp_err_t fadeFuncUninstall();
        esp_err_t ledc_isr_register(void (*fn)(void *), void *arg, int intr_alloc_flags, ledc_isr_handle_t *handle );
        */
    private:
        ledc_channel_config_t channel_config;

};

#endif