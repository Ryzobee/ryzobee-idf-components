/** @file yoke_motor.c */
#include "yoke_motor.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "bdc_motor.h"
#include "soc/soc_caps.h"

/* With valid arguments, a non-NULL error output is owned only for deletion retry. */
esp_err_t yoke_motor_new_mcpwm_device(const bdc_motor_config_t *, const bdc_motor_mcpwm_config_t *, bdc_motor_handle_t *);

#define TAG "yoke_motor"

#define YOKE_MOTOR_PWM_FREQ_HZ 20000U
#define YOKE_MOTOR_RESOLUTION_HZ 10000000U
#define YOKE_MOTOR_DEFAULT_SPEED_PERCENT 50U

struct yoke_motor {
    void *handle;
    rootmaker_port_t port;
    gpio_num_t pins[2];
    uint32_t period_ticks;
    bool initialized;
    bool stopping;
    bool disabled;
    bool coasted;
};

/* Keep failed initialization reachable until hardware cleanup can succeed. */
static yoke_motor_handle_t s_failed_init_motor;

static bdc_motor_handle_t yoke_motor_handle(const yoke_motor_handle_t motor) {
    return (bdc_motor_handle_t)motor->handle;
}

static esp_err_t yoke_motor_check(const yoke_motor_handle_t motor) {
    if (motor == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!motor->initialized || motor->stopping || motor->handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

yoke_motor_handle_t yoke_motor_init(rootmaker_port_t port) {
    if (s_failed_init_motor != NULL) {
        esp_err_t cleanup_ret = yoke_motor_deinit(s_failed_init_motor);
        if (cleanup_ret != ESP_OK) {
            ESP_LOGE(TAG, "previous initialization cleanup still failing: %s", esp_err_to_name(cleanup_ret));
            return NULL;
        }
        s_failed_init_motor = NULL;
    }
    gpio_num_t pwma_gpio_num;
    gpio_num_t pwmb_gpio_num;
    esp_err_t ret = rootmaker_get_port_pins(&port, &pwma_gpio_num, &pwmb_gpio_num);
    if (ret != ESP_OK || pwma_gpio_num == pwmb_gpio_num) {
        ESP_LOGE(TAG, "invalid port pins");
        return NULL;
    }

    yoke_motor_handle_t motor = calloc(1, sizeof(struct yoke_motor));
    if (motor == NULL) {
        ESP_LOGE(TAG, "no mem");
        return NULL;
    }
    motor->pins[0] = port.pins[0];
    motor->pins[1] = port.pins[1];
    motor->port = (rootmaker_port_t){ .type = port.type, .pin_num = 2, .pins = motor->pins };

    ret = rootmaker_use_port(&motor->port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "port in use: %s", esp_err_to_name(ret));
        free(motor);
        return NULL;
    }

    const bdc_motor_config_t motor_config = {
        .pwma_gpio_num = pwma_gpio_num,
        .pwmb_gpio_num = pwmb_gpio_num,
        .pwm_freq_hz = YOKE_MOTOR_PWM_FREQ_HZ,
    };
    bdc_motor_mcpwm_config_t mcpwm_config = {
        .group_id = 0,
        .resolution_hz = YOKE_MOTOR_RESOLUTION_HZ,
    };

    bdc_motor_handle_t handle = NULL;
    for (int group = 0; group < SOC_MCPWM_GROUPS; group++) {
        mcpwm_config.group_id = group;
        ret = yoke_motor_new_mcpwm_device(&motor_config, &mcpwm_config, &handle);
        if (ret != ESP_ERR_NOT_FOUND || handle != NULL)
            break;
    }
    motor->handle = handle;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "new mcpwm device failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    motor->period_ticks = YOKE_MOTOR_RESOLUTION_HZ / YOKE_MOTOR_PWM_FREQ_HZ;
    motor->initialized = true;
    ret = bdc_motor_enable(handle);
    if (ret == ESP_OK)
        ret = yoke_motor_set_speed_percent(motor, YOKE_MOTOR_DEFAULT_SPEED_PERCENT);
    if (ret == ESP_OK)
        ret = bdc_motor_coast(handle);
    if (ret == ESP_OK)
        return motor;

fail:
    ret = yoke_motor_deinit(motor);
    if (ret != ESP_OK) {
        s_failed_init_motor = motor;
        ESP_LOGE(TAG, "initialization cleanup failed: %s; next init will retry cleanup", esp_err_to_name(ret));
    }
    return NULL;
}

esp_err_t yoke_motor_deinit(yoke_motor_handle_t motor) {
    if (motor == NULL)
        return ESP_ERR_INVALID_ARG;
    motor->stopping = true;
    esp_err_t ret;
    if (motor->handle != NULL) {
        bdc_motor_handle_t handle = yoke_motor_handle(motor);
        if (motor->initialized && !motor->coasted) {
            ret = bdc_motor_coast(handle);
            if (ret != ESP_OK)
                return ret;
            motor->coasted = true;
        }
        if (motor->initialized && !motor->disabled) {
            ret = bdc_motor_disable(handle);
            if (ret != ESP_OK)
                return ret;
            motor->disabled = true;
        }
        ret = bdc_motor_del(handle);
        if (ret != ESP_OK)
            return ret;
        motor->handle = NULL;
    }
    ret = rootmaker_release_port(&motor->port);
    if (ret != ESP_OK)
        return ret;
    free(motor);
    return ESP_OK;
}

esp_err_t yoke_motor_set_speed_percent(yoke_motor_handle_t motor, uint8_t percent) {
    esp_err_t ret = yoke_motor_check(motor);
    if (ret != ESP_OK)
        return ret;
    if (percent > 100U)
        return ESP_ERR_INVALID_ARG;
    uint32_t compare_ticks = (motor->period_ticks * percent) / 100U;
    return bdc_motor_set_speed(yoke_motor_handle(motor), compare_ticks);
}

esp_err_t yoke_motor_forward(yoke_motor_handle_t motor) {
    esp_err_t ret = yoke_motor_check(motor);
    return ret == ESP_OK ? bdc_motor_forward(yoke_motor_handle(motor)) : ret;
}

esp_err_t yoke_motor_reverse(yoke_motor_handle_t motor) {
    esp_err_t ret = yoke_motor_check(motor);
    return ret == ESP_OK ? bdc_motor_reverse(yoke_motor_handle(motor)) : ret;
}

esp_err_t yoke_motor_coast(yoke_motor_handle_t motor) {
    esp_err_t ret = yoke_motor_check(motor);
    return ret == ESP_OK ? bdc_motor_coast(yoke_motor_handle(motor)) : ret;
}

esp_err_t yoke_motor_brake(yoke_motor_handle_t motor) {
    esp_err_t ret = yoke_motor_check(motor);
    return ret == ESP_OK ? bdc_motor_brake(yoke_motor_handle(motor)) : ret;
}
