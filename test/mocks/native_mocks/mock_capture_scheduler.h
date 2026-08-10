#ifndef MOCK_CAPTURE_SCHEDULER_H
#define MOCK_CAPTURE_SCHEDULER_H

#include "capture_scheduler.h"

/**
 * @file mock_capture_scheduler.h
 * @brief Deterministic mock ICaptureScheduler for native unit tests.
 *
 * Stores the frame callback and invokes it synchronously when
 * on_frame_capture() is called. Mirrors the real scheduler's
 * synchronous stop contract: stop() invokes the stored callback once
 * (the "final frame") before marking the scheduler stopped. Provides
 * call tracking for test assertions.
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
        // Configurable behavior
        // ============================================================

        /** Value returned by the first start() call (simulates scheduler failure). */
        bool start_result{true};

        // ============================================================
        // Interface implementation
        // ============================================================

        bool start(uint32_t interval_ms,
                   std::function<void()> frame_callback) override
        {
            (void)interval_ms;
            if (m_started)
            {
                // Already running — do not overwrite the stored callback
                return false;
            }
            start_call_count++;
            if (!start_result)
            {
                return false;
            }
            m_callback = frame_callback;
            m_started = true;
            return true;
        }

        void stop() override
        {
            stop_call_count++;
            if (!m_started)
            {
                return;
            }

            // Synchronous stop contract: invoke the final callback once
            // so the consumer can process pending clip closure before the
            // scheduler stops.
            if (m_callback)
            {
                m_callback();
            }
            m_started = false;
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
