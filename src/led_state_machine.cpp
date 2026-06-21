#include "led_state_machine.h"

LedStateMachine::LedStateMachine(TimeService &time)
    : m_time(time)
{
}

void LedStateMachine::set_mode(LedMode mode, uint32_t on_ms, uint32_t off_ms)
{
    m_mode = mode;
    m_on_duration_ms = on_ms;
    m_off_duration_ms = off_ms;
    m_last_state_change_ms = m_time.now();
    m_blink_phase_on = true;
}

bool LedStateMachine::tick()
{
    switch (m_mode)
    {
        case LedMode::OFF:
            return false;

        case LedMode::ON:
            return true;

        case LedMode::PULSE_ON:
        {
            uint32_t elapsed = TimeService::elapsed_ms(m_last_state_change_ms, m_time.now());
            if (elapsed > m_on_duration_ms)
            {
                m_mode = LedMode::OFF;
                return false;
            }
            return true;
        }

        case LedMode::BLINK:
        {
            uint32_t elapsed = TimeService::elapsed_ms(m_last_state_change_ms, m_time.now());

            if (m_blink_phase_on)
            {
                if (elapsed > m_on_duration_ms)
                {
                    m_blink_phase_on = false;
                    m_last_state_change_ms = m_time.now();
                    return false;
                }
                return true;
            }
            else
            {
                if (elapsed > m_off_duration_ms)
                {
                    m_blink_phase_on = true;
                    m_last_state_change_ms = m_time.now();
                    return true;
                }
                return false;
            }
        }

        default:
            return false;
    }
}
