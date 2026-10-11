/**
 * @file zh_ac_dimmer.h
 *
 * @brief Header file for AC dimmer driver based on zero-crossing detection and TRIAC control.
 *
 * This module provides an interface for controlling AC power via phase-angle
 * dimming using ESP-IDF gptimer and GPIO peripherals. It detects the AC
 * mains zero-crossing point, measures the zero-crossing period, and fires the
 * TRIAC with a programmable delay to regulate the average power delivered to
 * the load.
 *
 * Key features:
 * - Zero-crossing detection via GPIO interrupt with period measurement
 * - TRIAC gate control through ESP-IDF gptimer
 * - Configurable dimming level (0–100 %)
 * - Automatic runtime calibration of the hardware base offset
 * - Start/stop/set/get API
 *
 * @note The supported mains frequency range is defined by
 *       `ZH_AC_DIMMER_FREQ_MIN_HZ` and `ZH_AC_DIMMER_FREQ_MAX_HZ`. The measured
 *       zero-crossing period is filtered and used to calculate the firing delay.
 * @warning This module interacts with mains voltage. Incorrect wiring or
 *          insufficient isolation can cause electric shock or hardware damage.
 *          Ensure proper opto-isolation and safety compliance before deployment.
 */

#pragma once

#include "stdint.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define ZH_AC_DIMMER_FREQ_MIN_HZ 30
#define ZH_AC_DIMMER_FREQ_MAX_HZ 500

#define ZH_AC_DIMMER_INIT_CONFIG_DEFAULT() \
    {                                      \
        .zero_cross_gpio = GPIO_NUM_MAX,   \
        .triac_gpio = GPIO_NUM_MAX}

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Opaque handle representing an AC dimmer instance.
     *
     * Internal fields are defined in `zh_ac_dimmer.c` and must not be
     * accessed directly by user code.
     */
    typedef struct _zh_ac_dimmer_handle_t zh_ac_dimmer_handle_t;

    /**
     * @brief Configuration structure for AC dimmer initialization.
     *
     * Specifies the GPIO pins used for zero-crossing detection and TRIAC
     * gate control. Both pins must be valid and different from each other.
     *
     * @note The firing-delay offset is calibrated automatically at runtime and
     *       is not part of the configuration.
     */
    typedef struct
    {
        gpio_num_t zero_cross_gpio; /*!< GPIO pin for zero-crossing detection input */
        gpio_num_t triac_gpio;      /*!< GPIO pin for TRIAC gate control output */
    } zh_ac_dimmer_init_config_t;

    /**
     * @brief Initialize the AC dimmer driver.
     *
     * Allocates memory for the dimmer handle, validates the configuration,
     * initializes GPIO pins (TRIAC as output, zero-crossing as input with
     * both-edge interrupt), installs the ISR service, and creates the gptimer.
     *
     * @param[in] config Pointer to the initialization configuration structure (may point to a
     *            temporary variable, not stored internally)
     * @param[out] handle Pointer to receive the created dimmer handle (must not be NULL and must point to NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if config or handle is NULL
     * @return ESP_ERR_INVALID_STATE if the handle pointer already points to an initialized instance
     * @return ESP_ERR_NO_MEM if memory allocation failed
     * @return ESP_FAIL if configuration validation, GPIO, ISR service, or timer initialization failed
     *
     * @note The driver supports only one instance per zero-crossing GPIO pin. Initializing a
     *       second instance on the same pin replaces the ISR handler of the first one, so each
     *       pin must be claimed by a single dimmer instance.
     */
    esp_err_t zh_ac_dimmer_init(const zh_ac_dimmer_init_config_t *config, zh_ac_dimmer_handle_t **handle);

    /**
     * @brief Deinitialize the AC dimmer driver.
     *
     * Disables TRIAC firing, removes the GPIO ISR handler, resets the GPIO
     * pins, stops, disables and deletes the gptimer, frees the dimmer handle
     * and sets the output handle to NULL. Cleanup steps are executed
     * unconditionally and their failures are only logged.
     *
     * @param[in,out] handle Pointer to the dimmer handle, set to NULL on return (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle or *handle is NULL
     *
     * @warning After calling this function, the handle is invalid and must not
     *          be used. Call `zh_ac_dimmer_init()` again to reinitialize.
     */
    esp_err_t zh_ac_dimmer_deinit(zh_ac_dimmer_handle_t **handle);

    /**
     * @brief Start the AC dimmer.
     *
     * Enables the dimmer so that zero-crossing events trigger TRIAC firing
     * according to the current dimming value.
     *
     * @param[in] handle Pointer to the dimmer handle (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle or *handle is NULL
     */
    esp_err_t zh_ac_dimmer_start(zh_ac_dimmer_handle_t **handle);

    /**
     * @brief Stop the AC dimmer.
     *
     * Disables TRIAC firing on zero-crossing events. The dimmer can be
     * restarted with `zh_ac_dimmer_start()`. The TRIAC gate stays in the
     * state left by the last half-cycle until the next zero-crossing event.
     *
     * @param[in] handle Pointer to the dimmer handle (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle or *handle is NULL
     */
    esp_err_t zh_ac_dimmer_stop(zh_ac_dimmer_handle_t **handle);

    /**
     * @brief Set the AC dimmer brightness level.
     *
     * Sets the dimming percentage that will be applied on the next
     * zero-crossing event.
     *
     * @param[in] handle Pointer to the dimmer handle (must not be NULL)
     * @param[in] value Dimming level in percent, where 0 keeps the load off and 100 fires the TRIAC immediately (must be <= 100)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle, *handle, or value is invalid
     */
    esp_err_t zh_ac_dimmer_set(zh_ac_dimmer_handle_t **handle, uint8_t value);

    /**
     * @brief Get the current AC dimmer brightness level.
     *
     * @param[in] handle Pointer to the dimmer handle (must not be NULL)
     * @param[out] value Pointer to receive the current dimming level in percent (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle, *handle, or value is NULL
     */
    esp_err_t zh_ac_dimmer_get(zh_ac_dimmer_handle_t **handle, uint8_t *value);

#ifdef __cplusplus
}
#endif