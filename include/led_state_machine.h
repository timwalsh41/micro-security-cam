#ifndef LED_STATE_MACHINE_H
#define LED_STATE_MACHINE_H

#include <cstdint>
#include "time_service.h"

/**
 * @file led_state_machine.h
 * @brief Pure LED state machine logic, testable on native.
 *
 * Produces a desired LED output (on/off) based on the current mode and
 * elapsed time. No hardware side effects — callers are responsible for
 * applying the returned state to a GPIO pin.
 */

/**
 * @brief LED operating modes.
 */
enum class LedMode : uint8_t
{
    OFF,        ///< LED permanently off
    ON,         ///< LED permanently on
    PULSE_ON,   ///< LED on for a fixed duration, then off
    BLINK       ///< LED alternates on/off at configurable intervals
};

/**
 * @class LedStateMachine
 * @brief State machine that determines the desired LED state.
 *
 * Uses an injected TimeService for time acquisition, enabling
 * deterministic testing with a mock.
 */
class LedStateMachine
{
public:
    /**
     * @brief Constructs a LedStateMachine with the given time source.
     * @param time Reference to a TimeService used for elapsed-time calculations.
     */
    explicit LedStateMachine(TimeService &time);

    /**
     * @brief Sets the operating mode and associated timing parameters.
     *
     * For PULSE_ON: the LED will be on for @p on_ms, then turn off.
     * For BLINK: the LED will alternate on for @p on_ms, off for @p off_ms.
     * For OFF/ON: timing parameters are ignored.
     *
     * @param mode The desired LED mode.
     * @param on_ms Duration in milliseconds the LED should be on (PULSE_ON / BLINK).
     * @param off_ms Duration in milliseconds the LED should be off (BLINK only).
     */
    void set_mode(LedMode mode, uint32_t on_ms = 0, uint32_t off_ms = 0);

    /**
     * @brief Advances the state machine and returns the desired LED state.
     *
     * @return true if the LED should be ON, false if it should be OFF.
     */
    bool tick();

private:
    TimeService &m_time;
    LedMode m_mode{LedMode::OFF};
    uint32_t m_on_duration_ms{0};
    uint32_t m_off_duration_ms{0};
    uint32_t m_last_state_change_ms{0};
    bool m_blink_phase_on{true};
};

#endif // LED_STATE_MACHINE_H
