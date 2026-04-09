#include "system_manager.h"
#include "actuator_manager.h"
#include "stream_manager.h"
#include "pal_gpio.h"
#include "pal_tick.h"

#define DISCONNECT_TIMEOUT_MS 5000
#define LED_HEARTBEAT_MS      2000
#define LED_FAST_BLINK_MS     100
#define LED_SLOW_BLINK_MS     500
#define LED_RAPID_FLASH_MS    50

typedef struct {
    system_state_t state;
    uint32_t       boot_time_ms;
    uint32_t       last_rx_time_ms;
    uint32_t       led_toggle_ms;
    uint8_t        led_phase;
} system_context_t;

static system_context_t ctx;

void system_init(void)
{
    ctx.state = SYS_POST;
    ctx.boot_time_ms = pal_tick_ms();
    ctx.last_rx_time_ms = ctx.boot_time_ms;
    ctx.led_toggle_ms = 0;
    ctx.led_phase = 0;
    pal_led_set(true);
}

system_state_t system_get_state(void)
{
    return ctx.state;
}

void system_on_frame_received(void)
{
    ctx.last_rx_time_ms = pal_tick_ms();

    if (ctx.state == SYS_DISCONNECTED)
        ctx.state = SYS_HANDSHAKE;
}

void system_on_emergency_stop(void)
{
    actuator_emergency_stop();
    stream_close_all();
    ctx.state = SYS_ESTOP;
}

void system_on_clear_emergency_stop(void)
{
    actuator_clear_emergency_stop();
    ctx.state = SYS_RUNNING;
}

/* Advance state machine based on external transitions */
void system_set_state(system_state_t new_state)
{
    ctx.state = new_state;
}

static void led_update(uint32_t now_ms)
{
    uint32_t interval;

    switch (ctx.state) {
    case SYS_BOOT:
        pal_led_set(true);
        return;
    case SYS_POST:
        interval = LED_FAST_BLINK_MS;
        break;
    case SYS_HANDSHAKE:
        interval = LED_SLOW_BLINK_MS;
        break;
    case SYS_RUNNING:
        /* Heartbeat: short flash every 2s */
        if (ctx.led_phase == 0) {
            if ((now_ms - ctx.led_toggle_ms) >= LED_HEARTBEAT_MS) {
                pal_led_set(true);
                ctx.led_toggle_ms = now_ms;
                ctx.led_phase = 1;
            }
        } else {
            if ((now_ms - ctx.led_toggle_ms) >= 50) {
                pal_led_set(false);
                ctx.led_phase = 0;
            }
        }
        return;
    case SYS_ESTOP:
        interval = LED_RAPID_FLASH_MS;
        break;
    case SYS_DISCONNECTED:
        /* Double blink pattern */
        interval = 150;
        break;
    default:
        return;
    }

    if ((now_ms - ctx.led_toggle_ms) >= interval) {
        pal_led_toggle();
        ctx.led_toggle_ms = now_ms;
    }
}

void system_tick(uint32_t now_ms)
{
    /* Disconnect detection (only while RUNNING) */
    if (ctx.state == SYS_RUNNING) {
        if ((now_ms - ctx.last_rx_time_ms) > DISCONNECT_TIMEOUT_MS) {
            ctx.state = SYS_DISCONNECTED;
            /* Execute disconnect procedure: return to defaults */
            actuator_homing_start();
        }
    }

    led_update(now_ms);
}
