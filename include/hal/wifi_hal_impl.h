#ifndef HAL_WIFI_HAL_IMPL_H
#define HAL_WIFI_HAL_IMPL_H

#include "hal/wifi_hal.h"

namespace hal
{

/**
 * @class WifiHalImpl
 * @brief Concrete ESP32 implementation of the WiFi HAL interface.
 *
 * Wraps the Arduino WiFi library, providing STA and AP mode
 * management. All WiFi library calls are guarded by ARDUINO
 * preprocessor checks to allow native unit testing.
 */
class WifiHalImpl : public WifiHal
{
    public:
        bool mode(bool sta, bool ap) override;
        bool begin_sta(const char* ssid, const char* password) override;
        bool begin_ap(const char* ssid, const char* password) override;
        WifiStatus status() const override;
        const char* local_ip() const override;
        void disconnect() override;
        bool is_connected() const override;

    private:
        bool m_connected{false};
        char m_ip_buffer[16]{"0.0.0.0"};
};

} // namespace hal

#endif // HAL_WIFI_HAL_IMPL_H
