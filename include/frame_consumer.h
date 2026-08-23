#ifndef FRAME_CONSUMER_H
#define FRAME_CONSUMER_H

#include <cstddef>
#include <cstdint>

/**
 * @file frame_consumer.h
 * @brief Frame-consumer interface for the capture pipeline.
 *
 * The capture loop is the sole frame producer. Interested activities
 * (recording, streaming, future motion detection) subscribe by
 * registering an IFrameConsumer implementation and receive every
 * captured JPEG frame while subscribed. on_frame() runs in the
 * capture task context and must not block indefinitely.
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
