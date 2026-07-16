#ifndef PLATFORM_INITIALIZER_H
#define PLATFORM_INITIALIZER_H

/**
 * @file platform_initializer.h
 * @brief Platform-level initialization for ESP32-S3 target.
 *
 * Centralizes ESP32-BSP-level initialization steps that must happen
 * early in setup() before any other subsystem is used. Currently
 * handles ESP-IDF internal log suppression to prevent UART0
 * interleaving on the dual-core ESP32-S3.
 *
 * This class exists at the boundary between the application and the
 * ESP-IDF BSP. It is intentionally kept separate from Arduino-level
 * abstractions (e.g. SerialLogBackend) to maintain clean abstraction
 * layers.
 *
 * Extend this class with additional platform initialization steps
 * (USB CDC/JTAG config, CPU frequency, PSRAM init, etc.) by adding
 * new private methods and calling them from init().
 */

/**
 * @class PlatformInitializer
 * @brief Owns platform-level (ESP-IDF BSP) initialization.
 *
 * Instantiate once at global scope and call init() at the top of
 * setup(), before any other initialization.
 */
class PlatformInitializer
{
    public:
        /**
         * @brief Construct a PlatformInitializer.
         */
        PlatformInitializer() = default;

        /**
         * @brief Run all platform initialization steps.
         *
         * Must be called once at the start of setup(), before any
         * logging or peripheral initialization.
         */
        void init();

    private:
        /**
         * @brief Suppress ESP-IDF internal log output on UART0.
         *
         * Disables per-tag logging and installs a no-op vprintf handler
         * so that ESP-IDF internals (WiFi, camera driver, etc.) do not
         * interleave with application log writes on UART0.
         */
        void suppress_esp_idf_logging();
};

#endif // PLATFORM_INITIALIZER_H
