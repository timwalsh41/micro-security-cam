#ifndef HAL_GPIO_HAL_H
#define HAL_GPIO_HAL_H

#include <cstdint>

/**
 * @file gpio_hal.h
 * @brief Hardware abstraction for GPIO operations.
 *
 * Provides platform-independent pin configuration and digital I/O.
 * Arduino-specific constants (pin numbers, logic levels) are mapped
 * here so that higher-level modules remain decoupled from the
 * framework.
 */

namespace hal
{

/** @brief GPIO pin shared by the LED and SD card chip-select. */
constexpr uint8_t SHARED_PIN = 21;
constexpr uint8_t LED_PIN = SHARED_PIN;
constexpr uint8_t SD_CS_PIN = SHARED_PIN;

/** @brief Logic-low level. */
constexpr uint8_t PIN_LOW = 0;

/** @brief Logic-high level. */
constexpr uint8_t PIN_HIGH = 1;

/**
 * @brief Configures a GPIO pin as an output.
 *
 * @param pin The GPIO pin number.
 */
void set_pin_as_output(uint8_t pin);

/**
 * @brief Configures a GPIO pin as an input.
 *
 * @param pin The GPIO pin number.
 */
void set_pin_as_input(uint8_t pin);

/**
 * @brief Writes a digital value to a GPIO pin.
 *
 * @param pin   The GPIO pin number.
 * @param value The digital value to write (hal::PIN_LOW or hal::PIN_HIGH).
 */
void digital_write(uint8_t pin, uint8_t value);

} // namespace hal

#endif // HAL_GPIO_HAL_H
