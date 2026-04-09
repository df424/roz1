#include "servo_driver.h"
#include "pal_pwm.h"

static void servo_init(const actuator_config_t *cfg)
{
    (void)cfg;
    /* PWM hardware already initialized by pal_pwm_init / CubeMX */
}

static void servo_set_position(const actuator_config_t *cfg, float position)
{
    const servo_config_t *sc = cfg->driver_config;
    float range = cfg->max_position - cfg->min_position;
    float normalized = (range > 0.0f) ? (position - cfg->min_position) / range
                                      : 0.0f;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;

    uint16_t pulse = sc->min_pulse_us
                   + (uint16_t)(normalized * (float)(sc->max_pulse_us - sc->min_pulse_us));
    pal_pwm_set_pulse(sc->pwm_channel, pulse);
}

static void servo_enable(const actuator_config_t *cfg)
{
    const servo_config_t *sc = cfg->driver_config;
    pal_pwm_start(sc->pwm_channel);
}

static void servo_disable(const actuator_config_t *cfg)
{
    const servo_config_t *sc = cfg->driver_config;
    pal_pwm_stop(sc->pwm_channel);
}

const actuator_driver_t servo_driver = {
    .init         = servo_init,
    .set_position = servo_set_position,
    .enable       = servo_enable,
    .disable      = servo_disable,
};
