#include "mic_service.h"
#include <cstring>
#include <cstdio>

#ifndef UNIT_TESTING

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

static void mic_task_func(void* param)
{
    static_cast<MicService*>(param)->mic_loop();
}

static void create_mic_task(MicService* service, void** handle)
{
    xTaskCreatePinnedToCore(mic_task_func, "mic_task", 4096,
                            service, 5,
                            static_cast<TaskHandle_t*>(handle), 1);
}

static void delete_mic_task(void* handle)
{
    if (handle != nullptr)
    {
        vTaskDelete(static_cast<TaskHandle_t>(handle));
    }
}

static void task_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void task_yield()
{
    taskYIELD();
}

#else

#include <thread>
#include <chrono>

static std::thread* s_mic_thread = nullptr;

static void create_mic_task(MicService* service, void** handle)
{
    s_mic_thread = new std::thread([service, handle]()
    {
        service->mic_loop();
        *handle = nullptr;
    });
}

static void delete_mic_task(void* handle)
{
    (void)handle;
    if (s_mic_thread != nullptr)
    {
        s_mic_thread->join();
        delete s_mic_thread;
        s_mic_thread = nullptr;
    }
}

static void task_delay_ms(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static void task_yield()
{
    std::this_thread::yield();
}

#endif

// ============================================================
// Construction
// ============================================================

MicService::MicService(hal::MicHal& hal, LoggingService& log,
                       ConfigService& config)
    : m_hal(hal)
    , m_log(log)
    , m_config(config)
{
}

// ============================================================
// Lifecycle
// ============================================================

bool MicService::init()
{
    // Read audio parameters from config
    const ConfigData::VideoConfig& vconfig = m_config.config().video;
    m_sample_rate = 16000;
    m_bits_per_sample = 16;
    m_channels = 1;

    // Allocate PSRAM ring buffer
    size_t ring_buffer_size = RING_BUFFER_SAMPLES * sizeof(int16_t);
#ifdef UNIT_TESTING
    m_ring_buffer = new int16_t[RING_BUFFER_SAMPLES];
#else
    m_ring_buffer = static_cast<int16_t*>(
        heap_caps_malloc(ring_buffer_size, MALLOC_CAP_SPIRAM));
    if (m_ring_buffer == nullptr)
    {
        // Fallback to regular heap
        m_ring_buffer = static_cast<int16_t*>(
            heap_caps_malloc(ring_buffer_size, MALLOC_CAP_8BIT));
    }
#endif

    if (m_ring_buffer == nullptr)
    {
        m_log.error(SUBSYSTEM, "Failed to allocate ring buffer");
        return false;
    }
    memset(m_ring_buffer, 0, ring_buffer_size);

    // Allocate read buffer for I2S reads
#ifdef UNIT_TESTING
    m_read_buffer = new int16_t[MAX_DMA_BUFFER_SAMPLES];
#else
    m_read_buffer = static_cast<int16_t*>(
        heap_caps_malloc(MAX_DMA_BUFFER_SAMPLES * sizeof(int16_t),
                         MALLOC_CAP_8BIT));
#endif

    if (m_read_buffer == nullptr)
    {
        m_log.error(SUBSYSTEM, "Failed to allocate read buffer");
#ifdef UNIT_TESTING
        delete[] m_ring_buffer;
#else
        heap_caps_free(m_ring_buffer);
#endif
        m_ring_buffer = nullptr;
        return false;
    }

    // Initialize hardware
    hal::MicConfig mic_config;
    mic_config.sample_rate = m_sample_rate;
    mic_config.bits_per_sample = m_bits_per_sample;
    mic_config.channels = m_channels;

    hal::MicInitResult result = m_hal.init(mic_config);
    if (result != hal::MicInitResult::SUCCESS)
    {
        m_log.error(SUBSYSTEM, "Mic hardware init failed");
#ifdef UNIT_TESTING
        delete[] m_ring_buffer;
        delete[] m_read_buffer;
#else
        heap_caps_free(m_ring_buffer);
        heap_caps_free(m_read_buffer);
#endif
        m_ring_buffer = nullptr;
        m_read_buffer = nullptr;
        return false;
    }

    m_write_idx.store(0, std::memory_order_relaxed);
    m_read_idx.store(0, std::memory_order_relaxed);

    m_log.info(SUBSYSTEM, "MicService initialized: %uHz %ubits %uchan",
               m_sample_rate, m_bits_per_sample, m_channels);
    return true;
}

bool MicService::start()
{
    if (m_capturing)
    {
        return true;
    }

    if (m_ring_buffer == nullptr)
    {
        m_log.error(SUBSYSTEM, "MicService not initialized");
        return false;
    }

    m_hal.start();
    m_capturing = true;

    create_mic_task(this, &m_task_handle);

    m_log.info(SUBSYSTEM, "Mic capture started");
    return true;
}

bool MicService::stop()
{
    if (!m_capturing)
    {
        return true;
    }

    m_capturing = false;

    delete_mic_task(m_task_handle);
    m_task_handle = nullptr;

    m_hal.stop();

    m_log.info(SUBSYSTEM, "Mic capture stopped");
    return true;
}

void MicService::deinit()
{
    if (m_capturing)
    {
        stop();
    }

    m_hal.deinit();

#ifdef UNIT_TESTING
    delete[] m_ring_buffer;
    delete[] m_read_buffer;
#else
    if (m_ring_buffer != nullptr)
    {
        heap_caps_free(m_ring_buffer);
    }
    if (m_read_buffer != nullptr)
    {
        heap_caps_free(m_read_buffer);
    }
#endif

    m_ring_buffer = nullptr;
    m_read_buffer = nullptr;

    m_log.info(SUBSYSTEM, "MicService deinitialized");
}

// ============================================================
// Audio chunk access (called by capture task)
// ============================================================

size_t MicService::get_audio_chunk(int16_t* buffer, size_t max_samples)
{
    if (buffer == nullptr || max_samples == 0)
    {
        return 0;
    }

    size_t available = ring_buffer_count();
    if (available == 0)
    {
        return 0;
    }

    size_t to_copy = (max_samples < available) ? max_samples : available;
    size_t read_pos = m_read_idx.load(std::memory_order_relaxed);

    for (size_t i = 0; i < to_copy; i++)
    {
        buffer[i] = m_ring_buffer[(read_pos + i) % RING_BUFFER_SAMPLES];
    }

    m_read_idx.store((read_pos + to_copy) % RING_BUFFER_SAMPLES,
                     std::memory_order_release);

    return to_copy;
}

size_t MicService::available_samples() const
{
    return ring_buffer_count();
}

// ============================================================
// WAV header generation
// ============================================================

bool MicService::generate_wav_header(uint8_t* header, size_t header_size,
                                     uint32_t data_size) const
{
    if (header == nullptr || header_size < 44)
    {
        return false;
    }

    WavHeader wav;
    wav.riff_size = 36 + data_size;
    wav.sample_rate = m_sample_rate;
    wav.byte_rate = m_sample_rate * m_bits_per_sample * m_channels / 8;
    wav.block_align = m_bits_per_sample * m_channels / 8;
    wav.bits_per_sample = m_bits_per_sample;
    wav.num_channels = m_channels;
    wav.data_size = data_size;

    memcpy(header, &wav, sizeof(WavHeader));
    return true;
}

// ============================================================
// Audio parameters
// ============================================================

uint32_t MicService::sample_rate() const
{
    return m_sample_rate;
}

uint16_t MicService::bits_per_sample() const
{
    return m_bits_per_sample;
}

uint16_t MicService::channels() const
{
    return m_channels;
}

uint32_t MicService::samples_per_second() const
{
    return m_sample_rate * m_channels;
}

uint16_t MicService::bytes_per_sample() const
{
    return (m_bits_per_sample * m_channels) / 8;
}

bool MicService::is_capturing() const
{
    return m_capturing;
}

// ============================================================
// Mic task (reads I2S into ring buffer)
// ============================================================

void MicService::mic_task_entry(void* param)
{
    static_cast<MicService*>(param)->mic_loop();
}

void MicService::mic_loop()
{
    while (m_capturing)
    {
        size_t samples_read = m_hal.read_samples(m_read_buffer, I2S_READ_SAMPLES);

        if (samples_read == 0)
        {
            task_delay_ms(1);
            continue;
        }

        // Apply gain
        uint8_t gain = m_config.config().video.mic_gain;
        if (gain > 1)
        {
            apply_gain(m_read_buffer, samples_read, gain);
        }

        // Write to ring buffer
        size_t write_pos = m_write_idx.load(std::memory_order_relaxed);
        for (size_t i = 0; i < samples_read; i++)
        {
            size_t pos = (write_pos + i) % RING_BUFFER_SAMPLES;
            m_ring_buffer[pos] = m_read_buffer[i];
        }
        m_write_idx.store((write_pos + samples_read) % RING_BUFFER_SAMPLES,
                          std::memory_order_release);

        task_delay_ms(1);  // Yield to other tasks
    }
}

// ============================================================
// Ring buffer helpers
// ============================================================

size_t MicService::ring_buffer_count() const
{
    size_t w = m_write_idx.load(std::memory_order_acquire);
    size_t r = m_read_idx.load(std::memory_order_acquire);
    if (w >= r)
    {
        return w - r;
    }
    else
    {
        return RING_BUFFER_SAMPLES - r + w;
    }
}

size_t MicService::ring_buffer_free() const
{
    return RING_BUFFER_SAMPLES - 1 - ring_buffer_count();
}

// ============================================================
// Gain
// ============================================================

void MicService::apply_gain(int16_t* buffer, size_t count, uint8_t gain)
{
    for (size_t i = 0; i < count; i++)
    {
        int32_t sample = static_cast<int32_t>(buffer[i]) * gain;
        // Clamp to int16_t range
        if (sample > 32767)
        {
            sample = 32767;
        }
        else if (sample < -32768)
        {
            sample = -32768;
        }
        buffer[i] = static_cast<int16_t>(sample);
    }
}