# ESP32 ESP-IDF component for AC dimmer (phase-angle control)

## Features

1. Zero-crossing detection via GPIO interrupt on both edges with edge polarity determined from the current GPIO level and duplicate/glitch edge rejection.
2. Spurious-interrupt filtering based on half-cycle range derived from the mains frequency limits (30 to 500 Hz).
3. Low-pass filtered zero-crossing half-period measurement adapts the firing delay to the actual mains frequency.
4. TRIAC gate control through ESP-IDF gptimer with 1 MHz resolution and configurable dimming level from 0 to 100 percent.
5. Firing delay clamped to a guard band of at least 5 microseconds before the end of the half-cycle.
6. Automatic runtime calibration of the hardware base offset using exponential moving average of the zero-crossing pulse width.
7. Handle-based API with init, deinit, start, stop, set and get functions.
8. Memory managed through ESP-IDF heap_caps functions with full cleanup on initialization failure.

## Attention

1. Only one dimmer instance per zero-crossing GPIO pin. Initializing a second instance on the same pin replaces the ISR handler of the first one.
2. Not thread-safe. Volatile fields prevent compiler optimizations but do not provide mutual exclusion between tasks.
3. Calibration assumes a symmetric zero-crossing optocoupler pulse. Asymmetric pulses introduce a systematic offset error.
4. Supported mains frequency range is limited by the compile-time constants 30 to 500 Hz. Out-of-range intervals reset the timing and calibration state.

## Using

In an existing project, run the following command to install the component:

```bash
cd ../your_project/components
git clone https://github.com/aZholtikov/zh_ac_dimmer
```

In the application, add the component:

```c
#include "zh_ac_dimmer.h"
```

## Examples

### Basic Example: Initialize and Control Dimming Level

```c
#include "zh_ac_dimmer.h"

zh_ac_dimmer_handle_t *ac_dimmer_handle = NULL;

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = GPIO_NUM_16;
    config.triac_gpio = GPIO_NUM_17;
    esp_err_t ret = zh_ac_dimmer_init(&config, &ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Dimmer initialization error: %s\n", esp_err_to_name(ret));
        return;
    }
    ret = zh_ac_dimmer_start(&ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Dimmer start error: %s\n", esp_err_to_name(ret));
    }
    uint8_t val1 = 50;
    ret = zh_ac_dimmer_set(&ac_dimmer_handle, val1);
    if (ret != ESP_OK)
    {
        printf("Dimmer set error: %s\n", esp_err_to_name(ret));
    }
    uint8_t value = 0;
    ret = zh_ac_dimmer_get(&ac_dimmer_handle, &value);
    if (ret == ESP_OK)
    {
        printf("Dimmer value: %u\n", value);
    }
    else
    {
        printf("Dimmer get error: %s\n", esp_err_to_name(ret));
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ret = zh_ac_dimmer_stop(&ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Dimmer stop error: %s\n", esp_err_to_name(ret));
    }
    zh_ac_dimmer_deinit(&ac_dimmer_handle);
}
```

---

### Fade In Fade Out Example

```c
#include "zh_ac_dimmer.h"

zh_ac_dimmer_handle_t *ac_dimmer_handle = NULL;

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = GPIO_NUM_16;
    config.triac_gpio = GPIO_NUM_17;
    esp_err_t ret = zh_ac_dimmer_init(&config, &ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Dimmer initialization error: %s\n", esp_err_to_name(ret));
        return;
    }
    ret = zh_ac_dimmer_start(&ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Dimmer start error: %s\n", esp_err_to_name(ret));
    }
    uint8_t value = 0;
    for (uint8_t i = 0; i <= 100; ++i)
    {
        zh_ac_dimmer_set(&ac_dimmer_handle, i);
        zh_ac_dimmer_get(&ac_dimmer_handle, &value);
        printf("Dimmer value: %u\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    for (uint8_t i = 100; i > 0; --i)
    {
        zh_ac_dimmer_set(&ac_dimmer_handle, i);
        zh_ac_dimmer_get(&ac_dimmer_handle, &value);
        printf("Dimmer value: %u\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    zh_ac_dimmer_stop(&ac_dimmer_handle);
    zh_ac_dimmer_deinit(&ac_dimmer_handle);
}
```

---

### Error Handling Example

```c
#include "zh_ac_dimmer.h"

zh_ac_dimmer_handle_t *ac_dimmer_handle = NULL;

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = GPIO_NUM_16;
    config.triac_gpio = GPIO_NUM_16;
    esp_err_t ret = zh_ac_dimmer_init(&config, &ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Expected init error: %s\n", esp_err_to_name(ret));
    }
    config.triac_gpio = GPIO_NUM_17;
    ret = zh_ac_dimmer_init(&config, &ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Dimmer initialization error: %s\n", esp_err_to_name(ret));
        return;
    }
    ret = zh_ac_dimmer_init(&config, &ac_dimmer_handle);
    if (ret != ESP_OK)
    {
        printf("Expected double init error: %s\n", esp_err_to_name(ret));
    }
    zh_ac_dimmer_start(&ac_dimmer_handle);
    ret = zh_ac_dimmer_set(&ac_dimmer_handle, 101);
    if (ret != ESP_OK)
    {
        printf("Expected set range error: %s\n", esp_err_to_name(ret));
    }
    ret = zh_ac_dimmer_set(&ac_dimmer_handle, 75);
    if (ret != ESP_OK)
    {
        printf("Dimmer set error: %s\n", esp_err_to_name(ret));
    }
    uint8_t value = 0;
    zh_ac_dimmer_get(&ac_dimmer_handle, &value);
    printf("Dimmer value: %u\n", value);
    zh_ac_dimmer_stop(&ac_dimmer_handle);
    zh_ac_dimmer_deinit(&ac_dimmer_handle);
}
```
