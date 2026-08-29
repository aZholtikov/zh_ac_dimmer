# zh_ac_dimmer - Компонент управления освещением переменного тока для ESP-IDF

## Содержание

- [Обзор](#обзор)
- [Возможности](#возможности)
- [Установка](#установка)
- [Справочник API](#справочник-api)
- [Примеры использования](#примеры-использования)
- [Технические характеристики](#технические-характеристики)
- [Коды ошибок](#коды-ошибок)
- [Вклад в проект](#вклад-в-проект)
- [Лицензия](#лицензия)

---

## Обзор

`zh_ac_dimmer` - это легковесный компонент ESP-IDF для управления освещением переменного тока. Он предоставляет простой API для управления мощностью нагрузки переменного тока с использованием детекции перехода через ноль и управления TRIAC. Компонент разработан специально для микроконтроллеров ESP32 и использует драйверы ESP-IDF v5.0+ (GPTimer, GPIO).

Компонент поддерживает частоту переменного тока 400 Гц и использует аппаратные таймеры для точного управления временем.

---

## Возможности

1. **Управление мощностью переменного тока**: Управление мощностью нагрузки от 0% до 100%
2. **Детекция перехода через ноль**: Использует прерывание GPIO для точного тайминга
3. **Управление TRIAC**: Управление затвором TRIAC с точным таймингом через GPTimer
4. **Аппаратный таймер**: Использует GPTimer с разрешением 1 МГц (1 мкс)
5. **IRAM-safe ISR и callback**: Прерывания и колбэки размещаются в IRAM для надёжной работы
6. **Обработка ошибок**: Комплексная проверка ошибок и валидация конфигурации
7. **Потокобезопасность**: Использует драйверы ESP-IDF (потокобезопасно)
8. **Минимальный объем памяти**: Низкие накладные расходы на память
9. **Простой API**: Легковесный интерфейс с поддержкой start/stop/set/get

---

## Установка

Перейдите в каталог компонентов вашего проекта:

```bash
cd ../ваш_проект/components
```

Клонируйте репозиторий:

```bash
git clone https://github.com/aZholtikov/zh_ac_dimmer.git
```

В вашем приложении подключите заголовочный файл:

```c
#include "zh_ac_dimmer.h"
```

Компонент будет автоматически собран вместе с вашим проектом.

### Необходимая конфигурация

Для правильной работы включите следующие настройки в menuconfig:

```text
GPIO_CTRL_FUNC_IN_IRAM
CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM
CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM
CONFIG_GPTIMER_ISR_CACHE_SAFE
```

---

## Справочник API

### Структура zh_ac_dimmer_init_config_t

```c
typedef struct
{
    gpio_num_t zero_cross_gpio; // GPIO детекции перехода через ноль
    gpio_num_t triac_gpio;      // GPIO управления TRIAC
} zh_ac_dimmer_init_config_t;
```

Используйте макрос `ZH_AC_DIMMER_INIT_CONFIG_DEFAULT()` для инициализации значениями по умолчанию:

- `zero_cross_gpio`: GPIO_NUM_MAX
- `triac_gpio`: GPIO_NUM_MAX

---

### Структура zh_ac_dimmer_handle_t

```c
typedef struct _zh_ac_dimmer_handle_t zh_ac_dimmer_handle_t;
```

Непрозрачный дескриптор экземпляра устройства.

---

### zh_ac_dimmer_init()

Инициализирует управление освещением переменного тока.

Выделяет память для handle, валидирует конфигурацию, инициализирует GPIO пины (TRIAC как выход, переход через ноль как вход с прерыванием по положительному фронту), устанавливает ISR-сервис и создаёт экземпляр gptimer.

**Параметры:**

- `config` - Указатель на структуру конфигурации инициализации. Не должен быть NULL
- `handle` - Указатель на указатель для получения созданного handle. Должен быть NULL

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - Неверный аргумент (NULL handle или неверный GPIO)
- `ESP_ERR_INVALID_STATE` - Освещение переменного тока уже инициализировано
- `ESP_ERR_NO_MEM` - Не удалось выделить память
- `ESP_FAIL` - Ошибка инициализации (GPIO, ISR-сервис или таймер)

**Пример:**

```c
zh_ac_dimmer_handle_t *dimmer = NULL;
zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
config.zero_cross_gpio = GPIO_NUM_16;
config.triac_gpio = GPIO_NUM_17;
esp_err_t err = zh_ac_dimmer_init(&config, &dimmer);
```

---

### zh_ac_dimmer_deinit()

Деинициализирует управление освещением переменного тока и освобождает ресурсы.

Останавливает и удаляет gptimer, удаляет ISR-обработчик GPIO, сбрасывает пины GPIO в режим ввода, освобождает handle и устанавливает выходной указатель в NULL.

**Параметры:**

- `handle` - Указатель на handle. Не должен быть NULL

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - handle или *handle равен NULL
- `ESP_FAIL` - Ошибка остановки таймера, отключения или удаления, или сброса GPIO

**Важно:** После вызова этой функции handle становится недействительным. Для повторной инициализации вызовите `zh_ac_dimmer_init()` заново.

---

### zh_ac_dimmer_start()

Запускает работу освещения переменного тока.

Включает диммер, чтобы события перехода через ноль запускали срабатывание TRIAC в соответствии с текущим значением затухания.

**Параметры:**

- `handle` - Указатель на handle. Не должен быть NULL

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - handle или *handle равен NULL

---

### zh_ac_dimmer_stop()

Останавливает работу освещения переменного тока.

Отключает срабатывание TRIAC на событиях перехода через ноль. Диммер может быть перезапущен с помощью `zh_ac_dimmer_start()`.

**Параметры:**

- `handle` - Указатель на handle. Не должен быть NULL

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - handle или *handle равен NULL

---

### zh_ac_dimmer_set()

Устанавливает значение затухания освещения переменного тока.

Устанавливает процент затухания, который будет применён на следующем событии перехода через ноль.

**Параметры:**

- `handle` - Указатель на handle. Не должен быть NULL
- `value` - Уровень затухания от 0 (выключено) до 100 (полная мощность)

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - handle, *handle или value недействительны

---

### zh_ac_dimmer_get()

Получает текущее значение затухания освещения переменного тока.

**Параметры:**

- `handle` - Указатель на handle. Не должен быть NULL
- `value` - Указатель для получения текущего уровня затухания (0–100). Не должен быть NULL

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - handle, *handle или value равен NULL

---

## Примеры использования

### Базовый пример: Управление затуханием

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
        printf("Значение затухания: %d.\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    for (uint8_t i = 100; i > 0; --i)
    {
        zh_ac_dimmer_set(&dimmer, i);
        zh_ac_dimmer_get(&dimmer, &value);
        printf("Значение затухания: %d.\n", value);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    zh_ac_dimmer_stop(&dimmer);
    zh_ac_dimmer_deinit(&dimmer);
}
```

---

### Пример: Управление затуханием с кнопкой

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
    printf("Значение затухания: %d\n", dimmer_value);
}

void app_main(void)
{
    esp_log_level_set("zh_ac_dimmer", ESP_LOG_ERROR);
    // Инициализация затухания
    zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT();
    config.zero_cross_gpio = ZERO_CROSS_GPIO;
    config.triac_gpio = TRIAC_GPIO;
    zh_ac_dimmer_init(&config, &dimmer);
    // Инициализация кнопки
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
    // Запуск затухания
    zh_ac_dimmer_start(&dimmer);
    printf("Затухание инициализировано. Кнопка на GPIO %d меняет значение затухания.\n", BUTTON_GPIO);
    for (;;)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        uint8_t current_value;
        zh_ac_dimmer_get(&dimmer, &current_value);
        printf("Текущее значение затухания: %d\n", current_value);
    }
}
```

---

## Технические характеристики

| Параметр | Значение |
|----------|----------|
| **Диапазон затухания** | 0–100% (0 = выкл, 100 = полная мощность) |
| **Частота переменного тока** | 400 Гц (период перехода через ноль: 1250 мкс) |
| **Детекция перехода через ноль** | Прерывание GPIO (положительный фронт) |
| **Управление TRIAC** | Выход GPIO |
| **Разрешение таймера** | 1 МГц (1 мкс) |
| **Время измерения** | ~1250 мкс на полупериод |
| **Базовая задержка TRIAC** | 330 мкс (измерено логическим анализатором) |
| **Версия ESP-IDF** | >= 5.0 |
| **Платформа** | Семейство ESP32 |
| **Язык** | C (C99) |

---

## Коды ошибок

| Код ошибки | Описание |
|------------|----------|
| `ESP_OK` | Операция выполнена успешно |
| `ESP_ERR_INVALID_ARG` | Неверный аргумент (NULL указатель, неверный GPIO или неверное значение затухания) |
| `ESP_ERR_INVALID_STATE` | Освещение переменного тока уже инициализировано (только для `zh_ac_dimmer_init`) |
| `ESP_ERR_NO_MEM` | Не удалось выделить память (только для `zh_ac_dimmer_init`) |
| `ESP_FAIL` | Общая ошибка (GPIO, ISR-сервис, инициализация/деинициализация таймера) |

---

## Вклад в проект

Вклад приветствуется! Чтобы внести свой вклад:

1. Сделайте форк репозитория
2. Создайте ветку функции (`git checkout -b feature/AmazingFeature`)
3. Закоммитьте ваши изменения (`git commit -m 'Add some AmazingFeature'`)
4. Отправьте в ветку (`git push origin feature/AmazingFeature`)
5. Откройте Pull Request

Пожалуйста, убедитесь, что ваш код следует существующему стилю и включает соответствующую документацию.

---

## Лицензия

Этот проект лицензирован по лицензии Apache, версия 2.0 - см. файл [LICENSE](LICENSE) для подробной информации.

### Apache License, Version 2.0

Авторское право (c) 2026 Алексей Жолтиков

Лицензировано по лицензии Apache License, Version 2.0 (далее — "Лицензия");
вы не можете использовать этот файл, кроме случаев, предусмотренных Лицензией.
Копию Лицензии можно получить по адресу:

    http://www.apache.org/licenses/LICENSE-2.0

Если иное не требуется действующим законодательством или не согласовано в письменном виде,
программное обеспечение, распространяемое по Лицензии, распространяется на условиях "КАК ЕСТЬ",
БЕЗ КАКИХ-ЛИБО ГАРАНТИЙ, явных или подразумеваемых, включая, но не ограничиваясь, гарантии
ТОВАРНОГО СОСТОЯНИЯ, ПРИГОДНОСТИ ДЛЯ КОНКРЕТНОЙ ЦЕЛИ И НЕНАРУШЕНИЯ ПРАВ.
Смотрите Лицензию для получения конкретных прав и ограничений.

---

## Дополнительные заметки

- **Частота переменного тока**: В настоящее время поддерживается только частота 400 Гц (авиационная частота). Период между переходами через ноль составляет 1250 мкс.
- **Тайминг перехода через ноль**: 1250 мкс период (400 Гц) с компенсацией 330 мкс для задержки таймера. Формула расчёта: `alarm_count = (((1250 - 330) / 100) * (100 - dimmer_value)) + 330`.
- **Прерывание GPIO**: Детекция перехода через ноль использует прерывание положительного фронта с защитой от дребезга (игнорируются прерывания, пришедшие ранее 90% от периода).
- **Требования IRAM**: Для правильной работы включите `GPIO_CTRL_FUNC_IN_IRAM`, `CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM`, `CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM` и `CONFIG_GPTIMER_ISR_CACHE_SAFE` в menuconfig.
- **Потокобезопасность**: Компонент использует драйверы ESP-IDF, которые являются потокобезопасными.
- **Безопасность**: Данный модуль взаимодействует с сетевым напряжением. Неправильное подключение или недостаточная изоляция могут привести к поражению электрическим током или повреждению оборудования. Убедитесь в наличии правильной оптоизоляции и соблюдении требований безопасности перед развёртыванием.

---

*Сгенерировано для zh_ac_dimmer v3.0.0*
