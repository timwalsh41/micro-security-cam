#ifndef MIC_SERVICE_H
#define MIC_SERVICE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "hal/mic_hal.h"
#include "logging_service.h"
#include "config_service.h"
#include "mic_task_runner.h"
#include "memory_allocator.h"

/**
 * @file mic_service.h
 * @brief Microphone service that reads I2S audio into a PSRAM ring buffer.
 *
 * The MicService manages a PSRAM ring buffer that the capture task
 * drains at activation boundaries. The mic_task reads I2S samples into
 * the ring buffer; the capture task reads audio chunks from the ring
 * buffer and writes them to the AVI file.
 *
 * The mic task writes to ring buffer only; the capture task is the sole SD writer.
 *
 * Audio parameters: 16kHz, 16-bit, mono (32 KB/s).
 * PSRAM ring buffer: ~1.9 MB for 60s clips.
 */

/**
 * @brief WAV file header structure.
 *
 * Used for generating clip metadata or temporary WAV files.
 */
struct WavHeader
{
    /** Default audio sample rate (Hz). */
    static constexpr uint32_t DEFAULT_SAMPLE_RATE = 16000;

    /** Default bits per sample. */
    static constexpr uint16_t DEFAULT_BITS_PER_SAMPLE = 16;

    /** Default number of channels. */
    static constexpr uint16_t DEFAULT_NUM_CHANNELS = 1;

    /** RIFF header. */
    char riff_id[4]{'R', 'I', 'F', 'F'};
    uint32_t riff_size{0};
    char wave_id[4]{'W', 'A', 'V', 'E'};

    /** fmt chunk. */
    char fmt_id[4]{'f', 'm', 't', ' '};
    uint32_t fmt_size{16};
    uint16_t audio_format{1};    // PCM
    uint16_t num_channels{DEFAULT_NUM_CHANNELS};
    uint32_t sample_rate{DEFAULT_SAMPLE_RATE};
    uint32_t byte_rate{DEFAULT_SAMPLE_RATE * DEFAULT_BITS_PER_SAMPLE * DEFAULT_NUM_CHANNELS / 8};
    uint16_t block_align{DEFAULT_BITS_PER_SAMPLE * DEFAULT_NUM_CHANNELS / 8};
    uint16_t bits_per_sample{DEFAULT_BITS_PER_SAMPLE};

    /** data chunk. */
    char data_id[4]{'d', 'a', 't', 'a'};
    uint32_t data_size{0};
};

/**
 * @class MicService
 * @brief Manages microphone lifecycle, PSRAM ring buffer, and gain control.
 *
 * The MicService reads I2S samples via MicHal into a PSRAM ring buffer.
 * The capture task drains audio from the ring buffer at frame boundaries
 * and writes audio chunks to the AVI file.
 *
 * Lifecycle: init() -> start() -> [mic_task reads I2S into ring buffer]
 *          -> get_audio_chunk() called by capture task at frame boundaries
 *          -> stop() -> deinit().
 *
 * Thread safety: mic_task reads from MicHal and writes to ring buffer;
 * capture task reads from ring buffer at frame boundaries. The ring
 * buffer uses atomic indices for lock-free access.
 */
class MicService
{
    public:
        /** @brief Audio sample rate in Hz. */
        static constexpr uint32_t SAMPLE_RATE = 16000;

        /**
         * @brief Constructs the MicService with injected dependencies.
         *
         * @param hal        Reference to MicHal for I2S hardware access.
         * @param log        Reference to LoggingService for diagnostics.
         * @param config     Reference to ConfigService for gain settings.
         * @param task_runner Reference to IMicTaskRunner for task lifecycle.
         * @param allocator  Reference to IMemoryAllocator for memory allocation.
         */
        MicService(hal::MicHal& hal, LoggingService& log,
                   ConfigService& config,
                   IMicTaskRunner& task_runner,
                   IMemoryAllocator& allocator);

        /**
         * @brief Initializes the microphone and ring buffer.
         *
         * @return true if initialization succeeded.
         */
        bool init();

        /**
         * @brief Starts the microphone capture (creates mic_task).
         *
         * @return true if capture started successfully.
         */
        bool start();

        /**
         * @brief Stops the microphone capture and waits for mic_task to exit.
         *
         * @return true if capture stopped successfully.
         */
        bool stop();

        /**
         * @brief Deinitializes the microphone and frees resources.
         */
        void deinit();

        /**
         * @brief Drains available audio from the ring buffer.
         *
         * Called by the capture task at each frame activation boundary.
         * Copies audio data into the provided buffer.
         *
         * @param buffer    Output buffer for audio samples.
         * @param max_samples Maximum number of samples to return.
         * @return Number of samples actually copied (may be less than max_samples).
         */
        size_t get_audio_chunk(int16_t* buffer, size_t max_samples);

        /**
         * @brief Returns the number of audio samples available in the ring buffer.
         *
         * @return Number of available samples.
         */
        size_t available_samples() const;

        /**
         * @brief Generates a WAV header for the current audio parameters.
         *
         * The header includes a placeholder data size that must be
         * updated when the WAV file is finalized.
         *
         * @param header       Output buffer for the WAV header (44 bytes).
         * @param header_size  Size of the output buffer.
         * @param data_size    Total audio data size in bytes.
         * @return true if the header was generated successfully.
         */
        bool generate_wav_header(uint8_t* header, size_t header_size,
                                 uint32_t data_size) const;

        /**
         * @brief Returns the sample rate.
         */
        uint32_t sample_rate() const;

        /**
         * @brief Returns the bits per sample.
         */
        uint16_t bits_per_sample() const;

        /**
         * @brief Returns the number of channels.
         */
        uint16_t channels() const;

        /**
         * @brief Returns the number of samples per second (sample_rate * channels).
         */
        uint32_t samples_per_second() const;

        /**
         * @brief Returns the bytes per sample (bits_per_sample * channels / 8).
         */
        uint16_t bytes_per_sample() const;

        /**
         * @brief Returns whether the microphone is currently capturing.
         */
        bool is_capturing() const;

        /**
         * @brief Mic task main loop (reads I2S into ring buffer).
         *
         * Called by the mic task entry point. This method runs in
         * the mic task context and is not safe to call from other
         * tasks while the mic task is running.
         */
        void mic_loop();

    private:

        static constexpr const char* SUBSYSTEM = "MIC";

        /**
         * @brief Applies gain to audio samples.
         *
         * @param buffer   Samples to apply gain to.
         * @param count    Number of samples.
         * @param gain     Gain factor (1-8).
         */
        void apply_gain(int16_t* buffer, size_t count, uint8_t gain);

        /** Ring buffer size (in samples) for ~60s at the selected sample rate. */
        static constexpr size_t RING_BUFFER_SAMPLES = SAMPLE_RATE*60; 

        /** Number of samples per I2S read. */
        static constexpr size_t I2S_READ_SAMPLES = 1024;

        /** Max DMA buffer size for I2S read. */
        static constexpr size_t MAX_DMA_BUFFER_SAMPLES = 1024;

        // Dependencies
        hal::MicHal& m_hal;
        LoggingService& m_log;
        ConfigService& m_config;
        IMicTaskRunner& m_task_runner;
        IMemoryAllocator& m_memory_allocator;

        // PSRAM ring buffer
        int16_t* m_ring_buffer{nullptr};  ///< PSRAM-backed ring buffer
        std::atomic<size_t> m_write_idx{0};  ///< Write position in ring buffer
        std::atomic<size_t> m_read_idx{0};   ///< Read position in ring buffer

        // Capture state
        volatile bool m_capturing{false};  ///< Whether capture is active

        // Read buffer (for I2S reads, owned by mic_task)
        int16_t* m_read_buffer{nullptr};

        // Cached audio parameters (from config)
        uint32_t m_sample_rate{SAMPLE_RATE};
        uint16_t m_bits_per_sample{16};
        uint16_t m_channels{1};

        /**
         * @brief Returns the number of samples in the ring buffer.
         */
        size_t ring_buffer_count() const;

        /**
         * @brief Returns the number of free slots in the ring buffer.
         */
        size_t ring_buffer_free() const;
};

#endif // MIC_SERVICE_H