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

static gptimer_handle_t _dimmer_timer = NULL;
static gptimer_alarm_config_t _alarm_config = {0};

static zh_ac_dimmer_init_config_t _init_config = {0};
static volatile uint64_t _prev_us = 0;
static volatile uint32_t _current_period_us = 0;
static volatile uint32_t _prev_period_us = 0;
static volatile uint16_t _zero_cross_us = 0;
static volatile uint8_t _dimmer_value = 0;
static volatile bool _is_dimmer_work = false;
static bool _is_initialized = false;

static esp_err_t _zh_ac_dimmer_validate_config(const zh_ac_dimmer_init_config_t *config);
static esp_err_t _zh_ac_dimmer_gpio_init(const zh_ac_dimmer_init_config_t *config);
static esp_err_t _zh_ac_dimmer_timer_init(void);
static void _zh_ac_dimmer_isr_handler(void *arg);
static bool _zh_ac_dimmer_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);

esp_err_t zh_ac_dimmer_init(const zh_ac_dimmer_init_config_t *config)
{
    ZH_LOGI("AC dimmer initialization started.");
    ZH_ERROR_CHECK(_is_initialized == false, ESP_ERR_INVALID_STATE, NULL, "AC dimmer initialization failed. AC dimmer is already initialized.");
    esp_err_t err = _zh_ac_dimmer_validate_config(config);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer initialization failed. Initial configuration check failed.");
    err = _zh_ac_dimmer_gpio_init(config);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer initialization failed. GPIO initialization failed.");
    err = _zh_ac_dimmer_timer_init();
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer initialization failed. Timer initialization failed.");
    _init_config = *config;
    _is_initialized = true;
    ZH_LOGI("AC dimmer initialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_deinit(void)
{
    ZH_LOGI("AC dimmer deinitialization started.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, NULL, "AC dimmer deinitialization failed. AC dimmer is not initialized.");
    gptimer_stop(_dimmer_timer);
    gptimer_disable(_dimmer_timer);
    gptimer_del_timer(_dimmer_timer);
    gpio_isr_handler_remove(_init_config.zero_cross_gpio);
    gpio_uninstall_isr_service();
    gpio_reset_pin(_init_config.triac_gpio);
    gpio_reset_pin(_init_config.zero_cross_gpio);
    _is_dimmer_work = false;
    _is_initialized = false;
    _dimmer_timer = NULL;
    _dimmer_value = 0;
    _current_period_us = 0;
    _prev_period_us = 0;
    _zero_cross_us = 0;
    _prev_us = 0;
    ZH_LOGI("AC dimmer deinitialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_start(void)
{
    ZH_LOGI("AC dimmer start begin.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, NULL, "AC dimmer start failed. AC dimmer is not initialized.");
    _is_dimmer_work = true;
    ZH_LOGI("AC dimmer start completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_stop(void)
{
    ZH_LOGI("AC dimmer stop begin.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, NULL, "AC dimmer stop failed. AC dimmer is not initialized.");
    _is_dimmer_work = false;
    ZH_LOGI("AC dimmer stop completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_set(uint8_t value)
{
    ZH_LOGI("AC dimmer setup begin.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, NULL, "AC dimmer stop failed. AC dimmer is not initialized.");
    ZH_ERROR_CHECK(value <= 100, ESP_ERR_INVALID_ARG, NULL, "AC dimmer setup failed. Dimming value invalid.");
    _dimmer_value = value;
    ZH_LOGI("AC dimmer setup completed successfully.");
    return ESP_OK;
}

static esp_err_t _zh_ac_dimmer_validate_config(const zh_ac_dimmer_init_config_t *config)
{
    ZH_ERROR_CHECK(config != NULL, ESP_ERR_INVALID_ARG, NULL, "Initial config is NULL.");
    ZH_ERROR_CHECK((config->zero_cross_gpio >= GPIO_NUM_0 && config->zero_cross_gpio < GPIO_NUM_MAX), ESP_ERR_INVALID_ARG, NULL, "Zero cross GPIO invalid.");
    ZH_ERROR_CHECK((config->triac_gpio >= GPIO_NUM_0 && config->triac_gpio < GPIO_NUM_MAX), ESP_ERR_INVALID_ARG, NULL, "Triac GPIO invalid.");
    ZH_ERROR_CHECK((config->zero_cross_gpio != config->triac_gpio), ESP_ERR_INVALID_ARG, NULL, "Both GPIO is same.");
    return ESP_OK;
}

static esp_err_t _zh_ac_dimmer_gpio_init(const zh_ac_dimmer_init_config_t *config)
{
    gpio_config_t triac_gpio_config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << config->triac_gpio),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    esp_err_t err = gpio_config(&triac_gpio_config);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Triac GPIO configuration failed.");
    gpio_set_level(config->triac_gpio, 0);
    gpio_config_t zero_cross_gpio_config = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << config->zero_cross_gpio),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    err = gpio_config(&zero_cross_gpio_config);
    ZH_ERROR_CHECK(err == ESP_OK, err, gpio_reset_pin(config->triac_gpio), "Zero cross GPIO configuration failed.");
    err = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    ZH_ERROR_CHECK(err == ESP_OK, err, gpio_reset_pin(config->triac_gpio); gpio_reset_pin(config->zero_cross_gpio), "Failed install isr service.")
    err = gpio_isr_handler_add(config->zero_cross_gpio, _zh_ac_dimmer_isr_handler, NULL);
    ZH_ERROR_CHECK(err == ESP_OK, err, gpio_uninstall_isr_service(); gpio_reset_pin(config->triac_gpio); gpio_reset_pin(config->zero_cross_gpio), "Failed add isr handler.");
    return ESP_OK;
}

static esp_err_t _zh_ac_dimmer_timer_init(void)
{
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1 * 1000 * 1000,
    };
    esp_err_t err = gptimer_new_timer(&timer_config, &_dimmer_timer);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Failed create dimmer timer.");
    gptimer_event_callbacks_t cbs = {
        .on_alarm = _zh_ac_dimmer_timer_on_alarm_cb,
    };
    err = gptimer_register_event_callbacks(_dimmer_timer, &cbs, NULL);
    ZH_ERROR_CHECK(err == ESP_OK, err, gptimer_del_timer(_dimmer_timer), "Failed register dimmer timer event callbacks.");
    err = gptimer_enable(_dimmer_timer);
    ZH_ERROR_CHECK(err == ESP_OK, err, gptimer_register_event_callbacks(_dimmer_timer, NULL, NULL); gptimer_del_timer(_dimmer_timer), "Failed enable dimmer timer.");
    return ESP_OK;
}

static void IRAM_ATTR _zh_ac_dimmer_isr_handler(void *arg)
{
    if (_is_dimmer_work == false)
    {
        return;
    }
    gpio_set_level(_init_config.triac_gpio, 0);
    uint64_t _current_us = esp_timer_get_time();
    _current_period_us = (uint32_t)(_current_us - _prev_us);
    _prev_us = _current_us;
    if (_current_period_us < 1000)
    {
        if (_current_period_us > 50)
        {
            _zero_cross_us = (uint16_t)_current_period_us;
        }
        _current_period_us = _prev_period_us;
    }
    _prev_period_us = _current_period_us;
    if (_dimmer_value != 0)
    {
        if (_dimmer_value == 100)
        {
            gpio_set_level(_init_config.triac_gpio, 1);
            return;
        }
        _alarm_config.alarm_count = (uint64_t)(((_current_period_us / 110) * (100 - _dimmer_value)) + _zero_cross_us);
        _alarm_config.flags.auto_reload_on_alarm = false;
        gptimer_set_alarm_action(_dimmer_timer, &_alarm_config); // This function is allowed to run within ISR context.
        gptimer_start(_dimmer_timer);                            // This function is allowed to run within ISR context.
    }
}

static bool IRAM_ATTR _zh_ac_dimmer_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    gpio_set_level(_init_config.triac_gpio, 1);
    gptimer_stop(_dimmer_timer);
    gptimer_set_raw_count(_dimmer_timer, 0);
    return true;
}