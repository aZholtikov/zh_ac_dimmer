# ESP32 ESP-IDF component for AC dimmer

## Features

1. Zero-crossing detection via GPIO interrupt on both edges (`GPIO_INTR_ANYEDGE`), with edge polarity determined from the current GPIO level and duplicate/glitch edge rejection. Spurious-interrupt filtering: the interval between rising edges must be within the half-cycle range derived from the frequency 30 - 500 Hz (shorter intervals are ignored, longer ones reset the timing and calibration state).
2. Measured zero-crossing half-period is low-pass filtered and used to calculate the TRIAC firing delay, so the dimmer adapts to the actual mains frequency.
3. TRIAC gate control through ESP-IDF gptimer (1 MHz resolution) with configurable dimming level (0–100%): 0 keeps the load off, 100 fires the TRIAC immediately. The firing delay is clamped to leave a guard band (period / 20, at least 5 us) before the end of the half-cycle.
4. Automatic runtime calibration of the hardware base offset: on each falling edge the width of the zero-crossing pulse is measured and fed into an EMA (`pulse_width / 2`), compensating for the zero-crossing detector advance relative to the physical mains zero crossing.
5. Handle-based API: init/deinit (with full GPIO, ISR and timer cleanup), start/stop, set/get dimming level.

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

## Example

```c
#include "zh_ac_dimmer.h"

zh_ac_dimmer_handle_t *ac_dimmer_handle = NULL;

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = GPIO_NUM_16;
    config.triac_gpio = GPIO_NUM_17;
    zh_ac_dimmer_init(&config, &ac_dimmer_handle);
    zh_ac_dimmer_start(&ac_dimmer_handle);
    uint8_t value = 0;
    for (uint8_t i = 0; i <= 100; ++i)
    {
        zh_ac_dimmer_set(&ac_dimmer_handle, i);
        zh_ac_dimmer_get(&ac_dimmer_handle, &value);
        printf("Dimmer value: %d.\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    for (uint8_t i = 100; i > 0; --i)
    {
        zh_ac_dimmer_set(&ac_dimmer_handle, i);
        zh_ac_dimmer_get(&ac_dimmer_handle, &value);
        printf("Dimmer value: %d.\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    zh_ac_dimmer_stop(&ac_dimmer_handle);
    zh_ac_dimmer_deinit(&ac_dimmer_handle);
}
```
