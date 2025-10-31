/**
 * @file zh_ac_dimmer.h
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"

/**
 * @brief AC dimmer initial default values.
 */
#define ZH_AC_DIMMER_INIT_CONFIG_DEFAULT() \
    {                                      \
        .zero_cross_gpio = GPIO_NUM_MAX,   \
        .triac_gpio = GPIO_NUM_MAX}

#ifdef __cplusplus

extern "C"
{
#endif
    /**
     * @brief Structure for initial initialization of AC dimmer.
     */
    typedef struct
    {
        uint8_t zero_cross_gpio; /*!< Zero cross GPIO. */
        uint8_t triac_gpio;      /*!< Triac GPIO. */
    } zh_ac_dimmer_init_config_t;

    /**
     * @brief Initialize AC dimmer.
     *
     * @param[in] config Pointer to AC dimmer initialized configuration structure. Can point to a temporary variable.
     *
     * @note Before initialize the AC dimmer recommend initialize zh_ac_dimmer_init_config_t structure with default values.
     *
     * @code zh_ac_dimmer_init_config_t config = ZH_AC_DIMMER_INIT_CONFIG_DEFAULT @endcode
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_ac_dimmer_init(const zh_ac_dimmer_init_config_t *config);

    /**
     * @brief Deinitialize AC dimmer.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_ac_dimmer_deinit(void);

    /**
     * @brief Start AC dimmer.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_ac_dimmer_start(void);

    /**
     * @brief Stop AC dimmer.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_ac_dimmer_stop(void);

    /**
     * @brief Set AC dimmer dimming value.
     *
     * @param[in] value Dimming value (0 to 100).
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_ac_dimmer_set(uint8_t value);

#ifdef __cplusplus
}
#endif