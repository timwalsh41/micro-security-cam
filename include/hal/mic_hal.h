#ifndef HAL_MIC_HAL_H
#define HAL_MIC_HAL_H

#include <cstddef>
#include <cstdint>

/**
 * @file mic_hal.h
 * @brief Abstract interface for microphone hardware operations.
 *
 * Defines the contract for microphone initialization, sample capture,
 * and resource management. Hardware-specific implementations
 * and mocks (native) derive from this interface.
 */

namespace hal
{

/**
 * @brief Result of microphone initialization.
 */
enum class MicInitResult : uint8_t
{
    SUCCESS,            ///< Microphone initialized successfully
    HARDWARE_FAILURE    ///< Microphone hardware init failed
};

/**
 * @brief Configuration parameters for microphone initialization.
 */
struct MicConfig
{
    /** Sample rate in Hz. */
    uint32_t sample_rate{16000};

    /** Bits per sample. */
    uint16_t bits_per_sample{16};

    /** Number of channels (1 = mono). */
    uint16_t channels{1};

    /** Number of DMA buffers. */
    uint16_t dma_buf_count{4};

    /** Length of each DMA buffer in samples. */
    uint16_t dma_buf_len{1024};

    /** GPIO pin for I2S clock (PDM). */
    int ws_pin{42};

    /** GPIO pin for I2S data (PDM). */
    int sd_pin{41};

    /** Whether to use PDM mode. */
    bool use_pdm{true};
};

/**
 * @class MicHal
 * @brief Abstract interface for microphone hardware operations.
 *
 * Provides a mockable abstraction over the ESP32 I2S driver.
 * Exposes init/start/stop/deinit lifecycle and sample read.
 */
class MicHal
{
    public:
        virtual ~MicHal() = default;

        /**
         * @brief Initializes the microphone hardware.
         *
         * @param config Microphone configuration parameters.
         * @return MicInitResult indicating success or failure.
         */
        virtual MicInitResult init(const MicConfig& config) = 0;

        /**
         * @brief Starts the microphone capture.
         */
        virtual void start() = 0;

        /**
         * @brief Stops the microphone capture.
         */
        virtual void stop() = 0;

        /**
         * @brief Deinitializes the microphone hardware and releases resources.
         */
        virtual void deinit() = 0;

        /**
         * @brief Reads samples from the microphone into the buffer.
         *
         * @param buffer Pointer to the output buffer (int16_t samples).
         * @param count  Number of samples to read.
         * @return Number of samples actually read (may be less than count).
         */
        virtual size_t read_samples(int16_t* buffer, size_t count) = 0;

        /**
         * @brief Checks whether the microphone has been successfully initialized.
         *
         * @return true if the microphone is initialized and ready, false otherwise.
         */
        virtual bool is_initialized() const = 0;
};

} // namespace hal

#endif // HAL_MIC_HAL_H