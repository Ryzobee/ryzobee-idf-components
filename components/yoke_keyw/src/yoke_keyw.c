/** @file yoke_keyw.c */
#include "yoke_keyw.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "button_interface.h"
#include "iot_button.h"

#define TAG "yoke_keyw"

#define YOKE_KEYW_BUTTON_ACTIVE_LEVEL 1
#define YOKE_KEYW_LED_INITIAL_ON false
#define YOKE_KEYW_LONG_PRESS_TIME_MS 1500

struct yoke_keyw {
    void *button_handle;
    button_driver_t button_driver;
    gpio_num_t pins[2];
    atomic_bool stopping;
    bool button_reset;
    atomic_bool in_callback;
    yoke_keyw_event_cb_t event_callbacks[BUTTON_EVENT_MAX];
    void *event_user_data[BUTTON_EVENT_MAX];
    rootmaker_port_t port;
    gpio_num_t button_gpio_num;
    gpio_num_t led_gpio_num;
    atomic_uint_least16_t long_press_time_ms;
    atomic_bool initialized;
};

/* Lifecycle calls are serialized by the application. */
static yoke_keyw_handle_t s_failed_init;

static void yoke_keyw_button_callback(void *button_handle, void *user_data) {
    (void)button_handle;
    yoke_keyw_handle_t keyw = user_data;
    if (keyw == NULL || keyw->stopping || !keyw->initialized)
        return;
    button_event_t event = iot_button_get_event((button_handle_t)keyw->button_handle);
    bool double_click_enabled = keyw->event_callbacks[BUTTON_DOUBLE_CLICK] != NULL;

    if (event == BUTTON_PRESS_UP && !double_click_enabled && keyw->event_callbacks[BUTTON_SINGLE_CLICK] != NULL &&
        iot_button_get_pressed_time((button_handle_t)keyw->button_handle) < keyw->long_press_time_ms) {
        /* Without a double-click subscriber there is no reason to wait for
         * Espressif's multi-click window. Dispatch one single click on every
         * short release, so two quick presses naturally produce two events. */
        keyw->in_callback = true;
        keyw->event_callbacks[BUTTON_SINGLE_CLICK](BUTTON_SINGLE_CLICK, keyw->event_user_data[BUTTON_SINGLE_CLICK]);
        keyw->in_callback = false;
    }

    if ((event == BUTTON_SINGLE_CLICK || event == BUTTON_DOUBLE_CLICK) && !double_click_enabled) {
        return;
    }
    if (event < BUTTON_EVENT_MAX && keyw->event_callbacks[event] != NULL) {
        keyw->in_callback = true;
        keyw->event_callbacks[event](event, keyw->event_user_data[event]);
        keyw->in_callback = false;
    }
}

static uint8_t keyw_get_level(button_driver_t *driver) {
    yoke_keyw_handle_t keyw = (void *)((char *)driver - offsetof(struct yoke_keyw, button_driver));
    return !keyw->stopping && gpio_get_level(keyw->button_gpio_num) == YOKE_KEYW_BUTTON_ACTIVE_LEVEL;
}

static esp_err_t keyw_delete_driver(button_driver_t *driver) {
    (void)driver;
    /* GPIO cleanup is performed by the owner before deleting the button. */
    return ESP_OK;
}

typedef struct {
    esp_err_t (*operation)(void *);
    void *arg;
    SemaphoreHandle_t done;
    esp_err_t result;
} keyw_timer_request_t;

static void keyw_operation_on_timer_task(void *arg) {
    keyw_timer_request_t *request = arg;
    request->result = request->operation(request->arg);
    xSemaphoreGive(request->done);
}

static esp_err_t keyw_run_on_timer_task(esp_err_t (*operation)(void *), void *arg) {
    /* Serialize mutations with scans, including callback arrays and user_data.
     * The waiting caller keeps the stack request alive until completion. */
    keyw_timer_request_t request = { .operation = operation, .arg = arg, .done = xSemaphoreCreateBinary() };
    if (request.done == NULL)
        return ESP_ERR_NO_MEM;
    esp_timer_handle_t timer = NULL;
    esp_timer_create_args_t args = { .callback = keyw_operation_on_timer_task, .arg = &request, .dispatch_method = ESP_TIMER_TASK, .name = "keyw_update" };
    esp_err_t ret = esp_timer_create(&args, &timer);
    if (ret == ESP_OK)
        ret = esp_timer_start_once(timer, 1);
    if (ret == ESP_OK) {
        xSemaphoreTake(request.done, portMAX_DELAY);
        ret = request.result;
    }
    if (timer != NULL)
        (void)esp_timer_delete(timer);
    vSemaphoreDelete(request.done);
    return ret;
}

static esp_err_t keyw_delete_button_on_timer_task(void *arg) {
    return iot_button_delete((button_handle_t)arg);
}

static esp_err_t keyw_delete_button(button_handle_t button) {
    return keyw_run_on_timer_task(keyw_delete_button_on_timer_task, button);
}

static esp_err_t keyw_create_button_on_timer_task(void *arg) {
    yoke_keyw_handle_t keyw = arg;
    const button_config_t button_config = {
        .long_press_time = keyw->long_press_time_ms,
        .short_press_time = 0,
    };
    const gpio_config_t button_gpio_config = {
        .pin_bit_mask = 1ULL << keyw->button_gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&button_gpio_config);
    if (ret != ESP_OK)
        return ret;
    keyw->button_driver = (button_driver_t){ .get_key_level = keyw_get_level, .del = keyw_delete_driver };
    button_handle_t button;
    ret = iot_button_create(&button_config, &keyw->button_driver, &button);
    if (ret != ESP_OK) {
        return ret;
    }
    keyw->button_handle = button;

    /* PRESS_UP provides immediate single-click dispatch when double-click is
     * unused. SINGLE/DOUBLE provide delayed classification when it is used. */
    ret = iot_button_register_cb(button, BUTTON_PRESS_UP, NULL, yoke_keyw_button_callback, keyw);
    if (ret == ESP_OK) {
        ret = iot_button_register_cb(button, BUTTON_SINGLE_CLICK, NULL, yoke_keyw_button_callback, keyw);
    }
    if (ret == ESP_OK) {
        ret = iot_button_register_cb(button, BUTTON_DOUBLE_CLICK, NULL, yoke_keyw_button_callback, keyw);
    }
    if (ret != ESP_OK) {
        if (iot_button_delete(button) == ESP_OK)
            keyw->button_handle = NULL;
        return ret;
    }

    for (int event = 0; event < BUTTON_EVENT_MAX; event++) {
        if (event == BUTTON_PRESS_UP || event == BUTTON_SINGLE_CLICK || event == BUTTON_DOUBLE_CLICK) {
            continue;
        }
        if (keyw->event_callbacks[event] != NULL) {
            ret = iot_button_register_cb(button, (button_event_t)event, NULL, yoke_keyw_button_callback, keyw);
            if (ret != ESP_OK) {
                if (iot_button_delete(button) == ESP_OK)
                    keyw->button_handle = NULL;
                return ret;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t keyw_create_button(yoke_keyw_handle_t keyw) {
    return keyw_run_on_timer_task(keyw_create_button_on_timer_task, keyw);
}

yoke_keyw_handle_t yoke_keyw_init(rootmaker_port_t port) {
    if (s_failed_init != NULL) {
        esp_err_t ret = yoke_keyw_deinit(s_failed_init);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "previous initialization cleanup still failing: %s", esp_err_to_name(ret));
            return NULL;
        }
        s_failed_init = NULL;
    }
    gpio_num_t button_gpio_num;
    gpio_num_t led_gpio_num;
    esp_err_t ret = rootmaker_get_port_pins(&port, &button_gpio_num, &led_gpio_num);
    if (ret != ESP_OK || button_gpio_num == led_gpio_num) {
        ESP_LOGE(TAG, "invalid port pins");
        return NULL;
    }

    yoke_keyw_handle_t keyw = calloc(1, sizeof(struct yoke_keyw));
    if (keyw == NULL) {
        ESP_LOGE(TAG, "no mem");
        return NULL;
    }
    atomic_init(&keyw->stopping, false);
    atomic_init(&keyw->in_callback, false);
    atomic_init(&keyw->initialized, false);
    atomic_init(&keyw->long_press_time_ms, YOKE_KEYW_LONG_PRESS_TIME_MS);
    keyw->pins[0] = button_gpio_num;
    keyw->pins[1] = led_gpio_num;
    keyw->port = (rootmaker_port_t){ .type = port.type, .pin_num = 2, .pins = keyw->pins };
    keyw->button_gpio_num = button_gpio_num;
    keyw->led_gpio_num = led_gpio_num;
    ret = rootmaker_use_port(&keyw->port);
    if (ret != ESP_OK) {
        free(keyw);
        return NULL;
    }

    const gpio_config_t led_gpio_config = {
        .pin_bit_mask = 1ULL << keyw->led_gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&led_gpio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led gpio config failed: %s", esp_err_to_name(ret));
        (void)rootmaker_release_port(&keyw->port);
        free(keyw);
        return NULL;
    }
    ret = gpio_set_level(keyw->led_gpio_num, YOKE_KEYW_LED_INITIAL_ON ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led level failed: %s", esp_err_to_name(ret));
        (void)rootmaker_release_port(&keyw->port);
        free(keyw);
        return NULL;
    }

    ret = keyw_create_button(keyw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(ret));
        ret = yoke_keyw_deinit(keyw);
        if (ret != ESP_OK) {
            s_failed_init = keyw;
            ESP_LOGE(TAG, "initialization cleanup failed: %s; next init will retry cleanup", esp_err_to_name(ret));
        }
        return NULL;
    }
    keyw->initialized = true;
    return keyw;
}

esp_err_t yoke_keyw_set_long_press_time(yoke_keyw_handle_t keyw, uint16_t time_ms) {
    if (keyw == NULL || time_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (keyw->stopping || keyw->in_callback)
        return ESP_ERR_INVALID_STATE;
    if (keyw->initialized) {
        const uint16_t old_time_ms = keyw->long_press_time_ms;
        keyw->long_press_time_ms = time_ms;
        esp_err_t delete_ret = keyw_delete_button((button_handle_t)keyw->button_handle);
        if (delete_ret != ESP_OK) {
            keyw->long_press_time_ms = old_time_ms;
            return delete_ret;
        }
        keyw->button_handle = NULL;
        keyw->initialized = false;
        esp_err_t ret = keyw_create_button(keyw);
        if (ret != ESP_OK) {
            keyw->long_press_time_ms = old_time_ms;
            if (keyw_create_button(keyw) == ESP_OK) {
                keyw->initialized = true;
            }
            ESP_LOGE(TAG, "recreate button failed: %s", esp_err_to_name(ret));
            return ret;
        }
        keyw->initialized = true;
    } else {
        keyw->long_press_time_ms = time_ms;
    }
    return ESP_OK;
}

esp_err_t yoke_keyw_set_led(yoke_keyw_handle_t keyw, bool on) {
    if (keyw == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!keyw->initialized || keyw->stopping)
        return ESP_ERR_INVALID_STATE;
    return gpio_set_level(keyw->led_gpio_num, on ? 1 : 0);
}

typedef struct {
    yoke_keyw_handle_t keyw;
    button_event_t event;
    yoke_keyw_event_cb_t callback;
    void *user_data;
    bool remove;
} keyw_callback_request_t;

static esp_err_t keyw_update_callback_on_timer_task(void *arg) {
    keyw_callback_request_t *request = arg;
    yoke_keyw_handle_t keyw = request->keyw;
    button_event_t event = request->event;
    if (request->remove ? keyw->event_callbacks[event] == NULL : keyw->event_callbacks[event] != NULL)
        return request->remove ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_STATE;

    esp_err_t ret = ESP_OK;
    if (event != BUTTON_PRESS_UP && event != BUTTON_SINGLE_CLICK && event != BUTTON_DOUBLE_CLICK) {
        if (request->remove)
            ret = iot_button_unregister_cb((button_handle_t)keyw->button_handle, event, NULL);
        else
            ret = iot_button_register_cb((button_handle_t)keyw->button_handle, event, NULL, yoke_keyw_button_callback, keyw);
    }
    if (ret == ESP_OK) {
        keyw->event_callbacks[event] = request->remove ? NULL : request->callback;
        keyw->event_user_data[event] = request->remove ? NULL : request->user_data;
    }
    return ret;
}

esp_err_t yoke_keyw_register_event_callback(yoke_keyw_handle_t keyw, button_event_t event, yoke_keyw_event_cb_t callback, void *user_data) {
    if (keyw == NULL || callback == NULL || (unsigned)event >= BUTTON_EVENT_MAX)
        return ESP_ERR_INVALID_ARG;
    if (!keyw->initialized || keyw->stopping || keyw->in_callback)
        return ESP_ERR_INVALID_STATE;
    if (event == BUTTON_MULTIPLE_CLICK)
        return ESP_ERR_NOT_SUPPORTED;
    keyw_callback_request_t request = { .keyw = keyw, .event = event, .callback = callback, .user_data = user_data };
    return keyw_run_on_timer_task(keyw_update_callback_on_timer_task, &request);
}

esp_err_t yoke_keyw_unregister_event_callback(yoke_keyw_handle_t keyw, button_event_t event) {
    if (keyw == NULL || (unsigned)event >= BUTTON_EVENT_MAX)
        return ESP_ERR_INVALID_ARG;
    if (!keyw->initialized || keyw->stopping || keyw->in_callback)
        return ESP_ERR_INVALID_STATE;
    if (event == BUTTON_MULTIPLE_CLICK)
        return ESP_ERR_NOT_SUPPORTED;
    keyw_callback_request_t request = { .keyw = keyw, .event = event, .remove = true };
    return keyw_run_on_timer_task(keyw_update_callback_on_timer_task, &request);
}

esp_err_t yoke_keyw_deinit(yoke_keyw_handle_t keyw) {
    if (keyw == NULL)
        return ESP_ERR_INVALID_ARG;
    if (keyw->in_callback)
        return ESP_ERR_INVALID_STATE;
    keyw->stopping = true;
    keyw->initialized = false;
    esp_err_t ret = gpio_set_level(keyw->led_gpio_num, 0);
    if (ret != ESP_OK)
        return ret;
    if (!keyw->button_reset) {
        ret = gpio_reset_pin(keyw->button_gpio_num);
        if (ret != ESP_OK)
            return ret;
        keyw->button_reset = true;
    }
    if (keyw->button_handle != NULL) {
        ret = keyw_delete_button((button_handle_t)keyw->button_handle);
        if (ret != ESP_OK)
            return ret;
        keyw->button_handle = NULL;
    }
    ret = rootmaker_release_port(&keyw->port);
    if (ret != ESP_OK)
        return ret;
    free(keyw);
    return ESP_OK;
}
