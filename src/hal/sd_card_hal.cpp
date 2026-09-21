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
    else if (mode == hal::FileOpenMode::READ_WRITE)
    {
        // "r+": open for reading and writing without truncating.
        // The mode string is passed to fopen(), so this preserves
        // existing content for in-place header patching.
        m_file = SD.open(path, "r+");
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

bool SdCardHalImpl::seek(uint32_t pos)
{
    if (!m_file_open)
    {
        return false;
    }

    return m_file.seek(pos);
}

bool SdCardHalImpl::create_dir(const char* path)
{
    return SD.mkdir(path);
}

bool SdCardHalImpl::exists(const char* path)
{
    return SD.exists(path);
}

bool SdCardHalImpl::remove(const char* path)
{
    if (!SD.exists(path))
    {
        return true;
    }
    return SD.remove(path);
}

bool SdCardHalImpl::list_dir(const char* dir_path,
                             DirEntryCallback callback)
{
    File dir = SD.open(dir_path);
    if (!dir || !dir.isDirectory())
    {
        if (dir)
        {
            dir.close();
        }
        return false;
    }

    File entry = dir.openNextFile();
    while (entry)
    {
        const char* name = entry.name();
        if (name != nullptr)
        {
            size_t size = entry.isDirectory() ? 0 : entry.size();
            if (!callback(name, size, entry.isDirectory()))
            {
                break;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }

    dir.close();
    return true;
}

size_t SdCardHalImpl::file_size(const char* path)
{
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory())
    {
        if (file)
        {
            file.close();
        }
        return 0;
    }
    size_t size = file.size();
    file.close();
    return size;
}

uint64_t SdCardHalImpl::free_bytes()
{
    return SD.totalBytes() - SD.usedBytes();
}

uint64_t SdCardHalImpl::total_bytes()
{
    return SD.totalBytes();
}

bool SdCardHalImpl::is_initialized() const
{
    return m_initialized;
}

} // namespace hal
