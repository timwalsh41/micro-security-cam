#ifndef HAL_WIFI_HAL_H
#define HAL_WIFI_HAL_H

#include <cstdint>

/**
 * @file wifi_hal.h
 * @brief Abstract interface for WiFi hardware operations.
 *
 * Provides a mockable abstraction over the Arduino WiFi library.
 * Concrete platform implementation and native test mock derive from
 * this interface.
 */

namespace hal
{

/**
 * @brief WiFi connection status codes.
 *
 * Mirrors the Arduino WiFi library's status constants so that
 * the mapping between the HAL enum and the underlying hardware
 * is explicit and unambiguous.
 */
enum class WifiStatus : int
{
    IDLE             = 0,  /**< WiFi is in idle state. */
    NO_SSID_AVAIL    = 1,  /**< No matching SSID found. */
    SCAN_COMPLETED   = 2,  /**< Network scan completed. */
    CONNECTED        = 3,  /**< Successfully connected to a network. */
    CONNECT_FAILED   = 4,  /**< Connection attempt failed. */
    CONNECTION_LOST  = 5,  /**< Previously established connection lost. */
    DISCONNECTED     = 6   /**< Not connected to any network. */
};

/**
 * @class WifiHal
 * @brief Abstract interface for WiFi hardware operations.
 *
 * Supports both station (STA) and access-point (AP) modes.
 * Implementations manage the WiFi lifecycle; consumers only
 * query status and IP address.
 */
class WifiHal
{
    public:
        virtual ~WifiHal() = default;

        /**
         * @brief Sets the WiFi operating mode.
         * @param sta Enable station mode.
         * @param ap  Enable access-point mode.
         */
        virtual void mode(bool sta, bool ap) = 0;

        /**
         * @brief Initiates a WiFi station-mode connection (async).
         * @param ssid     Network name.
         * @param password Network password.
         *
         * Connection is asynchronous; poll status() to determine
         * when the link is established.
         */
        virtual void begin_sta(const char* ssid, const char* password) = 0;

        /**
         * @brief Starts a soft access point.
         * @param ssid     AP name.
         * @param password AP password.
         */
        virtual void begin_ap(const char* ssid, const char* password) = 0;

        /**
         * @brief Returns the current WiFi connection status.
         * @return A WifiStatus enum value indicating the connection state.
         */
        virtual WifiStatus status() const = 0;

        /**
         * @brief Returns the local IP address as a dotted-decimal string.
         * @return Pointer to a static or member buffer containing the IP string.
         */
        virtual const char* local_ip() const = 0;

        /**
         * @brief Configures static IP addressing for the station interface.
         *
         * Must be called before begin_sta(). If configuration fails
         * (e.g. invalid address strings), the caller should fall back
         * to DHCP by simply not calling this method.
         *
         * @param ip       Static IP address as dotted-decimal string.
         * @param gateway  Gateway IP address as dotted-decimal string.
         * @param subnet   Subnet mask as dotted-decimal string.
         * @return true if the configuration was applied successfully.
         */
        virtual bool config_static(const char* ip, const char* gateway,
                                   const char* subnet) = 0;

        /**
         * @brief Disconnects from the current network.
         */
        virtual void disconnect() = 0;

        /**
         * @brief Whether the station interface is currently connected.
         * @return true if connected.
         */
        virtual bool is_connected() const = 0;
};

} // namespace hal

#endif // HAL_WIFI_HAL_H
