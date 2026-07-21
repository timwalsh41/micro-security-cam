#include "hal/wifi_hal_impl.h"
#include <WiFi.h>
#include <cstring>

/**
 * @file wifi_hal_impl.cpp
 * @brief ESP32 concrete implementation of the WiFi HAL interface.
 */

namespace hal
{

void WifiHalImpl::mode(bool sta, bool ap)
{
    if (sta && ap)
    {
        WiFi.mode(WIFI_AP_STA);
    }
    else if (sta)
    {
        WiFi.mode(WIFI_STA);
    }
    else if (ap)
    {
        WiFi.mode(WIFI_AP);
    }
    else
    {
        WiFi.mode(WIFI_OFF);
    }
}

void WifiHalImpl::begin_sta(const char* ssid, const char* password)
{
    WiFi.begin(ssid, password);
}

void WifiHalImpl::begin_ap(const char* ssid, const char* password)
{
    WiFi.softAP(ssid, password);
    strncpy(m_ip_buffer, WiFi.softAPIP().toString().c_str(),
            sizeof(m_ip_buffer) - 1);
    m_ip_buffer[sizeof(m_ip_buffer) - 1] = '\0';
}

WifiStatus WifiHalImpl::status() const
{
    return static_cast<WifiStatus>(WiFi.status());
}

const char* WifiHalImpl::local_ip() const
{
    return WiFi.localIP().toString().c_str();
}

void WifiHalImpl::disconnect()
{
    WiFi.disconnect();
    m_connected = false;
}

bool WifiHalImpl::is_connected() const
{
    return WiFi.status() == WL_CONNECTED;
}

} // namespace hal
