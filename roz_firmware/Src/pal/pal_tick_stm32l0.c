#include "pal_tick.h"
#include "main.h"

uint32_t pal_tick_ms(void)
{
    return HAL_GetTick();
}

uint32_t pal_tick_us(void)
{
    /*
     * Combine SysTick counter with HAL_GetTick() for microsecond resolution.
     * SysTick counts down from (SystemCoreClock/1000 - 1) to 0 each ms.
     * At 32 MHz, that's 32000 ticks per ms.
     */
    uint32_t ms;
    uint32_t ticks;

    /* Read atomically: if ms changes between reads, retry */
    do {
        ms = HAL_GetTick();
        ticks = SysTick->VAL;
    } while (ms != HAL_GetTick());

    uint32_t us_in_ms = (SysTick->LOAD + 1 - ticks) / (SystemCoreClock / 1000000U);
    return ms * 1000U + us_in_ms;
}
