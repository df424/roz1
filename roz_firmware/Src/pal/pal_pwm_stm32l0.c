#include "pal_pwm.h"
#include "main.h"

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim22;

/*
 * Channel mapping:
 *   0 -> TIM2 CH1 (PA0)  neck yaw
 *   1 -> TIM2 CH2 (PA1)  eye horizontal
 *   2 -> TIM2 CH3 (PB0)  eye vertical
 *   3 -> TIM22 CH2 (PA7) jaw
 */

typedef struct {
    TIM_HandleTypeDef *htim;
    uint32_t channel;
} pwm_map_entry_t;

static const pwm_map_entry_t pwm_map[] = {
    { &htim2,  TIM_CHANNEL_1 },
    { &htim2,  TIM_CHANNEL_2 },
    { &htim2,  TIM_CHANNEL_3 },
    { &htim22, TIM_CHANNEL_2 },
};

#define PWM_CHANNEL_COUNT (sizeof(pwm_map) / sizeof(pwm_map[0]))

/* Timer tick period in microseconds.
 * TIM2:  32 MHz / (32+1) prescaler ≈ 969.7 kHz → ~1.031 µs/tick
 * TIM22: same prescaler → same tick rate
 * For CubeMX prescaler=32 → divides by 33, not 32.
 * Pulse conversion: pulse_us * 32000000 / 1000000 / 33 = pulse_us * 32/33
 * Close enough to 1:1 at this prescaler. We use integer math:
 *   ticks = pulse_us * 32 / 33
 */
#define PULSE_US_TO_TICKS(us) ((uint32_t)(us) * 32U / 33U)

void pal_pwm_init(void)
{
    /* Nothing extra needed — MX_TIMx_Init already configured the timers */
}

void pal_pwm_start(pwm_channel_id_t ch)
{
    if (ch >= PWM_CHANNEL_COUNT) return;
    HAL_TIM_PWM_Start(pwm_map[ch].htim, pwm_map[ch].channel);
}

void pal_pwm_stop(pwm_channel_id_t ch)
{
    if (ch >= PWM_CHANNEL_COUNT) return;
    HAL_TIM_PWM_Stop(pwm_map[ch].htim, pwm_map[ch].channel);
}

void pal_pwm_set_pulse(pwm_channel_id_t ch, uint16_t pulse_us)
{
    if (ch >= PWM_CHANNEL_COUNT) return;
    __HAL_TIM_SET_COMPARE(pwm_map[ch].htim, pwm_map[ch].channel,
                          PULSE_US_TO_TICKS(pulse_us));
}
