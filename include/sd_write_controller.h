#ifndef SD_WRITE_CONTROLLER_H
#define SD_WRITE_CONTROLLER_H

#include <cstdint>
#include <cstddef>
#include "time_service.h"
#include "hal/sd_card_hal.h"
#include "pin_arbitrator.h"

/**
 * @file sd_write_controller.h
 * @brief SD card write lifecycle with chip-select timing protocol.
 *
 * The SdWriteController owns a state machine that sequences the
 * chip-select (CS) pin assertion and de-assertion around SD card
 * write operations, ensuring proper timing between transactions.
 */

/**
 * @brief Result of an SD card write operation.
 */
enum class SdWriteResult : uint8_t
{
    SUCCESS,            ///< File written successfully
    FILE_OPEN_FAILED,   ///< Could not open file for writing
    FILE_WRITE_ERROR,   ///< Error during file write
    TIMEOUT             ///< Write operation exceeded allowed time
};

/**
 * @class SdWriteController
 * @brief Manages SD card write lifecycle with proper CS timing protocol.
 *
 * Owns a state machine that sequences chip-select (CS) assertion and
 * de-assertion around SD card write operations:
 *
 *   IDLE → PRE_DEACTIVATE_CS → ACTIVATE_CS → WRITING → STOP_WRITE
 *       → POST_DEACTIVATE_CS → IDLE
 *
 * The tick() method returns the desired SdSignal for the shared pin.
 * Callers are responsible for applying the resolved state to the
 * physical pin.
 *
 * Hardware I/O is delegated to an injected SdCardHal implementation,
 * enabling deterministic testing with a mock.
 */
class SdWriteController
{
    public:
        /**
         * @brief Constructs an SdWriteController with the given dependencies.
         *
         * @param time   Reference to a TimeService used for timing.
         * @param sd_hal Reference to an SdCardHal used for hardware I/O.
         */
        explicit SdWriteController(TimeService &time, hal::SdCardHal &sd_hal);

        /**
         * @brief Advances the state machine and returns the desired CS signal.
         *
         * Call this periodically from the main loop. The returned value
         * indicates the SD card's current request for the shared CS pin:
         *
         *  - IDLE:        SD doesn't care about the pin; LED's request wins.
         *  - CS_ACTIVE:   SD needs CS driven LOW (during activation/writing).
         *  - CS_INACTIVE: SD needs CS driven HIGH (during deactivation timing).
         *
         * Feed the return value directly into PinArbitrator::resolve().
         *
         * @return peripherals::SdSignal describing the desired CS pin state.
         */
        peripherals::SdSignal tick();

        /**
         * @brief Initiates a non-blocking write operation.
         *
         * Enters the CS timing protocol starting from PRE_DEACTIVATE_CS.
         * Call tick() repeatedly to advance the state machine through
         * the full cycle back to IDLE.
         *
         * The caller must ensure that @p path, @p data, and @p len remain
         * valid until the state machine returns to IDLE.
         *
         * @param path The file path to write to.
         * @param data Pointer to the data buffer to write.
         * @param len  Number of bytes to write.
         */
        void start_write(const char *path, const uint8_t *data, size_t len);

        /**
         * @brief Performs a synchronous write with a timeout.
         *
         * Calls start_write() then loops tick() until the state machine
         * returns to IDLE or the timeout expires.
         *
         * @param path      The file path to write to.
         * @param data      Pointer to the data buffer to write.
         * @param len       Number of bytes to write.
         * @param timeout_ms Maximum time in milliseconds for the full operation.
         * @return SdWriteResult indicating success, failure reason, or timeout.
         */
        SdWriteResult write_synchronous(const char *path, const uint8_t *data,
                                         size_t len, uint32_t timeout_ms);

        /**
         * @brief Initializes the SD card hardware.
         *
         * Delegates to SdCardHal::init().
         *
         * @return hal::SdInitResult indicating success or the failure reason.
         */
        hal::SdInitResult init_card();

        // ============================================================
        // Timing constants (public so tests can reference them)
        // ============================================================

        /** Minimum time CS must remain de-asserted between transactions (ms). */
        static constexpr uint32_t DEACTIVATE_TIME_MS = 10;

        /** Minimum time CS must be asserted before starting I/O (ms). */
        static constexpr uint32_t ACTIVATE_TIME_MS = 5;

    private:
        /**
         * @brief Internal states of the SD write protocol state machine.
         */
        enum class SdState : uint8_t
        {
            IDLE,               ///< No write operation in progress
            PRE_DEACTIVATE_CS,  ///< Ensuring CS is de-asserted before re-asserting
            ACTIVATE_CS,        ///< CS asserted, waiting for settle time
            WRITING,            ///< Performing file I/O (open/write/close)
            STOP_WRITE,         ///< Transitioning CS from active to inactive
            POST_DEACTIVATE_CS  ///< Ensuring CS meets minimum de-assertion time
        };

        TimeService &m_time;
        hal::SdCardHal &m_sd_hal;

        SdState m_state{SdState::IDLE};
        uint32_t m_last_state_change_ms{0};

        // Buffered write parameters (valid until state returns to IDLE)
        const char *m_write_path{nullptr};
        const uint8_t *m_write_data{nullptr};
        size_t m_write_len{0};

        // Result tracking
        SdWriteResult m_write_result{SdWriteResult::SUCCESS};

        // Internal function to advance state
        void set_state(SdState next_state);
};

#endif // SD_WRITE_CONTROLLER_H
