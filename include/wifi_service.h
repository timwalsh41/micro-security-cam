#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "hal/wifi_hal.h"
#include "time_service.h"
#include "logging_service.h"
#include "ini_parser.h"

/**
 * @file wifi_service.h
 * @brief WiFi lifecycle service with STA+AP fallback.
 *
 * Encapsulates WiFi connection logic — attempts STA first,
 * falls back to AP mode on timeout. Reconnection strategy is
 * encapsulated here for future evolution.
 */
class WifiService
{
    public:
        /**
         * @brief Constructs a WifiService with the given dependencies.
         *
         * @param hal  WiFi hardware abstraction layer.
         * @param time Time service for elapsed/delay operations.
         * @param log  Logging service for status messages.
         */
        WifiService(hal::WifiHal& hal, TimeService& time, LoggingService& log);

        /**
         * @brief Initializes WiFi with STA mode, falling back to AP.
         *
         * @param sta          Station mode config (SSID, password).
         * @param ap           AP fallback config (SSID, password).
         * @param timeout_secs Max seconds to wait for STA connection.
         */
        void init(const ConfigData::WifiConfig& sta,
                  const ConfigData::ApConfig& ap,
                  uint16_t timeout_secs = 30);

        /**
         * @brief Returns whether WiFi is currently connected.
         * @return true if connected in STA mode.
         */
        bool is_connected() const;

        /**
         * @brief Returns the local IP address as a dotted-decimal string.
         * @return Pointer to an internal buffer with the IP string.
         */
        const char* local_ip() const;

    private:
        hal::WifiHal& m_hal;
        TimeService& m_time;
        LoggingService& m_log;
        bool m_connected{false};
};

#endif // WIFI_SERVICE_H
