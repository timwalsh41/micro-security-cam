#ifndef SD_CONTROLLER_H
#define SD_CONTROLLER_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include "time_service.h"
#include "hal/sd_card_hal.h"

/**
 * @file sd_controller.h
 * @brief SD card read/write lifecycle with chip-select timing protocol.
 *
 * The SdController owns a state machine that sequences the
 * chip-select (CS) pin assertion and de-assertion around SD card
 * read and write operations, ensuring proper timing between transactions.
 */

namespace peripherals
{
    /**
     * @brief Describes the SD card's current request for the shared CS pin.
     *
     * The SD controller drives this signal to indicate what the
     * chip-select pin should do. When IDLE, the caller may apply
     * the LED's request instead.
     */
    enum class SdSignal : uint8_t
    {
        IDLE,           ///< SD doesn't care; caller decides
        CS_ACTIVE,      ///< SD needs CS driven LOW (during activation/writing)
        CS_INACTIVE     ///< SD needs CS driven HIGH (during deactivation timing)
    };
}

/**
 * @brief Function type for driving the CS pin during sync I/O.
 *
 * @param signal The desired SdSignal for the CS pin.
 */
using CsSignalFn = std::function<void(peripherals::SdSignal)>;

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
 * @brief Result of an SD card read operation.
 */
enum class SdReadResult : uint8_t
{
    SUCCESS,            ///< File read successfully
    FILE_OPEN_FAILED,   ///< Could not open file for reading
    FILE_READ_EMPTY,    ///< Open succeeded but read returned zero bytes
    TIMEOUT             ///< Read operation exceeded allowed time
};

/**
 * @brief Result of an SD card directory creation operation.
 */
enum class SdCreateDirResult : uint8_t
{
    SUCCESS,            ///< Directory created successfully
    FAILED,             ///< Directory creation failed
    TIMEOUT             ///< Operation exceeded allowed time
};

/**
 * @class SdController
 * @brief Manages SD card read/write lifecycle with proper CS timing protocol.
 *
 * Owns a state machine that sequences chip-select (CS) assertion and
 * de-assertion around SD card I/O operations:
 *
 *   IDLE → PRE_DEACTIVATE_CS → ACTIVATE_CS → IO_ACTIVE → END_IO
 *       → POST_DEACTIVATE_CS → IDLE
 *
 * The tick() method returns the desired SdSignal for the shared pin.
 * Callers are responsible for applying the resolved state to the
 * physical pin.
 *
 * Hardware I/O is delegated to an injected SdCardHal implementation,
 * enabling deterministic testing with a mock.
 */
class SdController
{
    public:
        /**
         * @brief Constructs an SdController with the given dependencies.
         *
         * @param time   Reference to a TimeService used for timing.
         * @param sd_hal Reference to an SdCardHal used for hardware I/O.
         */
        explicit SdController(TimeService& time, hal::SdCardHal& sd_hal,
                              CsSignalFn cs_signal_fn = nullptr);

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
         * Callers apply the returned signal directly to the physical CS pin.
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
         * @param mode The file open mode (WRITE to truncate, APPEND to preserve).
         */
        void start_write(const char* path, const uint8_t* data, size_t len,
                         hal::FileOpenMode mode = hal::FileOpenMode::APPEND);

        /**
         * @brief Initiates a non-blocking read operation.
         *
         * Enters the CS timing protocol starting from PRE_DEACTIVATE_CS.
         * Call tick() repeatedly to advance the state machine through
         * the full cycle back to IDLE.
         *
         * The caller must ensure that @p path, @p buffer, and @p buffer_size
         * remain valid until the state machine returns to IDLE.
         *
         * @param path        The file path to read from.
         * @param buffer      Pointer to the buffer to read data into.
         * @param buffer_size Maximum number of bytes the buffer can hold.
         */
        void start_read(const char* path, uint8_t* buffer, size_t buffer_size);

        /**
         * @brief Initiates a non-blocking directory creation operation.
         *
         * Enters the CS timing protocol starting from PRE_DEACTIVATE_CS.
         * Call tick() repeatedly to advance the state machine through
         * the full cycle back to IDLE.
         *
         * The caller must ensure that @p path remains valid until
         * the state machine returns to IDLE.
         *
         * @param path The directory path to create.
         */
        void start_create_dir(const char* path);

        /**
         * @brief Returns whether the state machine is in IDLE state.
         * @return true if no I/O operation is in progress.
         */
        bool is_idle() const;

        /**
         * @brief Returns the result of the last completed write operation.
         * @return SdWriteResult from the most recent start_write() cycle.
         */
        SdWriteResult get_write_result() const;

        /**
         * @brief Returns the result of the last completed read operation.
         * @return SdReadResult from the most recent start_read() cycle.
         */
        SdReadResult get_read_result() const;

        /**
         * @brief Returns the result of the last completed create_dir operation.
         * @return SdCreateDirResult from the most recent start_create_dir() cycle.
         */
        SdCreateDirResult get_create_dir_result() const;

        /**
         * @brief Returns the number of bytes read in the last read operation.
         * @return Byte count from the most recent start_read() cycle.
         */
        size_t get_bytes_read() const;

        /**
         * @brief Initializes the SD card hardware.
         *
         * Delegates to SdCardHal::init().
         *
         * @return hal::SdInitResult indicating success or the failure reason.
         */
        hal::SdInitResult init_card();

        /**
         * @brief Returns whether the SD card is currently usable.
         *
         * True only after init_card() returned SUCCESS. Any failure
         * (no card present or initialization failure) latches this to
         * false until a subsequent successful init_card() call. Before
         * the first init_card() call the card is reported unavailable.
         *
         * @return true if the card was successfully initialized.
         */
        bool is_card_available() const;

        // ============================================================
        // Timing constants (public so tests can reference them)
        // ============================================================

        /** Minimum time CS must remain de-asserted between transactions (ms). */
        static constexpr hal::millis_t DEACTIVATE_TIME_MS = 1;

        /** Minimum time CS must be asserted before starting I/O (ms). */
        static constexpr hal::millis_t ACTIVATE_TIME_MS = 1;

        // ============================================================
        // Synchronous I/O methods
        // ============================================================

        /**
         * @brief Synchronous write: asserts CS, performs write, de-asserts CS.
         *
         * @param path File path to write to.
         * @param data Pointer to data buffer.
         * @param len  Number of bytes to write.
         * @param mode Open mode (WRITE truncates, APPEND preserves).
         * @return true if write succeeded.
         */
        bool write_sync(const char* path, const uint8_t* data, size_t len,
                        hal::FileOpenMode mode = hal::FileOpenMode::APPEND);

        /**
         * @brief Synchronous read: asserts CS, performs read, de-asserts CS.
         *
         * @param path        File path to read from.
         * @param buffer      Buffer to read into.
         * @param buffer_size Maximum bytes to read.
         * @return Number of bytes read (0 on failure).
         */
        size_t read_sync(const char* path, uint8_t* buffer, size_t buffer_size);

        /**
         * @brief Synchronous directory creation: asserts CS, creates dir, de-asserts CS.
         *
         * @param path Directory path to create.
         * @return true if creation succeeded.
         */
        bool create_dir_sync(const char* path);

        /**
         * @brief Synchronous file-existence check: asserts CS, checks, de-asserts CS.
         *
         * All SD I/O goes through this controller so the shared CS/LED pin
         * remains properly arbitrated.
         *
         * @param path File path to check.
         * @return true if the file exists.
         */
        bool exists_sync(const char* path);

        /**
         * @brief Synchronous file removal: asserts CS, removes file, de-asserts CS.
         *
         * All SD I/O goes through this controller so the shared CS/LED pin
         * remains properly arbitrated.
         *
         * @param path File path to remove.
         * @return true if the file was removed (or did not exist), false on error.
         */
        bool remove_sync(const char* path);

        /**
         * @brief Synchronous directory listing: asserts CS, lists entries, de-asserts CS.
         *
         * All SD I/O goes through this controller so the shared CS/LED pin
         * remains properly arbitrated.
         *
         * @param dir_path Directory path to list.
         * @param callback Function called with each entry (name, size, is_dir).
         * @return true if the directory was opened successfully, false otherwise.
         */
        bool list_dir_sync(const char* dir_path,
                           hal::DirEntryCallback callback);

        /**
         * @brief Synchronous seek-and-write: asserts CS, opens, seeks, writes, closes.
         *
         * Used for overwriting the AVI header at position 0 after recording.
         * Opens with READ_WRITE mode so existing file content is preserved
         * and only the bytes at @p offset are replaced.
         *
         * @param path   File path to open.
         * @param offset Byte offset to seek to before writing.
         * @param data   Pointer to data buffer.
         * @param len    Number of bytes to write.
         * @return true if seek + write succeeded.
         */
        bool write_at_sync(const char* path, uint32_t offset, const uint8_t* data, size_t len);

        /**
         * @brief Synchronous read-at-offset: asserts CS, opens, seeks, reads, closes.
         *
         * Each call is a single self-contained transaction, so serving a
         * large file issues one call per chunk (e.g. per download buffer).
         *
         * @param path   File path to open.
         * @param offset Byte offset to seek to before reading.
         * @param buffer Buffer to read into.
         * @param len    Maximum number of bytes to read.
         * @return Number of bytes read (0 on failure, missing file, or EOF).
         */
        size_t read_at_sync(const char* path, uint32_t offset,
                            uint8_t* buffer, size_t len);

        /**
         * @brief Synchronous file size: asserts CS, stats, de-asserts CS.
         *
         * @param path File path to stat.
         * @return File size in bytes, or 0 if the file does not exist
         *         or the path names a directory.
         */
        size_t size_sync(const char* path);

        /**
         * @brief Synchronous free-space query: asserts CS, queries, de-asserts CS.
         *
         * @return Free bytes on the card, or 0 when unknown/unavailable.
         */
        uint64_t free_bytes_sync();

        /**
         * @brief Synchronous total-capacity query: asserts CS, queries, de-asserts CS.
         *
         * @return Total card capacity in bytes, or 0 when unknown/unavailable.
         */
        uint64_t total_bytes_sync();

    private:
        /**
         * @brief Internal states of the SD I/O protocol state machine.
         */
        enum class SdState : uint8_t
        {
            IDLE,               ///< No I/O in progress
            PRE_DEACTIVATE_CS,  ///< Ensuring CS is de-asserted before re-asserting
            ACTIVATE_CS,        ///< CS asserted, waiting for settle time
            IO_ACTIVE,          ///< Performing file I/O (open/read|write/close)
            END_IO,             ///< I/O complete, transitioning CS from active to inactive
            POST_DEACTIVATE_CS  ///< Ensuring CS meets minimum de-assertion time
        };

        /**
         * @brief Whether the current I/O operation is a read or write.
         */
        enum class SdOperation : uint8_t
        {
            READ,
            WRITE,
            CREATE_DIR
        };

        TimeService& m_time;
        hal::SdCardHal& m_sd_hal;
        CsSignalFn m_cs_signal_fn;

        /** Result of the most recent init_card() call. */
        hal::SdInitResult m_init_result{hal::SdInitResult::NO_SD_CARD};

        SdState m_state{SdState::IDLE};
        hal::millis_t m_last_state_change_ms{0};

        /** The current I/O operation type. */
        SdOperation m_operation{SdOperation::WRITE};

        /** Open mode for the current write operation. */
        hal::FileOpenMode m_open_mode{hal::FileOpenMode::APPEND};

        // Shared I/O parameters
        const char* m_io_path{nullptr};

        // Write-specific parameters
        const uint8_t* m_write_data{nullptr};
        size_t m_write_len{0};
        SdWriteResult m_write_result{SdWriteResult::SUCCESS};

        // Read-specific parameters
        uint8_t* m_read_buffer{nullptr};
        size_t m_read_buffer_size{0};
        size_t m_bytes_read{0};
        SdReadResult m_read_result{SdReadResult::SUCCESS};

        // CreateDir-specific parameters
        SdCreateDirResult m_create_dir_result{SdCreateDirResult::SUCCESS};

        // Internal function to advance state
        void set_state(SdState next_state);

        /** Assert CS (drive LOW) via the signal callback. */
        void cs_assert();

        /** De-assert CS (drive HIGH) via the signal callback. */
        void cs_deassert();
};

#endif // SD_CONTROLLER_H
