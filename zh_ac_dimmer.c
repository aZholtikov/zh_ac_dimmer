#include "zh_ac_dimmer.h"

static const char *TAG = "zh_ac_dimmer";

#define ZH_LOGI(msg, ...) ESP_LOGI(TAG, msg, ##__VA_ARGS__)
#define ZH_LOGE(msg, err, ...) ESP_LOGE(TAG, "[%s:%d:%s] " msg, __FILE__, __LINE__, esp_err_to_name(err), ##__VA_ARGS__)

#define ZH_ERROR_CHECK(cond, err, cleanup, msg, ...) \
    if (!(cond))                                     \
    {                                                \
        ZH_LOGE(msg, err, ##__VA_ARGS__);            \
        cleanup;                                     \
        return err;                                  \
    }

#define ZH_ERROR_CHECK_CONT(cond, cleanup, msg, ...) \
    if (!(cond))                                     \
    {                                                \
        ZH_LOGE(msg, ESP_FAIL, ##__VA_ARGS__);       \
        cleanup;                                     \
    }

/**
 * @brief Internal handle for AC dimmer instance.
 *
 * Stores GPIO pin assignments, gptimer handle, timing state,
 * current dimming level, and running state.
 */
struct _zh_ac_dimmer_handle_t
{
    gpio_num_t zero_cross_gpio;           /*!< GPIO pin for zero-crossing detection input */
    gpio_num_t triac_gpio;                /*!< GPIO pin for TRIAC gate control output */
    gptimer_handle_t dimmer_timer_handle; /*!< gptimer instance used for TRIAC firing delay */
    uint64_t prev_zero_crossing_us;       /*!< Timestamp of the last zero-crossing event (us) */
    uint8_t dimmer_value;                 /*!< Current dimming level (0 = off, 100 = full) */
    bool is_dimmer_work;                  /*!< `true` when the dimmer is actively running */
};

/**
 * @brief Validate the AC dimmer initialization configuration.
 *
 * Checks that both GPIO pins are valid (not `GPIO_NUM_MAX`),
 * are within the acceptable range, and are not the same pin.
 *
 * @param config Pointer to the initialization configuration structure
 *
 * @return ESP_OK if the configuration is valid
 * @return ESP_ERR_INVALID_ARG if any GPIO pin is invalid or both pins are the same
 */
static esp_err_t _zh_ac_dimmer_validate_config(const zh_ac_dimmer_init_config_t *config);

/**
 * @brief Initialize GPIO pins for TRIAC control and zero-crossing detection.
 *
 * Configures the TRIAC GPIO as an output (pulled low) and the zero-crossing
 * GPIO as an input with pull-down and positive-edge interrupt. Installs the
 * ESP-IDF ISR service (if not already installed) and registers the ISR
 * handler for the zero-crossing pin.
 *
 * @param config Pointer to the initialization configuration structure
 * @param handle Pointer to the dimmer handle to store GPIO pin numbers
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if GPIO configuration, ISR service installation, or ISR
 *         handler registration failed
 *
 * @note `ESP_ERR_INVALID_STATE` from `gpio_install_isr_service()` is treated
 *       as success because the ISR service may have been installed previously.
 */
static esp_err_t _zh_ac_dimmer_gpio_init(const zh_ac_dimmer_init_config_t *config, zh_ac_dimmer_handle_t *handle);

/**
 * @brief Initialize the gptimer used for TRIAC firing delay.
 *
 * Creates a new gptimer with a 1 MHz resolution (1 us steps) and registers
 * the alarm callback that fires the TRIAC. The timer is enabled but not
 * started; it is armed and started from the zero-crossing ISR for each
 * half-cycle.
 *
 * @param handle Pointer to the dimmer handle (timer handle stored here)
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if timer creation, callback registration, or enable failed
 */
static esp_err_t _zh_ac_dimmer_timer_init(zh_ac_dimmer_handle_t *handle);

/**
 * @brief ISR handler for zero-crossing detection.
 *
 * Executed on every positive edge of the zero-crossing signal. Ignores
 * spurious interrupts that arrive sooner than 90% of the expected
 * zero-crossing period (1250 us at 400 Hz). Resets the TRIAC gate low and
 * updates the last zero-crossing timestamp. Depending on the dimming level:
 *
 * - At dimming = 0: TRIAC stays off (gate held low).
 * - At dimming = 100: TRIAC fires immediately (gate set high).
 * - At dimming 1–99: gptimer alarm fires after a calculated delay. The alarm
 *   count is mapped from the dimming value with a 330 us base offset
 *   (50% of the zero-crossing period, measured with a logic analyzer).
 *
 * @param arg Pointer to the dimmer handle (passed from GPIO ISR)
 */
static void _zh_ac_dimmer_isr_handler(void *arg);

/**
 * @brief gptimer alarm callback that fires the TRIAC.
 *
 * Sets the TRIAC gate high to trigger conduction, then stops and resets
 * the timer for the next half-cycle.
 *
 * @param timer Handle to the gptimer that triggered the alarm
 * @param edata Event data (unused)
 * @param user_ctx Pointer to the dimmer handle
 *
 * @return false to indicate that no higher-priority task needs to be woken
 */
static bool _zh_ac_dimmer_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);

esp_err_t zh_ac_dimmer_init(const zh_ac_dimmer_init_config_t *config, zh_ac_dimmer_handle_t **handle)
{
    ZH_LOGI("AC dimmer initialization started.");
    ZH_ERROR_CHECK(config != NULL && handle != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer initialization failed. Invalid argument.");
    ZH_ERROR_CHECK(*handle == NULL, ESP_ERR_INVALID_STATE, NULL, "AC dimmer initialization failed. AC dimmer is already initialized.");
    *handle = heap_caps_calloc(1, sizeof(zh_ac_dimmer_handle_t), MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK(*handle != NULL, ESP_ERR_NO_MEM, NULL, "AC dimmer initialization failed. Failed to allocate handle.");
    ZH_ERROR_CHECK(_zh_ac_dimmer_validate_config(config) == ESP_OK, ESP_FAIL, heap_caps_free(*handle); *handle = NULL, "AC dimmer initialization failed. Initial configuration check failed.");
    ZH_ERROR_CHECK(_zh_ac_dimmer_gpio_init(config, *handle) == ESP_OK, ESP_FAIL, heap_caps_free(*handle); *handle = NULL, "AC dimmer initialization failed. GPIO initialization failed.");
    ZH_ERROR_CHECK(_zh_ac_dimmer_timer_init(*handle) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK_CONT(gpio_isr_handler_remove(config->zero_cross_gpio) == ESP_OK, NULL, "Remove GPIO isr handler failed.")};
                   {ZH_ERROR_CHECK_CONT(gpio_reset_pin(config->triac_gpio) == ESP_OK, NULL, "Reset GPIO failed.")};
                   {ZH_ERROR_CHECK_CONT(gpio_reset_pin(config->zero_cross_gpio) == ESP_OK, NULL, "Reset GPIO failed.")};
                   heap_caps_free(*handle); *handle = NULL, "AC dimmer initialization failed. Timer initialization failed.");
    ZH_LOGI("AC dimmer initialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_deinit(zh_ac_dimmer_handle_t **handle)
{
    ZH_LOGI("AC dimmer deinitialization started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer deinitialization failed. Invalid argument.");
    ZH_ERROR_CHECK_CONT(gptimer_stop((*handle)->dimmer_timer_handle) == ESP_OK, NULL, "AC dimmer deinitialization failed. Timer stop fail.");
    ZH_ERROR_CHECK_CONT(gptimer_disable((*handle)->dimmer_timer_handle) == ESP_OK, NULL, "AC dimmer deinitialization failed. Timer disable fail.");
    ZH_ERROR_CHECK_CONT(gptimer_del_timer((*handle)->dimmer_timer_handle) == ESP_OK, NULL, "AC dimmer deinitialization failed. Timer delete fail.");
    ZH_ERROR_CHECK_CONT(gpio_isr_handler_remove((*handle)->zero_cross_gpio) == ESP_OK, NULL, "AC dimmer deinitialization failed. Remove GPIO isr handler failed.");
    ZH_ERROR_CHECK_CONT(gpio_reset_pin((*handle)->triac_gpio) == ESP_OK, NULL, "AC dimmer deinitialization failed. Reset GPIO failed.");
    ZH_ERROR_CHECK_CONT(gpio_reset_pin((*handle)->zero_cross_gpio) == ESP_OK, NULL, "AC dimmer deinitialization failed. Reset GPIO failed.");
    heap_caps_free(*handle);
    *handle = NULL;
    ZH_LOGI("AC dimmer deinitialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_start(zh_ac_dimmer_handle_t **handle)
{
    ZH_LOGI("AC dimmer start begin.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer start failed. Invalid argument.");
    (*handle)->is_dimmer_work = true;
    ZH_LOGI("AC dimmer start completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_stop(zh_ac_dimmer_handle_t **handle)
{
    ZH_LOGI("AC dimmer stop begin.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer stop failed. Invalid argument.");
    (*handle)->is_dimmer_work = false;
    ZH_LOGI("AC dimmer stop completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_set(zh_ac_dimmer_handle_t **handle, uint8_t value)
{
    ZH_LOGI("AC dimmer setup begin.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer setup failed. Invalid argument.");
    ZH_ERROR_CHECK(value <= 100, ESP_ERR_INVALID_ARG, NULL, "AC dimmer setup failed. Dimming value invalid.");
    (*handle)->dimmer_value = value;
    ZH_LOGI("AC dimmer setup completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_get(zh_ac_dimmer_handle_t **handle, uint8_t *value)
{
    ZH_LOGI("AC dimmer getting status begin.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL && value != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer getting status failed. Invalid argument.");
    *value = (*handle)->dimmer_value;
    ZH_LOGI("AC dimmer getting status completed successfully.");
    return ESP_OK;
}

static esp_err_t _zh_ac_dimmer_validate_config(const zh_ac_dimmer_init_config_t *config)
{
    ZH_ERROR_CHECK(config->zero_cross_gpio < GPIO_NUM_MAX, ESP_ERR_INVALID_ARG, NULL, "Zero cross GPIO invalid.");
    ZH_ERROR_CHECK(config->triac_gpio < GPIO_NUM_MAX, ESP_ERR_INVALID_ARG, NULL, "Triac GPIO invalid.");
    ZH_ERROR_CHECK((config->zero_cross_gpio != config->triac_gpio), ESP_ERR_INVALID_ARG, NULL, "Both GPIO is same.");
    return ESP_OK;
}

static esp_err_t _zh_ac_dimmer_gpio_init(const zh_ac_dimmer_init_config_t *config, zh_ac_dimmer_handle_t *handle)
{
    gpio_config_t triac_gpio_config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << config->triac_gpio),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ZH_ERROR_CHECK(gpio_config(&triac_gpio_config) == ESP_OK, ESP_FAIL, NULL, "Triac GPIO configuration failed.");
    ZH_ERROR_CHECK(gpio_set_level(config->triac_gpio, 0) == ESP_OK, ESP_FAIL, NULL, "Triac GPIO set failed.");
    gpio_config_t zero_cross_gpio_config = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << config->zero_cross_gpio),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ZH_ERROR_CHECK(gpio_config(&zero_cross_gpio_config) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK_CONT(gpio_reset_pin(config->triac_gpio) == ESP_OK, NULL, "Reset GPIO failed.")}, "Zero cross GPIO configuration failed.");
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED);
    ZH_ERROR_CHECK(err == ESP_OK || err == ESP_ERR_INVALID_STATE, ESP_FAIL,
                   {ZH_ERROR_CHECK_CONT(gpio_reset_pin(config->triac_gpio) == ESP_OK, NULL, "Reset GPIO failed.")};
                   {ZH_ERROR_CHECK_CONT(gpio_reset_pin(config->zero_cross_gpio) == ESP_OK, NULL, "Reset GPIO failed.")}, "Failed install isr service.")
    handle->triac_gpio = config->triac_gpio;
    handle->zero_cross_gpio = config->zero_cross_gpio;
    ZH_ERROR_CHECK(gpio_isr_handler_add(config->zero_cross_gpio, _zh_ac_dimmer_isr_handler, handle) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK_CONT(gpio_reset_pin(config->triac_gpio) == ESP_OK, NULL, "Reset GPIO failed.")};
                   {ZH_ERROR_CHECK_CONT(gpio_reset_pin(config->zero_cross_gpio) == ESP_OK, NULL, "Reset GPIO failed.")}, "Failed add isr handler.");
    return ESP_OK;
}

static esp_err_t _zh_ac_dimmer_timer_init(zh_ac_dimmer_handle_t *handle)
{
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1 * 1000 * 1000,
    };
    ZH_ERROR_CHECK(gptimer_new_timer(&timer_config, &handle->dimmer_timer_handle) == ESP_OK, ESP_FAIL, NULL, "Failed create dimmer timer.");
    gptimer_event_callbacks_t cbs = {
        .on_alarm = _zh_ac_dimmer_timer_on_alarm_cb,
    };
    ZH_ERROR_CHECK(gptimer_register_event_callbacks(handle->dimmer_timer_handle, &cbs, handle) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK_CONT(gptimer_del_timer(handle->dimmer_timer_handle) == ESP_OK, NULL, "Timer delete fail.")}, "Failed register dimmer timer event callbacks.");
    ZH_ERROR_CHECK(gptimer_enable(handle->dimmer_timer_handle) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK_CONT(gptimer_del_timer(handle->dimmer_timer_handle) == ESP_OK, NULL, "Timer delete fail.")}, "Failed enable dimmer timer.");
    return ESP_OK;
}

static void _zh_ac_dimmer_isr_handler(void *arg)
{
    zh_ac_dimmer_handle_t *handle = (zh_ac_dimmer_handle_t *)arg;
    uint64_t _current_us = esp_timer_get_time();
    // Ignore spurious interrupts that arrive too soon after the previous one.
    if (_current_us - handle->prev_zero_crossing_us > (1250 * 0.9)) // 90% of zero-crossing period (1250 µs) at 400 Hz.
    {
        gpio_set_level(handle->triac_gpio, 0);
        handle->prev_zero_crossing_us = _current_us;
        if (handle->is_dimmer_work == true)
        {
            if (handle->dimmer_value != 0)
            {
                if (handle->dimmer_value == 100)
                {
                    gpio_set_level(handle->triac_gpio, 1);
                    return;
                }
                gptimer_alarm_config_t _alarm_config = {0};
                // Calculate alarm delay: maps dimming (1–99) to a µs value.
                // 330 µs is the base offset (50% of zero-crossing period, measured with a logic analyzer).
                _alarm_config.alarm_count = (uint64_t)((((1250 - 330) * (100 - handle->dimmer_value)) / 100) + 330);
                _alarm_config.flags.auto_reload_on_alarm = false;
                gptimer_set_alarm_action(handle->dimmer_timer_handle, &_alarm_config);
                gptimer_start(handle->dimmer_timer_handle);
            }
        }
    }
}

static bool _zh_ac_dimmer_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)edata;
    zh_ac_dimmer_handle_t *handle = (zh_ac_dimmer_handle_t *)user_ctx;
    gpio_set_level(handle->triac_gpio, 1);
    gptimer_stop(timer);
    gptimer_set_raw_count(timer, 0);
    return false;
}