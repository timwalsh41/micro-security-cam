#ifndef HAL_WIFI_HAL_IMPL_H
#define HAL_WIFI_HAL_IMPL_H

#include "hal/wifi_hal.h"

namespace hal
{

/**
 * @class WifiHalImpl
 * @brief Concrete implementation of the WiFi HAL interface.
 *
 * Wraps the Arduino WiFi library, providing STA and AP mode
 * management.
 */
class WifiHalImpl : public WifiHal
{
    public:
        void mode(bool sta, bool ap) override;
        void begin_sta(const char* ssid, const char* password) override;
        void begin_ap(const char* ssid, const char* password) override;
        WifiStatus status() const override;
        const char* local_ip() const override;
        void disconnect() override;
        bool is_connected() const override;
        bool config_static(const char* ip, const char* gateway,
                           const char* subnet) override;

    private:
        bool m_connected{false};
        char m_ip_buffer[16]{"0.0.0.0"};
};

} // namespace hal

#endif // HAL_WIFI_HAL_IMPL_H
