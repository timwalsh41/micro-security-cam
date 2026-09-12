#include <unity.h>
#include "stream_service.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// Test fixture
// ============================================================

/**
 * @brief Sets up a StreamService backed by a mock logger.
 */
struct TestFixture
{
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    StreamService service;

    TestFixture()
        : logger(log_backend, time)
        , service(logger)
    {
    }
};

static const uint8_t FAKE_JPEG[8] = {0xFF, 0xD8, 0x01, 0x02, 0x03, 0x04, 0xFF, 0xD9};

/**
 * @brief Drains the whole pending part from a channel.
 */
static std::string drain(StreamFrameChannel& channel)
{
    std::string out;
    uint8_t buf[512];
    size_t n;
    while ((n = channel.pull(buf, sizeof(buf))) > 0)
    {
        out.append(reinterpret_cast<const char*>(buf), n);
    }
    return out;
}

// ============================================================
// Tests
// ============================================================

/**
 * @brief init() allocates the configured number of slots.
 */
void test_init_allocates_slots()
{
    TestFixture f;
    f.service.init(4);

    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
}

/**
 * @brief init() clamps max_streams above MAX_STREAMS.
 */
void test_init_clamps_max_streams()
{
    TestFixture f;
    f.service.init(255);

    // Should not crash; active count is still zero.
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
}

/**
 * @brief deliver_frame() with no active slots is a no-op.
 */
void test_deliver_frame_no_slots()
{
    TestFixture f;
    f.service.init(4);

    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));

    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
}

/**
 * @brief start_stream() pushes no bytes of its own: the channel stays
 *        empty until the first real multipart part is delivered, so the
 *        stream never begins with a standalone (empty) opening boundary
 *        part that strict multipart parsers reject.
 */
void test_start_stream_pushes_no_bytes()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    int slot = f.service.start_stream(channel);

    TEST_ASSERT_GREATER_OR_EQUAL(0, slot);
    TEST_ASSERT_EQUAL_UINT8(1, f.service.active_slot_count());
    TEST_ASSERT_FALSE(raw->has_pending_part());
    TEST_ASSERT_EQUAL_size_t(0, drain(*raw).size());
}

/**
 * @brief deliver_frame() pushes a complete multipart JPEG part into the
 *        channel.
 */
void test_deliver_frame_single_stream()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.service.start_stream(channel);

    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));

    std::string part = drain(*raw);
    // The part is exactly: boundary line, image/jpeg content type,
    // Content-Length, blank line, the raw JPEG bytes, and the footer.
    static const char EXPECTED_HEADER[] =
        "--CAM_BOUNDARY\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 8\r\n\r\n";
    const size_t header_len = sizeof(EXPECTED_HEADER) - 1;
    TEST_ASSERT_EQUAL_size_t(header_len + sizeof(FAKE_JPEG) + 2,
                             part.size());
    TEST_ASSERT_EQUAL_MEMORY(EXPECTED_HEADER, part.c_str(), header_len);
    TEST_ASSERT_EQUAL_MEMORY(FAKE_JPEG, part.c_str() + header_len,
                             sizeof(FAKE_JPEG));
    TEST_ASSERT_EQUAL_UINT8(0x0D, part[part.size() - 2]);
    TEST_ASSERT_EQUAL_UINT8(0x0A, part[part.size() - 1]);
}

/**
 * @brief deliver_frame() distributes to multiple active streams.
 */
void test_deliver_frame_multiple_streams()
{
    TestFixture f;
    f.service.init(4);

    auto c1 = std::make_shared<StreamFrameChannel>();
    auto c2 = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw1 = c1.get();
    StreamFrameChannel* raw2 = c2.get();
    f.service.start_stream(c1);
    f.service.start_stream(c2);

    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));

    TEST_ASSERT_TRUE(raw1->has_pending_part());
    TEST_ASSERT_TRUE(raw2->has_pending_part());
    TEST_ASSERT_EQUAL_UINT8(2, f.service.active_slot_count());
}

/**
 * @brief A viewer still draining its previous part drops the new frame
 *        instead of queueing it (bounded mailbox, live-preview drop
 *        semantics).
 */
void test_deliver_frame_drops_for_slow_viewer()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.service.start_stream(channel);

    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    TEST_ASSERT_TRUE(raw->has_pending_part());

    // Second frame while the first part is pending: dropped.
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));

    // The pending part is still the first one.
    std::string part = drain(*raw);
    TEST_ASSERT_NOT_NULL(strstr(part.c_str(), "Content-Length: 8"));

    // After the part is consumed, the next frame goes through.
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    TEST_ASSERT_TRUE(raw->has_pending_part());
    // Part = "--CAM_BOUNDARY\r\n" (16) + "Content-Type: image/jpeg\r\n" (26)
    // + "Content-Length: 8\r\n\r\n" (21) + frame bytes + CRLF footer (2).
    TEST_ASSERT_EQUAL_size_t(16 + 26 + 21 + sizeof(FAKE_JPEG) + 2,
                             drain(*raw).size());
}

/**
 * @brief stop_stream() ends the channel (after draining) and frees the
 *        slot immediately.
 */
void test_stop_stream_closes_and_frees()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    int slot = f.service.start_stream(channel);
    TEST_ASSERT_GREATER_OR_EQUAL(0, slot);

    f.service.stop_stream(static_cast<uint8_t>(slot));

    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
    TEST_ASSERT_TRUE(raw->is_closed());
    // The closing token is drained before the stream ends.
    std::string tail = drain(*raw);
    TEST_ASSERT_NOT_NULL(strstr(tail.c_str(), "--CAM_BOUNDARY--"));
    TEST_ASSERT_FALSE(raw->has_pending_part());
}

/**
 * @brief stop_all_streams() ends every active stream and frees all slots.
 */
void test_stop_all_streams_closes_and_frees()
{
    TestFixture f;
    f.service.init(4);

    auto c1 = std::make_shared<StreamFrameChannel>();
    auto c2 = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw1 = c1.get();
    StreamFrameChannel* raw2 = c2.get();
    f.service.start_stream(c1);
    f.service.start_stream(c2);
    TEST_ASSERT_EQUAL_UINT8(2, f.service.active_slot_count());

    f.service.stop_all_streams();

    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
    TEST_ASSERT_TRUE(raw1->is_closed());
    TEST_ASSERT_TRUE(raw2->is_closed());
}

/**
 * @brief stop_all_streams() with no active streams is a no-op.
 */
void test_stop_all_streams_no_active()
{
    TestFixture f;
    f.service.init(4);

    f.service.stop_all_streams();

    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
}

/**
 * @brief start_stream() returns -1 when all slots are full.
 */
void test_start_stream_when_full()
{
    TestFixture f;
    f.service.init(2);

    TEST_ASSERT_GREATER_OR_EQUAL(0, f.service.start_stream(
        std::make_shared<StreamFrameChannel>()));
    TEST_ASSERT_GREATER_OR_EQUAL(0, f.service.start_stream(
        std::make_shared<StreamFrameChannel>()));

    // 3rd stream should be rejected (max 2).
    int slot = f.service.start_stream(std::make_shared<StreamFrameChannel>());
    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT8(2, f.service.active_slot_count());
}

/**
 * @brief start_stream() rejects a null channel.
 */
void test_start_stream_null_channel()
{
    TestFixture f;
    f.service.init(4);

    int slot = f.service.start_stream(nullptr);
    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
}

/**
 * @brief deliver_frame() with a null/empty frame is a no-op.
 */
void test_deliver_frame_empty()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.service.start_stream(channel);

    f.service.deliver_frame(nullptr, 0);

    TEST_ASSERT_FALSE(raw->has_pending_part());
}

/**
 * @brief A transport-reported response end (disconnect or completion)
 *        frees the slot via on_stream_ended().
 */
void test_stream_end_frees_slot()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    int slot = f.service.start_stream(channel);
    TEST_ASSERT_GREATER_OR_EQUAL(0, slot);
    TEST_ASSERT_EQUAL_UINT8(1, f.service.active_slot_count());

    // Simulate the transport destroying the response (client
    // disconnect): the end callback frees the slot. The connection is
    // already gone, so the channel is not closed.
    f.service.on_stream_ended(raw);

    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
    TEST_ASSERT_FALSE(raw->is_closed());

    // Unknown or already-freed channels are a no-op.
    f.service.on_stream_ended(raw);
    f.service.on_stream_ended(nullptr);
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
}

/**
 * @brief on_stream_ended() after stop_stream() is idempotent.
 */
void test_stream_end_after_stop_is_noop()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    int slot = f.service.start_stream(channel);
    f.service.stop_stream(static_cast<uint8_t>(slot));
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());

    // The transport response ends later; the slot is already free.
    f.service.on_stream_ended(raw);
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
}

/**
 * @brief Regression for the cross-task slot race: start_stream() binding
 *        and on_stream_ended() reclamation (transport task) running
 *        concurrently with deliver_frame() pushes and stop_all_streams()
 *        (producer/stop contexts) can never interleave on the slot array
 *        — no lost bindings, no double reclamation, no torn delivery
 *        state.
 */
void test_concurrent_slot_mutation_and_delivery()
{
    constexpr int ROUNDS = 50;

    for (int round = 0; round < ROUNDS; round++)
    {
        TestFixture f;
        f.service.init(4);

        std::atomic<bool> go{false};
        std::atomic<bool> churn_done{false};

        // Churn thread (stands in for the transport task): binds a
        // viewer then reclaims its slot via the two possible paths —
        // an explicit stop, or the response-end callback (disconnect).
        std::thread churn([&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int i = 0; i < 50; i++)
            {
                auto channel = std::make_shared<StreamFrameChannel>();
                StreamFrameChannel* raw = channel.get();
                int slot = f.service.start_stream(channel);
                if (slot < 0)
                {
                    continue;
                }
                if (i % 2 == 0)
                {
                    f.service.stop_stream(static_cast<uint8_t>(slot));
                }
                else
                {
                    f.service.on_stream_ended(raw);
                }
            }
            churn_done.store(true, std::memory_order_release);
        });

        // Delivery thread (stands in for the frame producer task):
        // pushes frames into whatever slots are active, snapshotting
        // the registry under the slot mutex on every call.
        std::thread deliver([&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            while (!churn_done.load(std::memory_order_acquire))
            {
                f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
                std::this_thread::yield();
            }
        });

        go.store(true, std::memory_order_release);
        churn.join();
        deliver.join();

        // Every bound slot was reclaimed exactly once; the registry is
        // empty and stops cleanly.
        TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
        f.service.stop_all_streams();
        TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
    }
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_init_allocates_slots);
    RUN_TEST(test_init_clamps_max_streams);
    RUN_TEST(test_deliver_frame_no_slots);
    RUN_TEST(test_start_stream_pushes_no_bytes);
    RUN_TEST(test_deliver_frame_single_stream);
    RUN_TEST(test_deliver_frame_multiple_streams);
    RUN_TEST(test_deliver_frame_drops_for_slow_viewer);
    RUN_TEST(test_stop_stream_closes_and_frees);
    RUN_TEST(test_stop_all_streams_closes_and_frees);
    RUN_TEST(test_stop_all_streams_no_active);
    RUN_TEST(test_start_stream_when_full);
    RUN_TEST(test_start_stream_null_channel);
    RUN_TEST(test_deliver_frame_empty);
    RUN_TEST(test_stream_end_frees_slot);
    RUN_TEST(test_stream_end_after_stop_is_noop);
    RUN_TEST(test_concurrent_slot_mutation_and_delivery);

    return UNITY_END();
}
