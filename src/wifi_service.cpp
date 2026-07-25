#include "wifi_service.h"
#include <cstdio>

/**
 * @file wifi_service.cpp
 * @brief WiFi lifecycle service implementation with STA+AP fallback.
 */

/**
 * @brief Polling interval in milliseconds during STA connection wait.
 */
static constexpr hal::millis_t POLL_INTERVAL_MS = 1000;

WifiService::WifiService(hal::WifiHal& hal, TimeService& time, LoggingService& log)
    : m_hal(hal)
    , m_time(time)
    , m_log(log)
{
}

void WifiService::init(const ConfigData::WifiConfig& sta,
                       const ConfigData::ApConfig& ap,
                       uint16_t timeout_secs)
{
    m_log.info("Wifi", "Initializing WiFi...");

    // Set WiFi mode to STA (station) only initially
    m_hal.mode(true, false);

    // Apply static IP if all three fields are provided
    if (sta.static_ip[0] != '\0' &&
        sta.gateway[0] != '\0' &&
        sta.subnet_mask[0] != '\0')
    {
        m_log.info("Wifi", "Configuring static IP: %s / %s / %s",
                   sta.static_ip, sta.gateway, sta.subnet_mask);
        if (!m_hal.config_static(sta.static_ip, sta.gateway, sta.subnet_mask))
        {
            m_log.warn("Wifi", "Static IP configuration failed, "
                       "falling back to DHCP");
        }
    }
    else
    {
        m_log.debug("Wifi", "No static IP config, using DHCP");
    }

    // Attempt STA connection
    m_log.info("Wifi", "Connecting to STA network: %s", sta.ssid);
    m_hal.begin_sta(sta.ssid, sta.password);

    // Poll until connected or timeout
    hal::millis_t start_ms = m_time.now();
    hal::millis_t elapsed_ms = 0;
    
    do
    {
        m_time.delay_ms(POLL_INTERVAL_MS);
        
        if (m_hal.status() == hal::WifiStatus::CONNECTED)
        {
            m_connected = true;
            m_log.info("Wifi", "STA connected successfully. IP: %s",
                       m_hal.local_ip());
            return;
        }

        elapsed_ms = m_time.elapsed_ms(start_ms, m_time.now());
        m_log.debug("Wifi", "Waiting for STA connection... (%u ms/%u ms)",
                     elapsed_ms, timeout_secs*1000);
    } while (elapsed_ms < timeout_secs*1000);

    // STA connection timed out, fall back to AP mode
    m_log.warn("Wifi", "STA connection timed out after %u seconds, "
               "falling back to AP mode", timeout_secs);

    // Switch to AP mode
    m_hal.mode(false, true);
    m_hal.begin_ap(ap.ssid, ap.password);

    m_log.info("Wifi", "AP started. SSID: %s, IP: %s",
               ap.ssid, m_hal.local_ip());

    // Note: In AP mode, m_connected remains false as we are not
    // connected to a network as a station.
}

bool WifiService::is_connected() const
{
    return m_connected;
}

const char* WifiService::local_ip() const
{
    return m_hal.local_ip();
}
