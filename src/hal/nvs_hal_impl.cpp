#include "hal/nvs_hal_impl.h"
#include <cstring>

/**
 * @file nvs_hal_impl.cpp
 * @brief ESP32 concrete implementation of the NVS HAL interface.
 */

namespace hal
{

bool NvsHalImpl::begin(const char* namespace_name)
{
    // Preferences::begin() fails while a namespace is already open,
    // so close the prior handle before re-opening.
    if (m_open)
    {
        m_prefs.end();
        m_open = false;
    }
    m_open = m_prefs.begin(namespace_name, false);
    return m_open;
}

size_t NvsHalImpl::read_blob(const char* key,
                             uint8_t* buffer, size_t buffer_size)
{
    if (!m_open || key == nullptr || buffer == nullptr)
    {
        return 0;
    }

    if (!m_prefs.isKey(key))
    {
        return 0;
    }

    return m_prefs.getBytes(key, buffer, buffer_size);
}

bool NvsHalImpl::write_blob(const char* key,
                            const uint8_t* data, size_t len)
{
    if (!m_open || key == nullptr || data == nullptr)
    {
        return false;
    }

    return m_prefs.putBytes(key, data, len) == len;
}

bool NvsHalImpl::erase_key(const char* key)
{
    if (!m_open || key == nullptr)
    {
        return false;
    }

    if (!m_prefs.isKey(key))
    {
        return true;
    }

    return m_prefs.remove(key);
}

} // namespace hal