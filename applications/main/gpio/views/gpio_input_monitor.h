#pragma once

#include "../gpio_items.h"

#include <gui/view.h>

typedef struct GpioInputMonitor GpioInputMonitor;

GpioInputMonitor* gpio_input_monitor_alloc(GPIOItems* gpio_items);

void gpio_input_monitor_free(GpioInputMonitor* gpio_input_monitor);

View* gpio_input_monitor_get_view(GpioInputMonitor* gpio_input_monitor);

void gpio_input_monitor_update(GpioInputMonitor* gpio_input_monitor);

// Samples the pins into the model without redrawing (call on scene entry, before the view switch).
void gpio_input_monitor_sample(GpioInputMonitor* gpio_input_monitor);
