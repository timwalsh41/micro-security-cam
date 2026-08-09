#ifndef MOCK_CAPTURE_SCHEDULER_H
#define MOCK_CAPTURE_SCHEDULER_H

#include "capture_scheduler.h"

/**
 * @file mock_capture_scheduler.h
 * @brief Deterministic mock ICaptureScheduler for native unit tests.
 *
 * Stores the frame callback and invokes it synchronously when
 * on_frame_capture() is called. Provides call tracking for test
 * assertions.
 */

class MockCaptureScheduler : public ICaptureScheduler
{
    public:
        // ============================================================
        // Call tracking
        // ============================================================

        /** Number of times start() has been called. */
        int start_call_count{0};

        /** Number of times stop() has been called. */
        int stop_call_count{0};

        /** Number of times on_frame_capture() has been called. */
        int trigger_count{0};

        // ============================================================
        // Interface implementation
        // ============================================================

        void start(uint32_t interval_ms,
                   std::function<void()> frame_callback) override
        {
            (void)interval_ms;
            m_callback = frame_callback;
            m_started = true;
            start_call_count++;
        }

        void stop() override
        {
            m_started = false;
            stop_call_count++;
        }

        void on_frame_capture() override
        {
            trigger_count++;
            if (m_callback)
            {
                m_callback();
            }
        }

        /**
         * @brief Returns whether the scheduler is in the started state.
         */
        bool is_started() const
        {
            return m_started;
        }

    private:
        std::function<void()> m_callback;
        bool m_started{false};
};

#endif // MOCK_CAPTURE_SCHEDULER_H
