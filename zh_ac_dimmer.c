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
 * Stores GPIO pin assignments, gptimer handle, zero-crossing timing state,
 * auto-calibrated base offset, current dimming level, and running state.
 */
struct _zh_ac_dimmer_handle_t
{
    gpio_num_t zero_cross_gpio;           /*!< GPIO pin for zero-crossing detection input */
    gpio_num_t triac_gpio;                /*!< GPIO pin for TRIAC gate control output */
    gptimer_handle_t dimmer_timer_handle; /*!< gptimer instance used for TRIAC firing delay */
    uint64_t prev_zero_crossing_us;       /*!< Timestamp of the previous rising edge (us) */
    uint64_t pulse_start_us;              /*!< Timestamp of the current rising edge, used to measure pulse width (us) */
    uint32_t period_us;                   /*!< Filtered zero-crossing half-period in microseconds (0 until measured) */
    volatile uint32_t base_offset_us;     /*!< Auto-calibrated advance of the zero-cross ISR relative to T0 (us) */
    volatile uint32_t auto_base_samples;  /*!< Number of valid pulse-width measurements used for calibration */
    volatile uint8_t dimmer_value;        /*!< Current dimming level, 0 off .. 100 full */
    volatile bool is_dimmer_work;         /*!< Set when the dimmer is actively running */
    bool last_edge_was_rising;            /*!< Tracks edge polarity to reject duplicate/glitch edges */
};

/**
 * @brief Validate the AC dimmer initialization configuration.
 *
 * Checks that both GPIO pins are valid (less than `GPIO_NUM_MAX`) and are not
 * the same pin.
 *
 * @param config Pointer to the initialization configuration structure
 *
 * @return ESP_OK if the configuration is valid
 * @return ESP_ERR_INVALID_ARG if any GPIO pin is invalid, or both pins are the same
 */
static esp_err_t _zh_ac_dimmer_validate_config(const zh_ac_dimmer_init_config_t *config);

/**
 * @brief Initialize GPIO pins for TRIAC control and zero-crossing detection.
 *
 * Configures the TRIAC GPIO as an output (pulled low) and the zero-crossing
 * GPIO as an input with pull-down and both-edge interrupt. Installs the
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
 * Creates a new gptimer with a 1 MHz resolution (1 us steps), registers the
 * alarm callback that fires the TRIAC, and enables the timer.
 * The timer is enabled but not started. It is stopped, reset and armed from
 * the rising-edge branch of the zero-crossing ISR, then started when a delay
 * is required.
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
 * Executed on both edges of the zero-crossing signal. The edge polarity is
 * determined from the current GPIO level:
 *
 * - On a falling edge (falling edge follows a rising edge of the same pulse),
 *   the width of the zero-crossing pulse is measured and fed into the
 *   auto-calibration EMA for `base_offset_us`. The value represents the
 *   advance of the rising edge relative to the physical mains zero crossing
 *   and is computed as `pulse_width / 2` (valid for a symmetric AC-input
 *   optocoupler). No TRIAC action is taken on falling edges.
 *
 * - On a rising edge, the interval since the previous rising edge is compared
 *   against the half-cycle range derived from `ZH_AC_DIMMER_FREQ_MIN_HZ` and
 *   `ZH_AC_DIMMER_FREQ_MAX_HZ`:
 *
 *   - An interval shorter than the minimum is treated as spurious and ignored.
 *   - An interval longer than the maximum resets the timing state: the period
 *     filter and the calibration sample counter are cleared, the TRIAC gate is
 *     set low, and only the timestamp is kept.
 *   - The first rising edge only stores its timestamp and sets the TRIAC gate low.
 *
 *   Each accepted rising edge updates the low-pass filtered half-period and
 *   resets the TRIAC gate low, then behaves according to the running state and
 *   the dimming level:
 *
 *   - When not running or at dimming = 0: the timer is stopped and reset, and
 *     the gate stays low.
 *   - When the computed delay is not positive: the TRIAC fires immediately
 *     (gate set high).
 *   - Otherwise: the gptimer alarm fires after a delay calculated from the
 *     filtered period, the dimming value and the auto-calibrated base offset,
 *     clamped to leave a guard band (period / 20, at least 5 us) before the
 *     end of the half-cycle. The delay decreases as the dimming value
 *     increases, so the TRIAC fires earlier and the load receives more power.
 *
 * @param arg Pointer to the dimmer handle (passed from GPIO ISR)
 */
static void _zh_ac_dimmer_isr_handler(void *arg);

/**
 * @brief gptimer alarm callback that fires the TRIAC.
 *
 * Stops the timer and clears its count, then sets the TRIAC gate high to
 * trigger conduction, but only while the dimmer is running and the dimming
 * level is above zero. Otherwise the gate is left unchanged until the next
 * zero-crossing event.
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
    (*handle)->is_dimmer_work = false;
    ZH_ERROR_CHECK_CONT(gpio_isr_handler_remove((*handle)->zero_cross_gpio) == ESP_OK, NULL, "AC dimmer deinitialization failed. Remove GPIO isr handler failed.");
    ZH_ERROR_CHECK_CONT(gpio_reset_pin((*handle)->triac_gpio) == ESP_OK, NULL, "AC dimmer deinitialization failed. Reset GPIO failed.");
    ZH_ERROR_CHECK_CONT(gpio_reset_pin((*handle)->zero_cross_gpio) == ESP_OK, NULL, "AC dimmer deinitialization failed. Reset GPIO failed.");
    ZH_ERROR_CHECK_CONT(gptimer_stop((*handle)->dimmer_timer_handle) == ESP_OK, NULL, "AC dimmer deinitialization failed. Timer stop fail.");
    ZH_ERROR_CHECK_CONT(gptimer_disable((*handle)->dimmer_timer_handle) == ESP_OK, NULL, "AC dimmer deinitialization failed. Timer disable fail.");
    ZH_ERROR_CHECK_CONT(gptimer_del_timer((*handle)->dimmer_timer_handle) == ESP_OK, NULL, "AC dimmer deinitialization failed. Timer delete fail.");

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
        .intr_type = GPIO_INTR_ANYEDGE,
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
        .resolution_hz = 1000000,
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
    uint64_t now_us = esp_timer_get_time();
    if (gpio_get_level(handle->zero_cross_gpio) == 0)
    {
        if (handle->last_edge_was_rising)
        {
            uint64_t pulse_width = now_us - handle->pulse_start_us;
            const uint64_t pulse_min_us = 4;
            const uint64_t pulse_max_us = (handle->period_us > 0) ? (handle->period_us / 4) : 2000;
            if (pulse_width >= pulse_min_us && pulse_width <= pulse_max_us)
            {
                uint32_t measured_adv = (uint32_t)(pulse_width / 2);
                if (handle->auto_base_samples == 0)
                {
                    handle->base_offset_us = measured_adv;
                }
                else
                {
                    handle->base_offset_us =
                        (handle->base_offset_us * 15 + measured_adv) / 16;
                }
                if (handle->auto_base_samples < UINT32_MAX)
                {
                    ++handle->auto_base_samples;
                }
            }
        }
        handle->last_edge_was_rising = false;
        return;
    }
    handle->last_edge_was_rising = true;
    handle->pulse_start_us = now_us;
    if (handle->prev_zero_crossing_us == 0)
    {
        handle->prev_zero_crossing_us = now_us;
        gpio_set_level(handle->triac_gpio, 0);
        return;
    }
    uint64_t delta_us = now_us - handle->prev_zero_crossing_us;
    const uint64_t period_min_us = 1000000 / (2 * ZH_AC_DIMMER_FREQ_MAX_HZ);
    const uint64_t period_max_us = 1000000 / (2 * ZH_AC_DIMMER_FREQ_MIN_HZ);
    if (delta_us < period_min_us)
    {
        return;
    }
    if (delta_us > period_max_us)
    {
        handle->prev_zero_crossing_us = now_us;
        handle->period_us = 0;
        handle->auto_base_samples = 0;
        gpio_set_level(handle->triac_gpio, 0);
        return;
    }
    if (handle->period_us == 0)
    {
        handle->period_us = (uint32_t)delta_us;
    }
    else
    {
        handle->period_us = (handle->period_us * 7 + (uint32_t)delta_us) / 8;
    }
    handle->prev_zero_crossing_us = now_us;
    gpio_set_level(handle->triac_gpio, 0);
    if (handle->is_dimmer_work == false || handle->dimmer_value == 0)
    {
        gptimer_stop(handle->dimmer_timer_handle);
        gptimer_set_raw_count(handle->dimmer_timer_handle, 0);
        return;
    }
    uint32_t period = handle->period_us;
    uint32_t base = handle->base_offset_us;
    if (base >= period)
    {
        base = period;
    }
    int32_t t_fire = (int32_t)(((uint64_t)period * (100 - handle->dimmer_value)) / 100);
    int32_t delay_us = t_fire + (int32_t)base;
    if (delay_us <= 0)
    {
        gpio_set_level(handle->triac_gpio, 1);
        return;
    }
    int32_t guard_us = (int32_t)(period / 20);
    if (guard_us < 5)
    {
        guard_us = 5;
    }
    if (delay_us > (int32_t)period - guard_us)
    {
        delay_us = (int32_t)period - guard_us;
    }
    gptimer_stop(handle->dimmer_timer_handle);
    gptimer_set_raw_count(handle->dimmer_timer_handle, 0);
    gptimer_alarm_config_t alarm_config = {0};
    alarm_config.alarm_count = (uint32_t)delay_us;
    alarm_config.flags.auto_reload_on_alarm = false;
    gptimer_set_alarm_action(handle->dimmer_timer_handle, &alarm_config);
    gptimer_start(handle->dimmer_timer_handle);
}

static bool _zh_ac_dimmer_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)edata;
    zh_ac_dimmer_handle_t *handle = (zh_ac_dimmer_handle_t *)user_ctx;
    gptimer_stop(timer);
    gptimer_set_raw_count(timer, 0);
    if (handle->is_dimmer_work && handle->dimmer_value > 0)
    {
        gpio_set_level(handle->triac_gpio, 1);
    }
    return false;
}