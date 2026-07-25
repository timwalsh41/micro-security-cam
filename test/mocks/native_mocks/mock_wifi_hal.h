#ifndef MOCK_WIFI_HAL_H
#define MOCK_WIFI_HAL_H

#include "hal/wifi_hal.h"
#include <cstring>

/**
 * @file mock_wifi_hal.h
 * @brief Configurable mock WifiHal for native unit tests.
 *
 * Provides full control over return values and call tracking
 * so that WifiService can be tested deterministically on the
 * host machine.
 */
class MockWifiHal : public hal::WifiHal
{
    public:
        // ============================================================
        // Configurable return values (set before calling the method)
        // ============================================================

        /** Value returned by status(). */
        hal::WifiStatus status_result{hal::WifiStatus::CONNECTED};

        /** Value returned by is_connected(). */
        bool connected_result{true};

        /** IP address returned by local_ip(). */
        char ip_buffer[16]{"192.168.1.100"};

        /** Value returned by config_static(). */
        bool config_static_result{true};

        // ============================================================
        // Call tracking (inspect after calling the method)
        // ============================================================

        int mode_call_count{0};
        int begin_sta_call_count{0};
        int begin_ap_call_count{0};
        int disconnect_call_count{0};
        int config_static_call_count{0};

        char last_ssid[64]{};
        char last_password[64]{};
        char last_static_ip[64]{};
        char last_gateway[64]{};
        char last_subnet[64]{};

        // ============================================================
        // Interface implementation
        // ============================================================

        void mode(bool /*sta*/, bool /*ap*/) override
        {
            mode_call_count++;
        }

        void begin_sta(const char* ssid, const char* password) override
        {
            begin_sta_call_count++;
            strncpy(last_ssid, ssid, sizeof(last_ssid) - 1);
            strncpy(last_password, password, sizeof(last_password) - 1);
        }

        void begin_ap(const char* ssid, const char* password) override
        {
            begin_ap_call_count++;
            strncpy(last_ssid, ssid, sizeof(last_ssid) - 1);
            strncpy(last_password, password, sizeof(last_password) - 1);
        }

        hal::WifiStatus status() const override
        {
            return status_result;
        }

        const char* local_ip() const override
        {
            return ip_buffer;
        }

        void disconnect() override
        {
            disconnect_call_count++;
        }

        bool is_connected() const override
        {
            return connected_result;
        }

        bool config_static(const char* ip, const char* gateway,
                           const char* subnet) override
        {
            config_static_call_count++;
            strncpy(last_static_ip, ip, sizeof(last_static_ip) - 1);
            strncpy(last_gateway, gateway, sizeof(last_gateway) - 1);
            strncpy(last_subnet, subnet, sizeof(last_subnet) - 1);
            return config_static_result;
        }
};

#endif // MOCK_WIFI_HAL_H
