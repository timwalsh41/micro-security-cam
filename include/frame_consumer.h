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
 * Recording and streaming each subscribe as built-in sinks owned by
 * VideoController, bound by the executor when it executes the start
 * requests; register_frame_consumer() is the public API for
 * additional external consumers only.
 *
 * Lifecycle commands issued from within on_frame() (start_recording(),
 * stop_recording(), stop_streaming(), register/unregister) are safe:
 * they are fire-and-forget in the capture task's executor context —
 * queued and consumed at the next activation boundary — and never
 * block the capture task on its own execution.
 * A start triggered from on_frame() therefore binds for the next
 * activation. The one restriction that remains: a command issued from
 * on_frame() returns an acknowledgment, not the executed result (a
 * registration returns no usable id), and waiting on executor-owned
 * state from inside on_frame() is impossible by construction. When a
 * fire-and-forget command detaches the last consumer, the teardown
 * join is deferred to the next command task of any kind; connected
 * stream viewers are closed by the executor at the next activation
 * boundary so they never hang in the meantime.
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
