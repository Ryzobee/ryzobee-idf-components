/** @file rootmaker_utils.c */
#include "rootmaker_utils.h"
#include "rootmaker_pin.h"

#include <string.h>

typedef struct {
    i2c_master_bus_handle_t bus;
    gpio_num_t sda;
    gpio_num_t scl;
    uint16_t ref_count;
    bool in_use;
} rootmaker_i2c_bus_t;

static rootmaker_i2c_bus_t s_i2c_buses[I2C_NUM_MAX];

/* --- port usage tracking --- */

#define ROOTMAKER_MAX_USED_PORTS 16

typedef struct {
    rootmaker_port_t port;
    bool used;
} rootmaker_used_port_t;

static rootmaker_used_port_t s_used_ports[ROOTMAKER_MAX_USED_PORTS];

static bool rootmaker_port_has_pin(const rootmaker_port_t *port, gpio_num_t pin) {
    if (port == NULL || port->pins == NULL || pin == GPIO_NUM_NC) {
        return false;
    }
    for (uint8_t i = 0; i < port->pin_num; i++) {
        if (port->pins[i] == pin) {
            return true;
        }
    }
    return false;
}

static bool rootmaker_ports_overlap(const rootmaker_port_t *a, const rootmaker_port_t *b) {
    if (a == NULL || b == NULL || a->pins == NULL || b->pins == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < a->pin_num; i++) {
        if (rootmaker_port_has_pin(b, a->pins[i])) {
            return true;
        }
    }
    return false;
}

const rootmaker_port_t rootmaker_rs_port1 = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = (gpio_num_t[]){ PORT1_PIN1, PORT1_PIN2 },
};

const rootmaker_port_t rootmaker_rs_port2 = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = (gpio_num_t[]){ PORT2_PIN1, PORT2_PIN2 },
};

const rootmaker_port_t rootmaker_rs_port3 = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = (gpio_num_t[]){ PORT3_PIN1, PORT3_PIN2 },
};

const rootmaker_port_t rootmaker_rs_port4 = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = (gpio_num_t[]){ PORT4_PIN1, PORT4_PIN2 },
};

const rootmaker_port_t rootmaker_s_bus = {
    .type = ROOTMAKER_PORT_TYPE_S_BUS,
    .pin_num = 14,
    .pins = (gpio_num_t[]){ S_BUS1, S_BUS2, S_BUS3, S_BUS4, S_BUS5, S_BUS6, S_BUS7, S_BUS8, S_BUS9, S_BUS10, S_BUS11, S_BUS12, S_BUS13, S_BUS14 },
};

const rootmaker_port_t rootmaker_f_bus = {
    .type = ROOTMAKER_PORT_TYPE_F_BUS,
    .pin_num = 16,
    .pins = (gpio_num_t[]){ F_BUS9, F_BUS10, F_BUS11, F_BUS12, F_BUS13, F_BUS14, F_BUS15, F_BUS16, F_BUS17, F_BUS18, F_BUS19, F_BUS20, F_BUS21, F_BUS22,
                           F_BUS23, F_BUS24 },
};

const rootmaker_port_t rootmaker_sys_i2c_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = (gpio_num_t[]){ BOARD_SDA, BOARD_SCL },
};

esp_err_t rootmaker_get_port_pins(const rootmaker_port_t *port, gpio_num_t *pin_a, gpio_num_t *pin_b) {
    if (port == NULL || pin_a == NULL || pin_b == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (port->pin_num < 2 || port->pins == NULL || !GPIO_IS_VALID_GPIO(port->pins[0]) || !GPIO_IS_VALID_GPIO(port->pins[1])) {
        return ESP_ERR_INVALID_ARG;
    }
    *pin_a = port->pins[0];
    *pin_b = port->pins[1];
    return ESP_OK;
}

esp_err_t rootmaker_get_port_pin(const rootmaker_port_t *port, uint8_t index, gpio_num_t *pin) {
    if (port == NULL || pin == NULL || port->pins == NULL || index >= port->pin_num) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!GPIO_IS_VALID_GPIO(port->pins[index])) {
        return ESP_ERR_INVALID_ARG;
    }
    *pin = port->pins[index];
    return ESP_OK;
}

esp_err_t rootmaker_use_port(const rootmaker_port_t *port) {
    if (port == NULL || port->pins == NULL || port->pin_num == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < port->pin_num; i++) {
        if (!GPIO_IS_VALID_GPIO(port->pins[i]))
            return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < ROOTMAKER_MAX_USED_PORTS; i++) {
        if (s_used_ports[i].used && rootmaker_ports_overlap(&s_used_ports[i].port, port)) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    for (int i = 0; i < I2C_NUM_MAX; i++) {
        if (s_i2c_buses[i].in_use && (rootmaker_port_has_pin(port, s_i2c_buses[i].sda) || rootmaker_port_has_pin(port, s_i2c_buses[i].scl))) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    for (int i = 0; i < ROOTMAKER_MAX_USED_PORTS; i++) {
        if (!s_used_ports[i].used) {
            s_used_ports[i].port = *port;
            s_used_ports[i].used = true;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t rootmaker_release_port(const rootmaker_port_t *port) {
    if (port == NULL || port->pins == NULL || port->pin_num == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < ROOTMAKER_MAX_USED_PORTS; i++) {
        if (s_used_ports[i].used && s_used_ports[i].port.type == port->type && s_used_ports[i].port.pin_num == port->pin_num &&
            !memcmp(s_used_ports[i].port.pins, port->pins, port->pin_num * sizeof(port->pins[0]))) {
            s_used_ports[i].used = false;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

bool rootmaker_port_is_used(const rootmaker_port_t *port) {
    if (port == NULL || port->pins == NULL || port->pin_num == 0) {
        return false;
    }
    for (int i = 0; i < ROOTMAKER_MAX_USED_PORTS; i++) {
        if (s_used_ports[i].used && rootmaker_ports_overlap(&s_used_ports[i].port, port)) {
            return true;
        }
    }
    for (int i = 0; i < I2C_NUM_MAX; i++) {
        if (s_i2c_buses[i].in_use && (rootmaker_port_has_pin(port, s_i2c_buses[i].sda) || rootmaker_port_has_pin(port, s_i2c_buses[i].scl))) {
            return true;
        }
    }
    return false;
}

esp_err_t rootmaker_new_i2c_bus(const rootmaker_port_t *port, i2c_master_bus_handle_t *out_bus) {
    if (port == NULL || out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_bus = NULL;

    gpio_num_t sda;
    gpio_num_t scl;
    esp_err_t ret = rootmaker_get_port_pins(port, &sda, &scl);
    if (ret != ESP_OK) {
        return ret;
    }
    if (sda == scl) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < ROOTMAKER_MAX_USED_PORTS; i++) {
        if (s_used_ports[i].used && (rootmaker_port_has_pin(&s_used_ports[i].port, sda) || rootmaker_port_has_pin(&s_used_ports[i].port, scl))) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    /* Always reuse a matching bus before considering an empty controller slot. */
    for (int i2c_num = 0; i2c_num < I2C_NUM_MAX; i2c_num++) {
        rootmaker_i2c_bus_t *entry = &s_i2c_buses[i2c_num];
        if (entry->in_use && entry->sda == sda && entry->scl == scl) {
            if (entry->ref_count == UINT16_MAX) {
                return ESP_ERR_NO_MEM;
            }
            entry->ref_count++;
            *out_bus = entry->bus;
            return ESP_OK;
        }
    }

    /* Different buses must not drive any of the same signal pins, including
     * SDA/SCL swaps. Only an exact, ordered pair can share a controller. */
    for (int i = 0; i < I2C_NUM_MAX; i++) {
        rootmaker_i2c_bus_t *entry = &s_i2c_buses[i];
        if (entry->in_use && (entry->sda == sda || entry->sda == scl || entry->scl == sda || entry->scl == scl)) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    for (int i2c_num = 0; i2c_num < I2C_NUM_MAX; i2c_num++) {
        rootmaker_i2c_bus_t *entry = &s_i2c_buses[i2c_num];
        if (entry->in_use) {
            continue;
        }
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = i2c_num,
            .sda_io_num = sda,
            .scl_io_num = scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ret = i2c_new_master_bus(&bus_config, &entry->bus);
        if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_NOT_FOUND) {
            entry->bus = NULL;
            continue;
        }
        if (ret != ESP_OK) {
            entry->bus = NULL;
            return ret;
        }
        entry->sda = sda;
        entry->scl = scl;
        entry->ref_count = 1;
        entry->in_use = true;
        *out_bus = entry->bus;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t rootmaker_del_i2c_bus(const rootmaker_port_t *port) {
    if (port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_num_t sda;
    gpio_num_t scl;
    esp_err_t ret = rootmaker_get_port_pins(port, &sda, &scl);
    if (ret != ESP_OK) {
        return ret;
    }

    for (int i2c_num = 0; i2c_num < I2C_NUM_MAX; i2c_num++) {
        rootmaker_i2c_bus_t *entry = &s_i2c_buses[i2c_num];
        if (entry->in_use && entry->sda == sda && entry->scl == scl) {
            if (entry->ref_count > 1) {
                entry->ref_count--;
                return ESP_OK;
            }
            ret = i2c_del_master_bus(entry->bus);
            if (ret != ESP_OK) {
                return ret;
            }
            memset(entry, 0, sizeof(*entry));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

#define ROOTMAKER_MAX_I2C_DEVICES 32
static struct {
    i2c_master_bus_handle_t bus;
    uint8_t address;
} s_i2c_devices[ROOTMAKER_MAX_I2C_DEVICES];

esp_err_t rootmaker_claim_i2c_address(i2c_master_bus_handle_t bus, uint8_t address) {
    if (bus == NULL || address < 0x08 || address > 0x77)
        return ESP_ERR_INVALID_ARG;
    int free_slot = -1;
    for (int i = 0; i < ROOTMAKER_MAX_I2C_DEVICES; i++) {
        if (s_i2c_devices[i].bus == bus && s_i2c_devices[i].address == address)
            return ESP_ERR_INVALID_STATE;
        if (s_i2c_devices[i].bus == NULL && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return ESP_ERR_NO_MEM;
    s_i2c_devices[free_slot].bus = bus;
    s_i2c_devices[free_slot].address = address;
    return ESP_OK;
}

esp_err_t rootmaker_release_i2c_address(i2c_master_bus_handle_t bus, uint8_t address) {
    if (bus == NULL || address < 0x08 || address > 0x77)
        return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < ROOTMAKER_MAX_I2C_DEVICES; i++) {
        if (s_i2c_devices[i].bus == bus && s_i2c_devices[i].address == address) {
            s_i2c_devices[i].bus = NULL;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
