#include "sd_controller.h"
#include "hal/time_hal.h"

using hal::millis_t;

// ============================================================
// Construction
// ============================================================

SdController::SdController(TimeService& time, hal::SdCardHal& sd_hal,
                           CsSignalFn cs_signal_fn)
    : m_time(time)
    , m_sd_hal(sd_hal)
    , m_cs_signal_fn(cs_signal_fn)
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
            if (m_operation == SdOperation::CREATE_DIR)
            {
                // Directory creation: no open/close needed
                if (!m_sd_hal.create_dir(m_io_path))
                {
                    m_create_dir_result = SdCreateDirResult::FAILED;
                }
            }
            else if (!m_sd_hal.open(m_io_path,
                m_operation == SdOperation::READ ? hal::FileOpenMode::READ : m_open_mode))
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
            else if (m_operation == SdOperation::WRITE)
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
            if (m_operation != SdOperation::CREATE_DIR)
            {
                m_sd_hal.close();
            }

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

void SdController::start_write(const char* path, const uint8_t* data, size_t len,
                               hal::FileOpenMode mode)
{
    m_io_path = path;
    m_write_data = data;
    m_write_len = len;
    m_write_result = SdWriteResult::SUCCESS;
    m_operation = SdOperation::WRITE;
    m_open_mode = mode;

    set_state(SdState::PRE_DEACTIVATE_CS);
}

void SdController::start_read(const char* path, uint8_t* buffer, size_t buffer_size)
{
    m_io_path = path;
    m_read_buffer = buffer;
    m_read_buffer_size = buffer_size;
    m_bytes_read = 0;
    m_read_result = SdReadResult::SUCCESS;
    m_operation = SdOperation::READ;

    set_state(SdState::PRE_DEACTIVATE_CS);
}

void SdController::start_create_dir(const char* path)
{
    m_io_path = path;
    m_create_dir_result = SdCreateDirResult::SUCCESS;
    m_operation = SdOperation::CREATE_DIR;

    set_state(SdState::PRE_DEACTIVATE_CS);
}

bool SdController::is_idle() const
{
    return m_state == SdState::IDLE;
}

SdWriteResult SdController::get_write_result() const
{
    return m_write_result;
}

SdReadResult SdController::get_read_result() const
{
    return m_read_result;
}

SdCreateDirResult SdController::get_create_dir_result() const
{
    return m_create_dir_result;
}

size_t SdController::get_bytes_read() const
{
    return m_bytes_read;
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

// ============================================================
// Synchronous I/O methods
// ============================================================

void SdController::cs_assert()
{
    if (m_cs_signal_fn)
    {
        m_cs_signal_fn(peripherals::SdSignal::CS_ACTIVE);
    }
}

void SdController::cs_deassert()
{
    if (m_cs_signal_fn)
    {
        m_cs_signal_fn(peripherals::SdSignal::CS_INACTIVE);
    }
}

bool SdController::write_sync(const char* path, const uint8_t* data, size_t len,
                              hal::FileOpenMode mode)
{
    cs_assert();
    bool ok = m_sd_hal.open(path, mode) && m_sd_hal.write(data, len);
    m_sd_hal.close();
    cs_deassert();
    return ok;
}

size_t SdController::read_sync(const char* path, uint8_t* buffer, size_t buffer_size)
{
    cs_assert();
    size_t bytes_read = 0;
    if (m_sd_hal.open(path, hal::FileOpenMode::READ))
    {
        bytes_read = m_sd_hal.read(buffer, buffer_size);
    }
    m_sd_hal.close();
    cs_deassert();
    return bytes_read;
}

bool SdController::create_dir_sync(const char* path)
{
    cs_assert();
    bool ok = m_sd_hal.create_dir(path);
    cs_deassert();
    return ok;
}

bool SdController::exists_sync(const char* path)
{
    cs_assert();
    bool exists = m_sd_hal.exists(path);
    cs_deassert();
    return exists;
}

bool SdController::write_at_sync(const char* path, uint32_t offset, const uint8_t* data, size_t len)
{
    cs_assert();
    bool ok = m_sd_hal.open(path, hal::FileOpenMode::READ_WRITE)
              && m_sd_hal.seek(offset)
              && m_sd_hal.write(data, len);
    m_sd_hal.close();
    cs_deassert();
    return ok;
}
