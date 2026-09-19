#ifndef HAL_HARDWARE_PERIPHERAL_MANAGER_H
#define HAL_HARDWARE_PERIPHERAL_MANAGER_H

#include "sd_controller.h"

/**
 * @file hardware_peripheral_manager.h
 * @brief Abstract HAL interface for shared hardware peripheral management.
 *
 * Provides an abstraction for hardware peripherals that are shared between
 * multiple subsystems (LED and SD card share GPIO 21 on the ESP32S3 Sense).
 * This interface decouples the application layer from platform-specific
 * FreeRTOS semaphores, mutexes, and temperature-reading APIs.
 */

/**
 * @class HardwarePeripheralManager
 * @brief Abstract interface for managing shared hardware peripherals.
 *
 * Pure virtual interface with no platform dependencies. Concrete
 * implementations handle FreeRTOS mutexes and ESP32-specific APIs.
 */
class HardwarePeripheralManager
{
public:
    virtual ~HardwarePeripheralManager() = default;

    /**
     * @brief Drives the shared GPIO pin for SD card chip-select signaling.
     *
     * Acquires/releases the shared mutex as needed to ensure exclusive
     * access to the shared pin during SD transactions.
     *
     * @param signal The desired CS signal state.
     */
    virtual void cs_signal(peripherals::SdSignal signal) = 0;

    /**
     * @brief Updates the LED pin based on the desired LED state.
     *
     * Non-blocking: skips the update if the SD mutex is held (SD
     * transaction in progress). SD takes priority over LED.
     *
     * @param led_on true to turn LED on, false to turn LED off.
     */
    virtual void update_led(bool led_on) = 0;

    /**
     * @brief Reads the ESP32 core temperature.
     *
     * @return Temperature in degrees Celsius.
     */
    virtual float read_temperature() = 0;

    /**
     * @brief Configures the shared GPIO pin as an output.
     */
    virtual void setup_shared_pin() = 0;
};

#endif // HAL_HARDWARE_PERIPHERAL_MANAGER_H