#ifndef MOCK_HARDWARE_PERIPHERAL_MANAGER_H
#define MOCK_HARDWARE_PERIPHERAL_MANAGER_H

#include "hal/hardware_peripheral_manager.h"

/**
 * @file mock_hardware_peripheral_manager.h
 * @brief Configurable mock HardwarePeripheralManager for native tests.
 *
 * Provides a configurable core-temperature reading; the pin/LED
 * operations are no-ops (there is no shared pin in native tests).
 */
class MockHardwarePeripheralManager : public HardwarePeripheralManager
{
    public:
        /** Temperature returned by read_temperature(). */
        float temperature_c{48.5f};

        void cs_signal(peripherals::SdSignal signal) override
        {
            (void)signal;
        }

        void update_led(bool led_on) override
        {
            (void)led_on;
        }

        float read_temperature() override
        {
            return temperature_c;
        }

        void setup_shared_pin() override {}
};

#endif // MOCK_HARDWARE_PERIPHERAL_MANAGER_H
