# zh_ac_dimmer - AC Dimmer Component for ESP-IDF

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Installation](#installation)
- [API Reference](#api-reference)
- [Usage Examples](#usage-examples)
- [Technical Specifications](#technical-specifications)
- [Error Codes](#error-codes)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

`zh_ac_dimmer` is a lightweight ESP-IDF component for AC dimmer control. It provides a simple API to control AC load power using zero-crossing detection and TRIAC control. The component is designed specifically for ESP32 microcontrollers and uses ESP-IDF v5.0+ drivers (GPTimer, GPIO).

The component supports AC frequency of 400 Hz and uses hardware timers for precise timing control.

---

## Features

1. **AC Power Control**: Controls AC load power from 0% to 100%
2. **Zero-Crossing Detection**: Uses GPIO interrupt for precise timing
3. **TRIAC Control**: Controls TRIAC gate with precise timing via GPTimer
4. **Hardware Timer**: Uses GPTimer with 1 MHz resolution (1 µs)
5. **IRAM-safe ISR and Callback**: Interrupts and callbacks are placed in IRAM for reliable operation
6. **Error Handling**: Comprehensive error checking and configuration validation
7. **Thread-Safe**: Uses ESP-IDF drivers (thread-safe)
8. **Minimal Memory Footprint**: Low memory overhead
9. **Simple API**: Lightweight interface with start/stop/set/get support

---

## Installation

Navigate to your project's components directory:

```bash
cd ../your_project/components
```

Clone the repository:

```bash
git clone https://github.com/aZholtikov/zh_ac_dimmer.git
```

In your application, include the header:

```c
#include "zh_ac_dimmer.h"
```

The component will be automatically built with your project.

### Required Configuration

For correct operation, please enable the following settings in menuconfig:

```text
GPIO_CTRL_FUNC_IN_IRAM
CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM
CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM
CONFIG_GPTIMER_ISR_CACHE_SAFE
```

---

## API Reference

### zh_ac_dimmer_init_config_t Structure

```c
typedef struct
{
    gpio_num_t zero_cross_gpio; // Zero-crossing detection GPIO
    gpio_num_t triac_gpio;      // TRIAC control GPIO
} zh_ac_dimmer_init_config_t;
```

Use the `ZH_AC_DIMMER_INIT_CONFIG_DEFAULT()` macro to initialize with default values:

- `zero_cross_gpio`: GPIO_NUM_MAX
- `triac_gpio`: GPIO_NUM_MAX

---

### zh_ac_dimmer_handle_t Structure

```c
typedef struct _zh_ac_dimmer_handle_t zh_ac_dimmer_handle_t;
```

Opaque instance descriptor.

---

### zh_ac_dimmer_init()

Initializes AC dimmer control.

Allocates memory for handle, validates configuration, initializes GPIO pins (TRIAC as output, zero-crossing as input with positive edge interrupt), sets up ISR service and creates gptimer instance.

**Parameters:**

- `config` - Pointer to initialization configuration structure. Must not be NULL
- `handle` - Pointer to pointer to receive the created handle. Must be NULL

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - Invalid argument (NULL handle or invalid GPIO)
- `ESP_ERR_INVALID_STATE` - AC dimmer is already initialized
- `ESP_ERR_NO_MEM` - Memory allocation failed
- `ESP_FAIL` - Initialization error (GPIO, ISR service or timer)

**Example:**

```c
zh_ac_dimmer_handle_t *dimmer = NULL;
zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
config.zero_cross_gpio = GPIO_NUM_16;
config.triac_gpio = GPIO_NUM_17;
esp_err_t err = zh_ac_dimmer_init(&config, &dimmer);
```

---

### zh_ac_dimmer_deinit()

Deinitializes AC dimmer control and releases resources.

Stops and deletes gptimer, removes GPIO ISR handler, resets GPIO pins to input mode, frees handle and sets the output pointer to NULL.

**Parameters:**

- `handle` - Pointer to handle. Must not be NULL

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - handle or *handle is NULL
- `ESP_FAIL` - Timer stop, detach or delete error, or GPIO reset error

**Important:** After calling this function, the handle becomes invalid. To reinitialize, call `zh_ac_dimmer_init()` again.

---

### zh_ac_dimmer_start()

Starts AC dimmer operation.

Enables the dimmer so that zero-crossing events trigger TRIAC firing according to the current dimming value.

**Parameters:**

- `handle` - Pointer to handle. Must not be NULL

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - handle or *handle is NULL

---

### zh_ac_dimmer_stop()

Stops AC dimmer operation.

Disables TRIAC firing on zero-crossing events. The dimmer can be restarted with `zh_ac_dimmer_start()`.

**Parameters:**

- `handle` - Pointer to handle. Must not be NULL

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - handle or *handle is NULL

---

### zh_ac_dimmer_set()

Sets the AC dimmer dimming value.

Sets the dimming percentage that will be applied on the next zero-crossing event.

**Parameters:**

- `handle` - Pointer to handle. Must not be NULL
- `value` - Dimming level from 0 (off) to 100 (full power)

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - handle, *handle or value is invalid

---

### zh_ac_dimmer_get()

Gets the current AC dimmer dimming value.

**Parameters:**

- `handle` - Pointer to handle. Must not be NULL
- `value` - Pointer to receive the current dimming level (0–100). Must not be NULL

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - handle, *handle or value is NULL

---

## Usage Examples

### Basic Example: Dimmer Control

```c
#include "zh_ac_dimmer.h"

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    zh_ac_dimmer_handle_t *dimmer = NULL;
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = GPIO_NUM_16;
    config.triac_gpio = GPIO_NUM_17;
    zh_ac_dimmer_init(&config, &dimmer);
    zh_ac_dimmer_start(&dimmer);
    uint8_t value = 0;
    for (uint8_t i = 0; i <= 100; ++i)
    {
        zh_ac_dimmer_set(&dimmer, i);
        zh_ac_dimmer_get(&dimmer, &value);
        printf("Dimmer value: %d.\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    for (uint8_t i = 100; i > 0; --i)
    {
        zh_ac_dimmer_set(&dimmer, i);
        zh_ac_dimmer_get(&dimmer, &value);
        printf("Dimmer value: %d.\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    zh_ac_dimmer_stop(&dimmer);
    zh_ac_dimmer_deinit(&dimmer);
}
```

---

### Example: Dimmer with Button Control

```c
#include "zh_ac_dimmer.h"

#define ZERO_CROSS_GPIO GPIO_NUM_16
#define TRIAC_GPIO GPIO_NUM_17
#define BUTTON_GPIO GPIO_NUM_0

static uint8_t dimmer_value = 50;
static zh_ac_dimmer_handle_t *dimmer = NULL;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    (void)arg;
    dimmer_value = (dimmer_value + 10) % 110;
    if (dimmer_value > 100)
    {
        dimmer_value = 0;
    }
    zh_ac_dimmer_set(&dimmer, dimmer_value);
    printf("Dimmer value: %d\n", dimmer_value);
}

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    // Initialize dimmer
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = ZERO_CROSS_GPIO;
    config.triac_gpio = TRIAC_GPIO;
    zh_ac_dimmer_init(&config, &dimmer);
    // Initialize button
    gpio_config_t button_config = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&button_config);
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
    // Start dimmer
    zh_ac_dimmer_start(&dimmer);
    printf("Dimmer initialized. Button on GPIO %d changes dimmer value.\n", BUTTON_GPIO);
    for (;;)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        uint8_t current_value;
        zh_ac_dimmer_get(&dimmer, &current_value);
        printf("Current dimmer value: %d\n", current_value);
    }
}
```

---

## Technical Specifications

| Parameter | Value |
|-----------|-------|
| **Dimming Range** | 0–100% (0 = off, 100 = full power) |
| **AC Frequency** | 400 Hz (zero-crossing period: 1250 µs) |
| **Zero-Crossing Detection** | GPIO interrupt (positive edge) |
| **TRIAC Control** | GPIO output |
| **Timer Resolution** | 1 MHz (1 µs) |
| **Measurement Time** | ~1250 µs per half-cycle |
| **Base TRIAC Delay** | 330 µs (measured with logic analyzer) |
| **ESP-IDF Version** | >= 5.0 |
| **Platform** | ESP32 family |
| **Language** | C (C99) |

---

## Error Codes

| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Operation successful |
| `ESP_ERR_INVALID_ARG` | Invalid argument (NULL pointer, invalid GPIO or invalid dimming value) |
| `ESP_ERR_INVALID_STATE` | AC dimmer is already initialized (only for `zh_ac_dimmer_init`) |
| `ESP_ERR_NO_MEM` | Memory allocation failed (only for `zh_ac_dimmer_init`) |
| `ESP_FAIL` | General error (GPIO, ISR service, timer initialization/deinitialization) |

---

## Contributing

Contributions are welcome! To contribute:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

Please ensure your code follows the existing style and includes appropriate documentation.

---

## License

This project is licensed under the Apache License, Version 2.0 - see the [LICENSE](LICENSE) file for details.

### Apache License, Version 2.0

Copyright (c) 2026 Alexey Zholtikov

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

---

## Additional Notes

- **AC Frequency**: Currently only 400 Hz frequency is supported (aviation frequency). The period between zero-crossings is 1250 µs.
- **Zero-Crossing Timing**: 1250 µs period (400 Hz) with 330 µs compensation for timer delay. Calculation formula: `alarm_count = (((1250 - 330) / 100) * (100 - dimmer_value)) + 330`.
- **GPIO Interrupt**: Zero-crossing detection uses positive edge interrupt with debounce protection (interrupts arriving earlier than 90% of the period are ignored).
- **IRAM Requirements**: For correct operation, enable `GPIO_CTRL_FUNC_IN_IRAM`, `CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM`, `CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM` and `CONFIG_GPTIMER_ISR_CACHE_SAFE` in menuconfig.
- **Thread Safety**: The component uses ESP-IDF drivers which are thread-safe.
- **Safety**: This module interacts with mains voltage. Incorrect wiring or insufficient isolation may result in electric shock or equipment damage. Ensure proper optoisolation and safety compliance before deployment.

---

*Generated for zh_ac_dimmer v3.0.0*
