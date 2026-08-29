#ifndef FRAME_CONSUMER_H
#define FRAME_CONSUMER_H

#include <cstddef>
#include <cstdint>

/**
 * @file frame_consumer.h
 * @brief Frame-consumer interface for the capture pipeline.
 *
 * The capture loop is the sole frame producer. Interested activities
 * (recording, streaming) subscribe by
 * registering an IFrameConsumer implementation and receive every
 * captured JPEG frame while subscribed. on_frame() runs in the
 * capture task context and must not block indefinitely.
 *
 * on_frame() must never call unregister_frame_consumer(),
 * stop_recording(), or stop_streaming() when the caller could be the
 * last registered consumer: that tears down the capture scheduler from
 * inside the capture task itself, which blocks on joining its own task
 * until the bounded join times out and permanently wedges the
 * scheduler. Calling start_recording() from within on_frame() is safe
 * and is the sanctioned way for consumers to trigger recording: the
 * controller's internal lifecycle lock is never held across the bounded
 * teardown join, so the capture task can never block behind a joining
 * stop path and the short critical sections a start waits on always
 * complete without waiting on the capture task.
 */
class IFrameConsumer
{
    public:
        virtual ~IFrameConsumer() = default;

        /**
         * @brief Receives one captured JPEG frame.
         *
         * Called from the capture task for every frame while this
         * consumer is registered. Implementations must tolerate being
         * invoked after detachment begins (a frame already in flight).
         *
         * @param data Pointer to JPEG frame bytes.
         * @param len  Number of frame bytes.
         */
        virtual void on_frame(const uint8_t* data, size_t len) = 0;
};

#endif // FRAME_CONSUMER_H
