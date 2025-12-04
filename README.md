# ESP32 ESP-IDF component for AC dimmer

## Tested on

1. [ESP32 ESP-IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/index.html)

## Features

1. Supports frequency up to 400 Hz.
2. Automatic frequency detection.

## Attention

For correct operation, please enable the following settings in the menuconfig:

```text
GPIO_CTRL_FUNC_IN_IRAM
CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM
CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM
CONFIG_GPTIMER_ISR_CACHE_SAFE
CONFIG_GPTIMER_OBJ_CACHE_SAFE
```

## Using

In an existing project, run the following command to install the components:

```text
cd ../your_project/components
git clone http://git.zh.com.ru/esp_components/zh_ac_dimmer
```

In the application, add the component:

```c
#include "zh_ac_dimmer.h"
```

## Examples

```c
#include "zh_ac_dimmer.h"

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = GPIO_NUM_16;
    config.triac_gpio = GPIO_NUM_17;
    zh_ac_dimmer_init(&config);
    zh_ac_dimmer_start();
    uint8_t value = 0;
    for (uint8_t i = 0; i <= 100; ++i)
    {
        zh_ac_dimmer_set(i);
        zh_ac_dimmer_get(&value);
        printf("Dimmer value: %d\n.");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    for (uint8_t i = 100; i > 0; --i)
    {
        zh_ac_dimmer_set(i);
        zh_ac_dimmer_get(&value);
        printf("Dimmer value: %d\n.");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    zh_ac_dimmer_stop();
    zh_ac_dimmer_deinit();
}
```
