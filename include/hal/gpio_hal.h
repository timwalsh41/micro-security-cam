#ifndef HAL_GPIO_HAL_H
#define HAL_GPIO_HAL_H

#include <cstdint>

/**
 * @file gpio_hal.h
 * @brief Thin wrappers around Arduino GPIO functions.
 *
 * Provides forwarding wrappers for pinMode() and digitalWrite().
 * No logic — just indirection to enable future HAL-level mockability
 * if hardware test needs arise.
 */

namespace hal
{

/**
 * @brief Configures the mode of a GPIO pin.
 *
 * @param pin  The GPIO pin number.
 * @param mode The pin mode (OUTPUT, INPUT, INPUT_PULLUP, etc.).
 */
void pin_mode(uint8_t pin, uint8_t mode);

/**
 * @brief Writes a digital value to a GPIO pin.
 *
 * @param pin   The GPIO pin number.
 * @param value The digital value to write (HIGH or LOW).
 */
void digital_write(uint8_t pin, uint8_t value);

} // namespace hal

#endif // HAL_GPIO_HAL_H
