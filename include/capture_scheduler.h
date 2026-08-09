#ifndef CAPTURE_SCHEDULER_H
#define CAPTURE_SCHEDULER_H

#include <cstdint>
#include <functional>

/**
 * @file capture_scheduler.h
 * @brief Abstract interface for frame capture scheduling.
 *
 * Defines the contract for triggering periodic frame captures.
 *
 * Platform-specific implementations live in separate translation
 * units.
 */

/**
 * @class ICaptureScheduler
 * @brief Abstract interface for periodic frame capture scheduling.
 *
 * The scheduler manages the lifecycle of a background capture
 * mechanism and translates timer ISR events into frame callbacks.
 * VideoController delegates all scheduling concerns to this
 * interface, keeping itself free of platform-specific code.
 */
class ICaptureScheduler
{
    public:
        virtual ~ICaptureScheduler() = default;

        /**
         * @brief Starts the capture scheduler.
         *
         * Configures the periodic capture at the given interval and
         * registers the callback to invoke on each frame trigger.
         *
         * @param interval_ms  Capture interval in milliseconds.
         * @param frame_callback  Function to call on each frame capture.
         */
        virtual void start(uint32_t interval_ms,
                           std::function<void()> frame_callback) = 0;

        /**
         * @brief Stops the capture scheduler and releases resources.
         */
        virtual void stop() = 0;

        /**
         * @brief Called when a frame capture should be triggered.
         *
         * Must be safe to call from interrupt context.
         */
        virtual void on_frame_capture() = 0;
};

#endif // CAPTURE_SCHEDULER_H
