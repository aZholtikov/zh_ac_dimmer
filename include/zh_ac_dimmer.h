/**
 * @file zh_ac_dimmer.h
 *
 * @brief Header file for AC dimmer driver based on zero-crossing detection and TRIAC control.
 *
 * This module provides an interface for controlling AC power via phase-angle
 * dimming using ESP-IDF gptimer and GPIO peripherals. It detects the AC
 * mains zero-crossing point and fires the TRIAC with a programmable delay
 * to regulate the average power delivered to the load.
 *
 * Key features:
 * - Zero-crossing detection via GPIO interrupt
 * - TRIAC gate control through ESP-IDF gptimer
 * - Configurable dimming level (0–100 %)
 * - Thread-safe start/stop/set/get API
 *
 * @note The driver assumes a 400 Hz AC mains frequency (1250 µs period).
 *       Adjust the timing constants in `zh_ac_dimmer.c` for other frequencies.
 * @warning This module interacts with mains voltage. Incorrect wiring or
 *          insufficient isolation can cause electric shock or hardware damage.
 *          Ensure proper opto-isolation and safety compliance before deployment.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"

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
     * positive-edge interrupt), installs the ISR service, and creates the
     * gptimer instance.
     *
     * @param[in] config Pointer to the initialization configuration structure.
     *                   May point to a temporary variable (not stored internally)
     * @param[out] handle Pointer to receive the created dimmer handle (must be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle is NULL or GPIO pins are invalid
     * @return ESP_ERR_INVALID_STATE if a dimmer instance is already initialized
     * @return ESP_ERR_NO_MEM if memory allocation failed
     * @return ESP_FAIL if GPIO, ISR service, or timer initialization failed
     */
    esp_err_t zh_ac_dimmer_init(const zh_ac_dimmer_init_config_t *config, zh_ac_dimmer_handle_t **handle);

    /**
     * @brief Deinitialize the AC dimmer driver.
     *
     * Stops and deletes the gptimer, removes the GPIO ISR handler, resets
     * the GPIO pins to input mode, frees the dimmer handle, and sets the
     * output handle to NULL.
     *
     * @param[in,out] handle Pointer to the dimmer handle (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle or *handle is NULL
     * @return ESP_FAIL if timer or GPIO cleanup failed
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
     * @param[in,out] handle Pointer to the dimmer handle (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle or *handle is NULL
     */
    esp_err_t zh_ac_dimmer_start(zh_ac_dimmer_handle_t **handle);

    /**
     * @brief Stop the AC dimmer.
     *
     * Disables TRIAC firing on zero-crossing events. The dimmer can be
     * restarted with `zh_ac_dimmer_start()`.
     *
     * @param[in,out] handle Pointer to the dimmer handle (must not be NULL)
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
     * @param[in] value Dimming level from 0 (off) to 100 (full brightness)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle, *handle, or value is invalid
     */
    esp_err_t zh_ac_dimmer_set(zh_ac_dimmer_handle_t **handle, uint8_t value);

    /**
     * @brief Get the current AC dimmer brightness level.
     *
     * @param[in] handle Pointer to the dimmer handle (must not be NULL)
     * @param[out] value Pointer to receive the current dimming level (0–100) (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle, *handle, or value is NULL
     */
    esp_err_t zh_ac_dimmer_get(zh_ac_dimmer_handle_t **handle, uint8_t *value);

#ifdef __cplusplus
}
#endif