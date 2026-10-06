#ifndef HAL_HARDWARE_PERIPHERAL_MANAGER_IMPL_H
#define HAL_HARDWARE_PERIPHERAL_MANAGER_IMPL_H

#include "hal/hardware_peripheral_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @file hardware_peripheral_manager_impl.h
 * @brief ESP32 implementation of HardwarePeripheralManager.
 *
 * Uses FreeRTOS semaphores for shared GPIO pin arbitration between
 * the SD card chip-select and the LED. Directly calls ESP32 APIs
 * for temperature reading. Excluded from native builds.
 */

/**
 * @class HardwarePeripheralManagerImpl
 * @brief ESP32 concrete implementation of HardwarePeripheralManager.
 *
 * Manages the shared GPIO 21 pin (LED/SD CS) using a FreeRTOS mutex
 * for arbitration between SD card transactions and LED updates.
 * Provides ESP32 core temperature reading.
 */
class HardwarePeripheralManagerImpl : public HardwarePeripheralManager
{
public:
    /**
     * @brief Constructs the ESP32 hardware peripheral manager.
     *
     * Creates the FreeRTOS mutex for shared pin arbitration.
     */
    HardwarePeripheralManagerImpl();

    /**
     * @brief Drives the shared GPIO pin for SD card chip-select signaling.
     *
     * Acquires/releases the shared mutex as needed to ensure exclusive
     * access to the shared pin during SD transactions.
     *
     * @param signal The desired CS signal state.
     */
    void cs_signal(peripherals::SdSignal signal) override;

    /**
     * @brief Updates the LED pin based on the desired LED state.
     *
     * Non-blocking: skips the update if the SD mutex is held (SD
     * transaction in progress). SD takes priority over LED.
     *
     * @param led_on true to turn LED on, false to turn LED off.
     */
    void update_led(bool led_on) override;

    /**
     * @brief Reads the ESP32 core temperature.
     *
     * Serialized against concurrent callers: the sensor's read
     * sequence is stateful and must not interleave across tasks.
     *
     * @return Temperature in degrees Celsius.
     */
    float read_temperature() override;

    /**
     * @brief Configures the shared GPIO pin as an output.
     */
    void setup_shared_pin() override;

private:
    /** FreeRTOS mutex protecting the shared GPIO pin (LED/SD CS). */
    SemaphoreHandle_t m_sd_active_mutex{nullptr};

    /** FreeRTOS mutex serializing core temperature reads across tasks. */
    SemaphoreHandle_t m_temp_mutex{nullptr};
};

#endif // HAL_HARDWARE_PERIPHERAL_MANAGER_IMPL_H