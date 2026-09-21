#ifndef MOCK_MIC_HAL_H
#define MOCK_MIC_HAL_H

#include "hal/mic_hal.h"
#include <cstring>
#include <vector>

/**
 * @file mock_mic_hal.h
 * @brief Configurable mock MicHal for native unit tests.
 *
 * Provides full control over return values and call tracking
 * so that MicService can be tested deterministically on the
 * host machine.
 */
class MockMicHal : public hal::MicHal
{
    public:
        // ============================================================
        // Configurable return values (set before calling the method)
        // ============================================================

        /** Value returned by init(). */
        hal::MicInitResult init_result{hal::MicInitResult::SUCCESS};

        /** Value returned by is_initialized(). */
        bool initialized_state{true};

        /** Configurable sample data returned by read_samples(). */
        std::vector<int16_t> sample_data;

        /** Number of samples to return per read_samples() call.
         *  If 0, returns all available sample_data at once. */
        size_t samples_per_read{0};

        // ============================================================
        // Call tracking (inspect after calling the method)
        // ============================================================

        /** Number of times init() has been called. */
        int init_call_count{0};

        /** Number of times start() has been called. */
        int start_call_count{0};

        /** Number of times stop() has been called. */
        int stop_call_count{0};

        /** Number of times deinit() has been called. */
        int deinit_call_count{0};

        /** Number of times read_samples() has been called. */
        int read_samples_call_count{0};

        /** Total samples read across all calls. */
        size_t total_samples_read{0};

        /** The MicConfig passed to the most recent init() call. */
        hal::MicConfig last_init_config{};

        // ============================================================
        // Interface implementation
        // ============================================================

        hal::MicInitResult init(const hal::MicConfig& config) override
        {
            init_call_count++;
            last_init_config = config;
            return init_result;
        }

        void start() override
        {
            start_call_count++;
        }

        void stop() override
        {
            stop_call_count++;
        }

        void deinit() override
        {
            deinit_call_count++;
            initialized_state = false;
        }

        size_t read_samples(int16_t* buffer, size_t count) override
        {
            read_samples_call_count++;

            if (sample_data.empty() || buffer == nullptr)
            {
                return 0;
            }

            // Return either samples_per_read or all available data
            size_t to_read = count;
            if (samples_per_read > 0 && to_read > samples_per_read)
            {
                to_read = samples_per_read;
            }
            if (to_read > sample_data.size() - m_read_offset)
            {
                to_read = sample_data.size() - m_read_offset;
            }

            memcpy(buffer, &sample_data[m_read_offset],
                   to_read * sizeof(int16_t));
            m_read_offset += to_read;
            total_samples_read += to_read;
            return to_read;
        }

        bool is_initialized() const override
        {
            return initialized_state;
        }

        // ============================================================
        // Test helpers
        // ============================================================

        /** Resets the read offset to start from the beginning. */
        void reset_read_offset()
        {
            m_read_offset = 0;
        }

        /** Clears all sample data and resets read offset. */
        void clear_sample_data()
        {
            sample_data.clear();
            m_read_offset = 0;
        }

        /** Fills sample_data with a configurable number of samples. */
        void fill_sample_data(size_t count, int16_t value)
        {
            sample_data.resize(count, value);
            m_read_offset = 0;
        }

        /** Fills sample_data with incrementing values. */
        void fill_incremental_sample_data(size_t count, int16_t start = 0)
        {
            sample_data.resize(count);
            for (size_t i = 0; i < count; i++)
            {
                sample_data[i] = static_cast<int16_t>(start + i);
            }
            m_read_offset = 0;
        }

    private:
        size_t m_read_offset{0};
};

#endif // MOCK_MIC_HAL_H