#include <unity.h>
#include "stream_service.h"
#include "mock_stream_writer.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstring>
#include <memory>

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
 * @brief start_stream() writes the HTTP/multipart preamble.
 */
void test_start_stream_writes_preamble()
{
    TestFixture f;
    f.service.init(4);

    auto writer = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    MockStreamWriter* raw = writer.get();
    int slot = f.service.start_stream(std::move(writer));

    TEST_ASSERT_GREATER_OR_EQUAL(0, slot);
    TEST_ASSERT_EQUAL_UINT8(1, f.service.active_slot_count());

    // Preamble contains the multipart content type and boundary.
    TEST_ASSERT_NOT_NULL(strstr(raw->captured.c_str(),
                                "multipart/x-mixed-replace"));
    TEST_ASSERT_NOT_NULL(strstr(raw->captured.c_str(),
                                STREAM_BOUNDARY_TOKEN));
}

/**
 * @brief deliver_frame() writes a multipart JPEG part to a single stream.
 */
void test_deliver_frame_single_stream()
{
    TestFixture f;
    f.service.init(4);

    auto writer = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    MockStreamWriter* raw = writer.get();
    f.service.start_stream(std::move(writer));

    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));

    // The frame part includes the image/jpeg content type, the
    // Content-Length, and the raw JPEG bytes.
    TEST_ASSERT_NOT_NULL(strstr(raw->captured.c_str(), "image/jpeg"));
    TEST_ASSERT_NOT_NULL(strstr(raw->captured.c_str(), "Content-Length: 8"));
    // The JPEG bytes should be present in the captured output (binary-safe
    // find, since the frame bytes are not null-terminated).
    std::string jpeg(reinterpret_cast<const char*>(FAKE_JPEG), sizeof(FAKE_JPEG));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, raw->captured.find(jpeg));
}

/**
 * @brief deliver_frame() distributes to multiple active streams.
 */
void test_deliver_frame_multiple_streams()
{
    TestFixture f;
    f.service.init(4);

    auto w1 = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    auto w2 = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    MockStreamWriter* raw1 = w1.get();
    MockStreamWriter* raw2 = w2.get();
    f.service.start_stream(std::move(w1));
    f.service.start_stream(std::move(w2));

    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));

    TEST_ASSERT_GREATER_THAN(0, raw1->write_call_count);
    TEST_ASSERT_GREATER_THAN(0, raw2->write_call_count);
    TEST_ASSERT_EQUAL_UINT8(2, f.service.active_slot_count());
}

/**
 * @brief stop_stream() closes the writer and frees the slot.
 */
void test_stop_stream_closes_and_frees()
{
    TestFixture f;
    f.service.init(4);

    auto writer = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    int close_count = 0;
    writer->close_counter = &close_count;
    int slot = f.service.start_stream(std::move(writer));
    TEST_ASSERT_GREATER_OR_EQUAL(0, slot);

    f.service.stop_stream(static_cast<uint8_t>(slot));

    // The writer is deleted by stop_stream(), so observe close() via the
    // external counter rather than the (now dangling) writer pointer.
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
    TEST_ASSERT_EQUAL_INT(1, close_count);
}

/**
 * @brief stop_all_streams() closes every active writer and frees all slots.
 */
void test_stop_all_streams_closes_and_frees()
{
    TestFixture f;
    f.service.init(4);

    int close_count = 0;
    auto w1 = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    auto w2 = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    w1->close_counter = &close_count;
    w2->close_counter = &close_count;
    f.service.start_stream(std::move(w1));
    f.service.start_stream(std::move(w2));
    TEST_ASSERT_EQUAL_UINT8(2, f.service.active_slot_count());

    f.service.stop_all_streams();

    // All slots freed and every writer closed exactly once.
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
    TEST_ASSERT_EQUAL_INT(2, close_count);
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
        std::unique_ptr<hal::StreamWriter>(new MockStreamWriter())));
    TEST_ASSERT_GREATER_OR_EQUAL(0, f.service.start_stream(
        std::unique_ptr<hal::StreamWriter>(new MockStreamWriter())));

    // 3rd stream should be rejected (max 2).
    int slot = f.service.start_stream(
        std::unique_ptr<hal::StreamWriter>(new MockStreamWriter()));
    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT8(2, f.service.active_slot_count());
}

/**
 * @brief start_stream() rejects a null writer.
 */
void test_start_stream_null_writer()
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

    auto writer = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    MockStreamWriter* raw = writer.get();
    f.service.start_stream(std::move(writer));
    int calls_before = raw->write_call_count;

    f.service.deliver_frame(nullptr, 0);

    TEST_ASSERT_EQUAL_INT(calls_before, raw->write_call_count);
}

/**
 * @brief A client disconnect frees the slot without calling close().
 */
void test_disconnect_frees_slot()
{
    TestFixture f;
    f.service.init(4);

    auto writer = std::unique_ptr<MockStreamWriter>(new MockStreamWriter());
    int close_count = 0;
    writer->close_counter = &close_count;
    MockStreamWriter* raw = writer.get();
    int slot = f.service.start_stream(std::move(writer));
    TEST_ASSERT_GREATER_OR_EQUAL(0, slot);
    TEST_ASSERT_EQUAL_UINT8(1, f.service.active_slot_count());

    // Simulate the transport reporting a client disconnect.
    raw->simulate_disconnect();

    // The slot is freed (deleting the writer) and no close() was issued on
    // the dead connection.
    TEST_ASSERT_EQUAL_UINT8(0, f.service.active_slot_count());
    TEST_ASSERT_EQUAL_INT(0, close_count);
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
    RUN_TEST(test_start_stream_writes_preamble);
    RUN_TEST(test_deliver_frame_single_stream);
    RUN_TEST(test_deliver_frame_multiple_streams);
    RUN_TEST(test_stop_stream_closes_and_frees);
    RUN_TEST(test_stop_all_streams_closes_and_frees);
    RUN_TEST(test_stop_all_streams_no_active);
    RUN_TEST(test_start_stream_when_full);
    RUN_TEST(test_start_stream_null_writer);
    RUN_TEST(test_deliver_frame_empty);
    RUN_TEST(test_disconnect_frees_slot);

    return UNITY_END();
}
