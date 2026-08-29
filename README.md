# ESP32 ESP-IDF component for AC dimmer for 115V/400Hz

## Wiki

[EN](WIKI_EN.md) | [RU](WIKI_RU.md)

## Tested on

1. [ESP32 ESP-IDF v6.0.0](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/index.html)

## SAST Tools

[PVS-Studio](https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) — static analyzer for C, C++, C#, and Java code.

## Features

1. Zero-crossing detection via GPIO interrupt for precise phase-angle control.
2. TRIAC gate control through ESP-IDF gptimer with configurable dimming level (0–100%).
3. IRAM-safe ISR and callback functions for reliable operation.
4. Thread-safe start/stop/set/get API.

## Using

In an existing project, run the following command to install the component:

```text
cd ../your_project/components
git clone https://github.com/aZholtikov/zh_ac_dimmer
```

In the application, add the component:

```c
#include "zh_ac_dimmer.h"
```

## Examples

See Wiki [EN](WIKI_EN.md#usage-examples) | [RU](WIKI_RU.md#примеры-использования)
