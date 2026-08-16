#ifndef HAL_SERIAL_HAL_IMPL_H
#define HAL_SERIAL_HAL_IMPL_H

#include "hal/serial_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace hal
{

/**
 * @class SerialHalImpl
 * @brief Concrete implementation of the Serial HAL interface.
 *
 * Wraps the Arduino HardwareSerial (Serial) object, providing
 * UART communication for logging and serial command transport.
 *
 * Serial writes are serialised with a mutex so that multi-byte log
 * lines (including their trailing newline) are not interleaved when
 * multiple FreeRTOS tasks log concurrently. The mutex is created
 * lazily on the first write because the FreeRTOS heap is not ready
 * when global constructors run.
 */
class SerialHalImpl : public SerialHal
{
    public:
        /**
         * @brief Constructs the serial HAL and opens the port.
         * @param baud Communication speed in bits per second.
         */
        SerialHalImpl(uint32_t baud);

        int available() override;
        int read() override;
        size_t write(const uint8_t* data, size_t len) override;
        void flush() override;

        /**
         * @brief RAII guard that acquires a FreeRTOS mutex on entry
         *        and releases it on scope exit.
         *
         * Equivalent to std::lock_guard<std::mutex> but for a
         * SemaphoreHandle_t, so serial writes need not be manually
         * paired with an xSemaphoreGive().
         */
        class ScopedMutexLock
        {
            public:
                /**
                 * @brief Acquires the given mutex, blocking indefinitely.
                 * @param mutex The FreeRTOS mutex to acquire.
                 */
                explicit ScopedMutexLock(SemaphoreHandle_t mutex)
                    : m_mutex(mutex)
                {
                    if (m_mutex != nullptr)
                    {
                        xSemaphoreTake(m_mutex, portMAX_DELAY);
                    }
                }

                /**
                 * @brief Releases the mutex.
                 */
                ~ScopedMutexLock()
                {
                    if (m_mutex != nullptr)
                    {
                        xSemaphoreGive(m_mutex);
                    }
                }

                /** Not copyable. */
                ScopedMutexLock(const ScopedMutexLock&) = delete;
                /** Not assignable. */
                ScopedMutexLock& operator=(const ScopedMutexLock&) = delete;

            private:
                SemaphoreHandle_t m_mutex;
        };

    private:
        /**
         * @brief Critical section spinlock guarding lazy creation of the
         *        write mutex. Statically initialised, so it is safe to use
         *        from any task or core without prior initialisation.
         */
        portMUX_TYPE m_init_spinlock = portMUX_INITIALIZER_UNLOCKED;

        /**
         * @brief Serialises write() so a log line is emitted atomically
         *        when multiple tasks log concurrently. Created lazily on
         *        the first write; see ensure_mutex().
         */
        SemaphoreHandle_t m_write_mutex{nullptr};

        /**
         * @brief Guards the mutex-creation failure log so it is emitted
         *        only once rather than on every unprotected write.
         */
        bool m_mutex_failure_logged{false};

        /**
         * @brief Creates the write mutex if it does not already exist.
         *
         * Called under m_init_spinlock so a single mutex is created even
         * when the first write() happens concurrently on both cores.
         *
         * @return The write mutex, or nullptr if creation failed.
         */
        SemaphoreHandle_t ensure_mutex();
};

} // namespace hal

#endif // HAL_SERIAL_HAL_IMPL_H
