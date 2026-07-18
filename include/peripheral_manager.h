#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include "led_state_machine.h"
#include "sd_controller.h"
#include "time_service.h"
#include "hal/gpio_hal.h"

/**
 * @file peripherals.h
 * @brief Composition root that wires together all peripheral sub-components.
 *
 * PeripheralManager is a thin facade that owns and connects the
 * LedStateMachine, SdController, PinArbitrator, and HAL
 * implementations. It contains no state machine logic of its own —
 * all behavior is delegated to the sub-components.
 */

// See project notes - pin 21 is shared between LED and SD chip select
// GPIO21 is the builtin LED according to the documentation
// GPIO3 should be the chip select
#define SD_CS 21

/**
 * @class PeripheralManager
 * @brief Facade that composes and wires peripheral sub-components.
 *
 * Owns a RealTimeService, LedStateMachine, SdCardHalImpl, and
 * SdController. The tick() method ticks both state machines,
 * resolves the shared pin via PinArbitrator, and applies the result
 * to the GPIO pin.
 */
class PeripheralManager
{
    public:
        /**
         * @brief Constructs a PeripheralManager with the given dependencies.
         *
         * @param time   Reference to a TimeService for time acquisition.
         * @param sd_hal Reference to an SdCardHal for SD card I/O.
         */
        PeripheralManager(TimeService& time, hal::SdCardHal& sd_hal);
        ~PeripheralManager() = default;

        /**
         * @brief Initializes the HAL and sets the LED state machine to OFF.
         */
        void init();

        /**
         * @brief Initializes the SD card hardware.
         * @return hal::SdInitResult indicating success or the failure reason.
         */
        hal::SdInitResult init_sd_card();

        /**
         * @brief Checks if the SD card has been successfully initialized
         * @return bool true if successfully initialized
         */
        bool is_sd_card_initialized() const;

        /**
         * @brief Ticks both state machines and applies the resolved pin state.
         *
         * Call this periodically from the main loop.
         */
        void tick();

        // LED convenience methods — delegate to LedStateMachine

        /** @brief Turns the LED on permanently. */
        void turn_led_on();

        /** @brief Turns the LED off permanently. */
        void turn_led_off();

        /**
         * @brief Pulses the LED on for the given duration, then turns off.
         * @param ms Duration in milliseconds.
         */
        void pulse_led(hal::millis_t ms);

        /**
         * @brief Blinks the LED with the given on/off durations.
         * @param on_ms  Duration in milliseconds the LED is on.
         * @param off_ms Duration in milliseconds the LED is off.
         */
        void blink(hal::millis_t on_ms, hal::millis_t off_ms);

        // SD convenience methods — delegate to SdController

        /**
         * @brief Performs a synchronous SD card write with a timeout.
         * @param path      File path to write to.
         * @param data      Pointer to the data buffer.
         * @param len       Number of bytes to write.
         * @param timeout_ms Maximum time in milliseconds.
         * @return SdWriteResult indicating success or failure.
         */
        SdWriteResult write_file(const char* path, const uint8_t* data,
                                  size_t len, hal::millis_t timeout_ms,
                                  hal::FileOpenMode mode = hal::FileOpenMode::WRITE);

        /**
         * @brief Performs a synchronous SD card read with a timeout.
         * @param path        File path to read from.
         * @param buffer      Pointer to the buffer to read into.
         * @param buffer_size Maximum number of bytes the buffer can hold.
         * @param bytes_read  Output: number of bytes actually read.
         * @param timeout_ms  Maximum time in milliseconds.
         * @return SdReadResult indicating success or failure.
         */
        SdReadResult read_file(const char* path, uint8_t* buffer,
                                size_t buffer_size, size_t* bytes_read,
                                hal::millis_t timeout_ms);

        /**
         * @brief Creates a directory on the SD card with a timeout.
         * @param path      Directory path to create.
         * @param timeout_ms Maximum time in milliseconds.
         * @return SdCreateDirResult indicating success or failure.
         */
        SdCreateDirResult create_dir(const char* path,
                                     hal::millis_t timeout_ms);

    private:
        TimeService& m_time;
        hal::SdCardHal& m_sd_hal;
        LedStateMachine m_led;
        SdController m_sd;

        /**
         * @brief Applies the resolved pin state to the GPIO output.
         * @param pin_low True to drive pin LOW (active), false for HIGH.
         */
        void apply_led_state(bool pin_low);
};

#endif // PERIPHERALS_H