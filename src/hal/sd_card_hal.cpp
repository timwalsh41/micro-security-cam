#include "hal/sd_card_hal_impl.h"

namespace hal
{

// ============================================================
// SdCardHalImpl
// ============================================================

SdCardHalImpl::SdCardHalImpl(uint8_t cs_pin)
    : m_cs_pin(cs_pin)
{
}

SdInitResult SdCardHalImpl::init()
{
    if (!SD.begin(m_cs_pin))
    {
        return SdInitResult::HW_INIT_FAILURE;
    }

    // Verify a card is actually present
    if (SD.cardType() == CARD_NONE)
    {
        return SdInitResult::NO_SD_CARD;
    }

    m_initialized = true;
    
    return SdInitResult::SUCCESS;
}

bool SdCardHalImpl::open(const char* path, hal::FileOpenMode mode)
{
    if (mode == hal::FileOpenMode::READ)
    {
        m_file = SD.open(path, FILE_READ);
    }
    else if (mode == hal::FileOpenMode::APPEND)
    {
        m_file = SD.open(path, FILE_APPEND);
    }
    else
    {
        m_file = SD.open(path, FILE_WRITE);
    }
    m_file_open = m_file ? true : false;
    return m_file_open;
}

bool SdCardHalImpl::write(const uint8_t* data, size_t len)
{
    if (!m_file_open)
    {
        return false;
    }

    return m_file.write(data, len) == len;
}

size_t SdCardHalImpl::read(uint8_t* buffer, size_t len)
{
    if (!m_file_open)
    {
        return 0;
    }

    return m_file.read(buffer, len);
}

void SdCardHalImpl::close()
{
    if (m_file_open)
    {
        m_file.close();
        m_file_open = false;
    }
}

bool SdCardHalImpl::create_dir(const char* path)
{
    return SD.mkdir(path);
}

bool SdCardHalImpl::is_initialized() const
{
    return m_initialized;
}

} // namespace hal
