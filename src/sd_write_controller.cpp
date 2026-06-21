#include "sd_write_controller.h"

// ============================================================
// Construction
// ============================================================

SdWriteController::SdWriteController(TimeService &time, hal::SdCardHal &sd_hal)
    : m_time(time)
    , m_sd_hal(sd_hal)
{
}

// ============================================================
// Public interface
// ============================================================

peripherals::SdSignal SdWriteController::tick()
{
    uint32_t now = m_time.now();

    switch (m_state)
    {
        case SdState::IDLE:
        {
            return peripherals::SdSignal::IDLE;
        }

        case SdState::PRE_DEACTIVATE_CS:
        {
            // Wait for the minimum de-assertion time before re-asserting CS
            if (TimeService::elapsed_ms(m_last_state_change_ms, now) > DEACTIVATE_TIME_MS)
            {
                set_state(SdState::ACTIVATE_CS);
                return peripherals::SdSignal::CS_ACTIVE;    // CS active
            }
            return peripherals::SdSignal::CS_INACTIVE;     // CS inactive
        }

        case SdState::ACTIVATE_CS:
        {
            // Wait for the minimum assertion time before starting I/O
            if (TimeService::elapsed_ms(m_last_state_change_ms, now) > ACTIVATE_TIME_MS)
            {
                set_state(SdState::WRITING);
            }
            return peripherals::SdSignal::CS_ACTIVE;        // CS remains active
        }

        case SdState::WRITING:
        {
            // Perform the file I/O while CS is asserted.
            // close() is always called to ensure proper resource cleanup,
            // even if open() fails.
            if (!m_sd_hal.open(m_write_path))
            {
                m_write_result = SdWriteResult::FILE_OPEN_FAILED;
            }
            else
            {
                if (!m_sd_hal.write(m_write_data, m_write_len))
                {
                    m_write_result = SdWriteResult::FILE_WRITE_ERROR;
                }
            }
            m_sd_hal.close();

            set_state(SdState::STOP_WRITE);
            return peripherals::SdSignal::CS_ACTIVE;        // CS remains active through STOP_WRITE entry
        }

        case SdState::STOP_WRITE:
        {
            // Transition: begin de-assertion of CS
            set_state(SdState::POST_DEACTIVATE_CS);
            return peripherals::SdSignal::CS_INACTIVE;      // CS transitions to inactive
        }

        case SdState::POST_DEACTIVATE_CS:
        {
            // Wait for the minimum de-assertion time before returning to idle
            if (TimeService::elapsed_ms(m_last_state_change_ms, now) > DEACTIVATE_TIME_MS)
            {
                set_state(SdState::IDLE);
                return peripherals::SdSignal::IDLE;          // yielding to LED
            }
            return peripherals::SdSignal::CS_INACTIVE;      // CS remains inactive
        }

        default:
        {
            return peripherals::SdSignal::IDLE;
        }
    }
}

void SdWriteController::start_write(const char *path, uint8_t *data, size_t len)
{
    m_write_path = path;
    m_write_data = data;
    m_write_len = len;
    m_write_result = SdWriteResult::SUCCESS;

    set_state(SdState::PRE_DEACTIVATE_CS);
}

SdWriteResult SdWriteController::write_synchronous(const char *path, uint8_t *data,
                                                     size_t len, uint32_t timeout_ms)
{
    start_write(path, data, len);

    uint32_t start_time = m_time.now();

    while (m_state != SdState::IDLE)
    {
        uint32_t now = m_time.now();

        if (TimeService::elapsed_ms(start_time, now) > timeout_ms)
        {
            m_state = SdState::IDLE;
            return SdWriteResult::TIMEOUT;
        }

        tick();
    }

    return m_write_result;
}

hal::SdInitResult SdWriteController::init_card()
{
    return m_sd_hal.init();
}

void SdWriteController::set_state(SdWriteController::SdState next_state)
{
    m_state = next_state;
    m_last_state_change_ms = m_time.now();
}
