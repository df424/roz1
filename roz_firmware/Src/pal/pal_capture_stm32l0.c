#include "pal_capture.h"
#include "main.h"

extern TIM_HandleTypeDef htim21;

static volatile uint32_t capture_value;
static volatile uint8_t  capture_seq;
static volatile bool     capture_pending;

void pal_capture_init(void)
{
    capture_value = 0;
    capture_seq = 0;
    capture_pending = false;

    /* Start TIM21 base counter and enable input capture interrupt on CH1 */
    HAL_TIM_Base_Start(&htim21);
    HAL_TIM_IC_Start_IT(&htim21, TIM_CHANNEL_1);
}

bool pal_capture_ready(void)
{
    return capture_pending;
}

uint32_t pal_capture_read_us(uint8_t *seq_out)
{
    /* At prescaler=31, each tick is 1 µs, so raw value is already µs */
    uint32_t val = capture_value;
    if (seq_out)
        *seq_out = capture_seq;
    capture_pending = false;
    return val;
}

/* Called by HAL from TIM21_IRQHandler on input capture event */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM21) {
        capture_value = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        capture_seq++;
        capture_pending = true;
    }
}
