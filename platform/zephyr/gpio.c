#include "platform/gpio.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#define NRF_GPIO_PORT0_LIMIT 32
#define NRF_GPIO_PIN_COUNT 48
#define PLATFORM_GPIO_IRQ_SLOTS 16

typedef struct {
    const struct device *port;
    gpio_pin_t pin;
} resolved_gpio_t;

typedef struct {
    struct gpio_callback callback;
    const struct device *port;
    gpio_pin_t pin;
    platform_gpio_irq_cb_t handler;
    void *user;
    bool used;
} gpio_irq_slot_t;

static gpio_irq_slot_t irq_slots[PLATFORM_GPIO_IRQ_SLOTS];

static bool resolve_gpio(platform_gpio_t pin, resolved_gpio_t *out)
{
    if (pin < 0)
        return false;

    if (pin < NRF_GPIO_PORT0_LIMIT) {
        const struct device *port0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
        if (!device_is_ready(port0))
            return false;
        out->port = port0;
        out->pin = (gpio_pin_t)pin;
        return true;
    }

#if DT_NODE_EXISTS(DT_NODELABEL(gpio1))
    const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (pin < NRF_GPIO_PIN_COUNT && device_is_ready(port1)) {
        out->port = port1;
        out->pin = (gpio_pin_t)(pin - NRF_GPIO_PORT0_LIMIT);
        return true;
    }
#endif

    return false;
}

bool platform_gpio_is_valid(platform_gpio_t pin)
{
    resolved_gpio_t gpio;
    return resolve_gpio(pin, &gpio);
}

int platform_gpio_input_pullup(platform_gpio_t pin)
{
    resolved_gpio_t gpio;
    if (!resolve_gpio(pin, &gpio))
        return -EINVAL;

    return gpio_pin_configure(gpio.port, gpio.pin, GPIO_INPUT | GPIO_PULL_UP);
}

int platform_gpio_input_pulldown(platform_gpio_t pin)
{
    resolved_gpio_t gpio;
    if (!resolve_gpio(pin, &gpio))
        return -EINVAL;

    return gpio_pin_configure(gpio.port, gpio.pin, GPIO_INPUT | GPIO_PULL_DOWN);
}

int platform_gpio_read(platform_gpio_t pin)
{
    resolved_gpio_t gpio;
    if (!resolve_gpio(pin, &gpio))
        return -EINVAL;

    return gpio_pin_get_raw(gpio.port, gpio.pin);
}

int platform_gpio_output(platform_gpio_t pin, bool value)
{
    resolved_gpio_t gpio;
    if (!resolve_gpio(pin, &gpio))
        return -EINVAL;

    return gpio_pin_configure(gpio.port, gpio.pin, value ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW);
}

int platform_gpio_write(platform_gpio_t pin, bool value)
{
    resolved_gpio_t gpio;
    if (!resolve_gpio(pin, &gpio))
        return -EINVAL;

    return gpio_pin_set_raw(gpio.port, gpio.pin, value ? 1 : 0);
}

static void gpio_irq_handler(const struct device *port, struct gpio_callback *callback,
                             gpio_port_pins_t pins)
{
    gpio_irq_slot_t *slot = CONTAINER_OF(callback, gpio_irq_slot_t, callback);

    if (slot->port != port || !(pins & BIT(slot->pin)))
        return;

    slot->handler((platform_gpio_t)slot->pin, slot->user);
}

int platform_gpio_irq_falling(platform_gpio_t pin, platform_gpio_irq_cb_t cb, void *user)
{
    if (!cb)
        return -EINVAL;

    resolved_gpio_t gpio;
    if (!resolve_gpio(pin, &gpio))
        return -EINVAL;

    gpio_irq_slot_t *slot = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(irq_slots); ++i) {
        if (!irq_slots[i].used) {
            slot = &irq_slots[i];
            slot->used = true;
            break;
        }
    }
    if (!slot)
        return -ENOMEM;

    slot->port = gpio.port;
    slot->pin = gpio.pin;
    slot->handler = cb;
    slot->user = user;

    int rc = gpio_pin_configure(gpio.port, gpio.pin, GPIO_INPUT | GPIO_PULL_UP);
    if (rc) {
        slot->used = false;
        return rc;
    }

    gpio_init_callback(&slot->callback, gpio_irq_handler, BIT(gpio.pin));
    rc = gpio_add_callback(gpio.port, &slot->callback);
    if (rc) {
        slot->used = false;
        return rc;
    }

    rc = gpio_pin_interrupt_configure(gpio.port, gpio.pin, GPIO_INT_EDGE_FALLING);
    if (rc) {
        gpio_remove_callback(gpio.port, &slot->callback);
        slot->used = false;
        return rc;
    }

    return 0;
}
