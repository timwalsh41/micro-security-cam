#include "hal/wifi_hal_impl.h"
#include <WiFi.h>
#include <cstring>

/**
 * @file wifi_hal_impl.cpp
 * @brief ESP32 concrete implementation of the WiFi HAL interface.
 */

namespace hal
{

bool WifiHalImpl::config_static_ap(const char* ip, const char* gateway,
                                    const char* subnet)
{
    IPAddress ip_addr, gw_addr, subnet_addr;
    if (!ip_addr.fromString(ip) ||
        !gw_addr.fromString(gateway) ||
        !subnet_addr.fromString(subnet))
    {
        return false;
    }
    return WiFi.softAPConfig(ip_addr, gw_addr, subnet_addr);
}

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
    // An address configured by begin_ap() takes precedence. Otherwise
    // format the live STA address into stable member storage:
    // WiFi.localIP().toString() returns a temporary String whose
    // buffer dies with it, so its c_str() must not escape this call.
    if (m_ip_buffer[0] != '\0' && strcmp(m_ip_buffer, "0.0.0.0") != 0)
    {
        return m_ip_buffer;
    }
    String ip = WiFi.localIP().toString();
    strncpy(m_sta_ip_buffer, ip.c_str(), sizeof(m_sta_ip_buffer) - 1);
    m_sta_ip_buffer[sizeof(m_sta_ip_buffer) - 1] = '\0';
    return m_sta_ip_buffer;
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

bool WifiHalImpl::config_static(const char* ip, const char* gateway,
                                const char* subnet)
{
    IPAddress ip_addr, gw_addr, subnet_addr;
    if (!ip_addr.fromString(ip) ||
        !gw_addr.fromString(gateway) ||
        !subnet_addr.fromString(subnet))
    {
        return false;
    }
    return WiFi.config(ip_addr, gw_addr, subnet_addr);
}

} // namespace hal
