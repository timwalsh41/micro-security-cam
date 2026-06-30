#include "sd_controller.h"
#include "hal/time_hal.h"

using hal::millis_t;

// ============================================================
// Construction
// ============================================================

SdController::SdController(TimeService &time, hal::SdCardHal &sd_hal)
    : m_time(time)
    , m_sd_hal(sd_hal)
{
}

// ============================================================
// Public interface
// ============================================================

peripherals::SdSignal SdController::tick()
{
    millis_t now = m_time.now();

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
                set_state(SdState::IO_ACTIVE);
            }
            return peripherals::SdSignal::CS_ACTIVE;        // CS remains active
        }

        case SdState::IO_ACTIVE:
        {
            // Perform the file I/O while CS is asserted.
            // close() is always called to ensure proper resource cleanup,
            // even if open() fails.
            if (!m_sd_hal.open(m_io_path,
                m_operation == SdOperation::READ ? hal::FileOpenMode::READ : hal::FileOpenMode::WRITE))
            {
                if (m_operation == SdOperation::WRITE)
                {
                    m_write_result = SdWriteResult::FILE_OPEN_FAILED;
                }
                else
                {
                    m_read_result = SdReadResult::FILE_OPEN_FAILED;
                }
            }
            else
            {
                if (m_operation == SdOperation::WRITE)
                {
                    if (!m_sd_hal.write(m_write_data, m_write_len))
                    {
                        m_write_result = SdWriteResult::FILE_WRITE_ERROR;
                    }
                }
                else
                {
                    m_bytes_read = m_sd_hal.read(m_read_buffer, m_read_buffer_size);
                    m_read_result = (m_bytes_read > 0)
                        ? SdReadResult::SUCCESS
                        : SdReadResult::FILE_READ_EMPTY;
                }
            }
            m_sd_hal.close();

            set_state(SdState::END_IO);
            return peripherals::SdSignal::CS_ACTIVE;        // CS remains active through END_IO entry
        }

        case SdState::END_IO:
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

void SdController::start_write(const char *path, const uint8_t *data, size_t len)
{
    m_io_path = path;
    m_write_data = data;
    m_write_len = len;
    m_write_result = SdWriteResult::SUCCESS;
    m_operation = SdOperation::WRITE;

    set_state(SdState::PRE_DEACTIVATE_CS);
}

SdWriteResult SdController::write_file(const char *path, const uint8_t *data,
                                         size_t len, millis_t timeout_ms)
{
    start_write(path, data, len);

    millis_t start_time = m_time.now();

    while (m_state != SdState::IDLE)
    {
        if (TimeService::elapsed_ms(start_time, m_time.now()) > timeout_ms)
        {
            m_state = SdState::IDLE;
            return SdWriteResult::TIMEOUT;
        }

        tick();
        m_time.delay_ms(1);
    }

    return m_write_result;
}

void SdController::start_read(const char *path, uint8_t *buffer, size_t buffer_size)
{
    m_io_path = path;
    m_read_buffer = buffer;
    m_read_buffer_size = buffer_size;
    m_bytes_read = 0;
    m_read_result = SdReadResult::SUCCESS;
    m_operation = SdOperation::READ;

    set_state(SdState::PRE_DEACTIVATE_CS);
}

SdReadResult SdController::read_file(const char *path, uint8_t *buffer,
                                      size_t buffer_size, size_t *bytes_read,
                                      millis_t timeout_ms)
{
    start_read(path, buffer, buffer_size);

    millis_t start_time = m_time.now();

    while (m_state != SdState::IDLE)
    {
        if (TimeService::elapsed_ms(start_time, m_time.now()) > timeout_ms)
        {
            m_state = SdState::IDLE;
            if (bytes_read != nullptr)
            {
                *bytes_read = 0;
            }
            return SdReadResult::TIMEOUT;
        }

        tick();
        m_time.delay_ms(1);
    }

    if (bytes_read != nullptr)
    {
        *bytes_read = m_bytes_read;
    }

    return m_read_result;
}

// ============================================================
// Initialization
// ============================================================

hal::SdInitResult SdController::init_card()
{
    return m_sd_hal.init();
}

void SdController::set_state(SdController::SdState next_state)
{
    m_state = next_state;
    m_last_state_change_ms = m_time.now();
}
