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
volatile static uint64_t _prev_us = 0;
volatile static uint8_t _dimmer_value = 0;
volatile static bool _is_dimmer_work = false;
volatile static bool _is_initialized = false;

static esp_err_t _zh_ac_dimmer_validate_config(const zh_ac_dimmer_init_config_t *config);
static esp_err_t _zh_ac_dimmer_gpio_init(const zh_ac_dimmer_init_config_t *config);
static esp_err_t _zh_ac_dimmer_timer_init(void);
static void _zh_ac_dimmer_isr_handler(void *arg);
static bool _zh_ac_dimmer_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);

esp_err_t zh_ac_dimmer_init(const zh_ac_dimmer_init_config_t *config) // -V2008
{
    ZH_LOGI("AC dimmer initialization started.");
    ZH_ERROR_CHECK(config != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer initialization failed. Invalid argument.");
    ZH_ERROR_CHECK(_is_initialized == false, ESP_ERR_INVALID_STATE, NULL, "AC dimmer initialization failed. AC dimmer is already initialized.");
    esp_err_t err = _zh_ac_dimmer_validate_config(config);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer initialization failed. Initial configuration check failed.");
    err = _zh_ac_dimmer_gpio_init(config);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer initialization failed. GPIO initialization failed.");
    err = _zh_ac_dimmer_timer_init();
    ZH_ERROR_CHECK(err == ESP_OK, err, err = gpio_isr_handler_remove((gpio_num_t)config->zero_cross_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Remove GPIO isr handler failed."); err = gpio_reset_pin((gpio_num_t)config->triac_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Reset GPIO failed."); err = gpio_reset_pin((gpio_num_t)config->zero_cross_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Reset GPIO failed."), "AC dimmer initialization failed. Timer initialization failed.");
    _init_config = *config;
    _is_initialized = true;
    ZH_LOGI("AC dimmer initialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_deinit(void) // -V2008
{
    ZH_LOGI("AC dimmer deinitialization started.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, NULL, "AC dimmer deinitialization failed. AC dimmer is not initialized.");
    esp_err_t err = gptimer_stop(_dimmer_timer);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer deinitialization failed. Timer stop fail.");
    err = gptimer_disable(_dimmer_timer);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer deinitialization failed. Timer disable fail.");
    err = gptimer_del_timer(_dimmer_timer);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer deinitialization failed. Timer delete fail.");
    err = gpio_isr_handler_remove((gpio_num_t)_init_config.zero_cross_gpio);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer deinitialization failed. Remove GPIO isr handler failed.");
    err = gpio_reset_pin((gpio_num_t)_init_config.triac_gpio);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer deinitialization failed. Reset GPIO failed.")
    err = gpio_reset_pin((gpio_num_t)_init_config.zero_cross_gpio);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "AC dimmer deinitialization failed. Reset GPIO failed.")
    _is_dimmer_work = false;
    _is_initialized = false;
    _dimmer_timer = NULL;
    _dimmer_value = 0;
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
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, NULL, "AC dimmer setup failed. AC dimmer is not initialized.");
    ZH_ERROR_CHECK(value <= 100, ESP_ERR_INVALID_ARG, NULL, "AC dimmer setup failed. Dimming value invalid.");
    _dimmer_value = value;
    ZH_LOGI("AC dimmer setup completed successfully.");
    return ESP_OK;
}

esp_err_t zh_ac_dimmer_get(uint8_t *value)
{
    ZH_LOGI("AC dimmer getting status begin.");
    ZH_ERROR_CHECK(value != NULL, ESP_ERR_INVALID_ARG, NULL, "AC dimmer getting status failed. Invalid argument.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, NULL, "AC dimmer getting status failed. AC dimmer is not initialized.");
    *value = _dimmer_value;
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

static esp_err_t _zh_ac_dimmer_gpio_init(const zh_ac_dimmer_init_config_t *config) // -V2008
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
    err = gpio_set_level((gpio_num_t)config->triac_gpio, 0);
    ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Triac GPIO set failed.");
    gpio_config_t zero_cross_gpio_config = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << config->zero_cross_gpio),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    err = gpio_config(&zero_cross_gpio_config);
    ZH_ERROR_CHECK(err == ESP_OK, err, err = gpio_reset_pin((gpio_num_t)config->triac_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Reset GPIO failed."), "Zero cross GPIO configuration failed.");
    err = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED);
    ZH_ERROR_CHECK(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, err = gpio_reset_pin((gpio_num_t)config->triac_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Reset GPIO failed."); err = gpio_reset_pin((gpio_num_t)config->zero_cross_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Reset GPIO failed."), "Failed install isr service.")
    err = gpio_isr_handler_add((gpio_num_t)config->zero_cross_gpio, _zh_ac_dimmer_isr_handler, NULL);
    ZH_ERROR_CHECK(err == ESP_OK, err, err = gpio_reset_pin((gpio_num_t)config->triac_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Reset GPIO failed."); err = gpio_reset_pin((gpio_num_t)config->zero_cross_gpio);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Reset GPIO failed."), "Failed add isr handler.");
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
    ZH_ERROR_CHECK(err == ESP_OK, err, err = gptimer_del_timer(_dimmer_timer);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Timer delete fail."), "Failed register dimmer timer event callbacks.");
    err = gptimer_enable(_dimmer_timer);
    ZH_ERROR_CHECK(err == ESP_OK, err, err = gptimer_del_timer(_dimmer_timer);
                   ZH_ERROR_CHECK(err == ESP_OK, err, NULL, "Timer delete fail."), "Failed enable dimmer timer.");
    return ESP_OK;
}

static void IRAM_ATTR _zh_ac_dimmer_isr_handler(void *arg)
{
    uint64_t _current_us = esp_timer_get_time();
    if (_current_us - _prev_us <= (1250 * 0.9)) // 90% of zero crossing period (1250 µs) at 400 Hz.
    {
        return;
    }
    gpio_set_level((gpio_num_t)_init_config.triac_gpio, 0);
    _prev_us = _current_us;
    if (_is_dimmer_work == false)
    {
        return;
    }
    if (_dimmer_value != 0)
    {
        if (_dimmer_value == 100)
        {
            gpio_set_level((gpio_num_t)_init_config.triac_gpio, 1);
            return;
        }
        _alarm_config.alarm_count = (uint64_t)((((1250 - 330) / 100) * (100 - _dimmer_value)) + 330); // 330 is 50% of zero crossing time (by logic analyser).
        _alarm_config.flags.auto_reload_on_alarm = false;
        gptimer_set_alarm_action(_dimmer_timer, &_alarm_config);
        gptimer_start(_dimmer_timer);
    }
}

static bool IRAM_ATTR _zh_ac_dimmer_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    gpio_set_level((gpio_num_t)_init_config.triac_gpio, 1);
    gptimer_stop(_dimmer_timer);
    gptimer_set_raw_count(_dimmer_timer, 0);
    return false;
}