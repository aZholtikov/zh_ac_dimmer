# ESP32 ESP-IDF component for AC dimmer for 115V/400Hz

## Features

1. Zero-crossing detection via GPIO interrupt (positive edge) with filtering of spurious interrupts for precise phase-angle control.
2. TRIAC gate control through ESP-IDF gptimer (1 MHz resolution) with configurable dimming level (0–100%).
3. Designed for 400 Hz AC mains (1250 us half-period), with timing constants adjustable for other frequencies.
4. Handle-based API: init/deinit (with full GPIO, ISR and timer cleanup) and start/stop/set/get.

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
