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
 * @brief Expected placeholder JPEG payload (the PLACEHOLDER_JPEG array
 *        in stream_service.h); kept local so the wire bytes are pinned
 *        by the test.
 */
static const uint8_t EXPECTED_PLACEHOLDER_JPEG[159] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46,
    0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0xFF, 0xDB, 0x00, 0x43, 0x00, 0x28, 0x1C, 0x1E, 0x23, 0x1E,
    0x19, 0x28, 0x23, 0x21, 0x23, 0x2D, 0x2B, 0x28, 0x30, 0x3C,
    0x64, 0x41, 0x3C, 0x37, 0x37, 0x3C, 0x7B, 0x58, 0x5D, 0x49,
    0x64, 0x91, 0x80, 0x99, 0x96, 0x8F, 0x80, 0x8C, 0x8A, 0xA0,
    0xB4, 0xE6, 0xC3, 0xA0, 0xAA, 0xDA, 0xAD, 0x8A, 0x8C, 0xC8,
    0xFF, 0xCB, 0xDA, 0xEE, 0xF5, 0xFF, 0xFF, 0xFF, 0x9B, 0xC1,
    0xFF, 0xFF, 0xFF, 0xFA, 0xFF, 0xE6, 0xFD, 0xFF, 0xF8, 0xFF,
    0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01,
    0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08,
    0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0x3F, 0xFF, 0xD9
};

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
 * @brief Strict multipart/x-mixed-replace wire parser.
 *
 * Consumes a sequence of complete parts exactly as a strict client
 * parser would: each part must be a boundary line, the two standard
 * headers with a decimal Content-Length, a blank line, exactly that
 * many payload bytes, and the CRLF footer; the sequence may end only
 * with the closing boundary. Any slack, truncation, or framing
 * deviation fails the parse (ok == false).
 */
struct StrictPartParser
{
    bool ok{true};
    std::vector<std::string> payloads;

    void parse(const std::string& wire)
    {
        size_t pos = 0;
        payloads.clear();
        ok = true;
        while (ok && pos < wire.size())
        {
            if (consume(wire, pos, "--CAM_BOUNDARY--\r\n"))
            {
                break;
            }
            if (!consume(wire, pos, "--CAM_BOUNDARY\r\n") ||
                !consume(wire, pos, "Content-Type: image/jpeg\r\n") ||
                !consume(wire, pos, "Content-Length: "))
            {
                ok = false;
                break;
            }

            size_t len = 0;
            bool have_digit = false;
            while (pos < wire.size() && wire[pos] >= '0' && wire[pos] <= '9')
            {
                len = len * 10 + static_cast<size_t>(wire[pos] - '0');
                have_digit = true;
                pos++;
            }
            if (!have_digit || !consume(wire, pos, "\r\n\r\n") ||
                pos + len > wire.size())
            {
                ok = false;
                break;
            }
            payloads.push_back(wire.substr(pos, len));
            pos += len;
            if (!consume(wire, pos, "\r\n"))
            {
                ok = false;
                break;
            }
        }
        if (!ok || pos != wire.size())
        {
            ok = false;
        }
    }

    private:
        static bool consume(const std::string& wire, size_t& pos,
                            const char* token)
        {
            size_t len = strlen(token);
            if (wire.compare(pos, len, token) != 0)
            {
                return false;
            }
            pos += len;
            return true;
        }
};

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
 * @brief start_stream() seeds the channel with a well-formed placeholder
 *        JPEG part: boundary line, image/jpeg content type, a
 *        Content-Length matching the payload, the payload, and the CRLF
 *        footer. The channel is never empty when the transport response
 *        attaches.
 */
void test_start_stream_seeds_placeholder_part()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    int slot = f.service.start_stream(channel);

    TEST_ASSERT_GREATER_OR_EQUAL(0, slot);
    TEST_ASSERT_EQUAL_UINT8(1, f.service.active_slot_count());
    TEST_ASSERT_TRUE(raw->has_pending_part());

    std::string part = drain(*raw);
    // The part is exactly: boundary line, image/jpeg content type,
    // Content-Length matching the payload, blank line, the placeholder
    // JPEG bytes, and the footer.
    static const char EXPECTED_HEADER[] =
        "--CAM_BOUNDARY\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: 159\r\n\r\n";
    const size_t header_len = sizeof(EXPECTED_HEADER) - 1;
    TEST_ASSERT_EQUAL_size_t(
        header_len + sizeof(EXPECTED_PLACEHOLDER_JPEG) + 2, part.size());
    TEST_ASSERT_EQUAL_MEMORY(EXPECTED_HEADER, part.c_str(), header_len);
    TEST_ASSERT_EQUAL_MEMORY(EXPECTED_PLACEHOLDER_JPEG,
                             part.c_str() + header_len,
                             sizeof(EXPECTED_PLACEHOLDER_JPEG));
    TEST_ASSERT_EQUAL_UINT8(0x0D, part[part.size() - 2]);
    TEST_ASSERT_EQUAL_UINT8(0x0A, part[part.size() - 1]);
    TEST_ASSERT_FALSE(raw->has_pending_part());
}

/**
 * @brief The seeded placeholder part is consumable by a strict
 *        multipart wire parser with no slack and the declared
 *        Content-Length exactly matching the payload.
 */
void test_start_stream_placeholder_is_strict_parser_consumable()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.service.start_stream(channel);

    StrictPartParser parser;
    parser.parse(drain(*raw));

    TEST_ASSERT_TRUE(parser.ok);
    TEST_ASSERT_EQUAL_UINT32(1, parser.payloads.size());
    TEST_ASSERT_EQUAL_size_t(sizeof(EXPECTED_PLACEHOLDER_JPEG),
                             parser.payloads[0].size());
    TEST_ASSERT_EQUAL_MEMORY(EXPECTED_PLACEHOLDER_JPEG,
                             parser.payloads[0].data(),
                             parser.payloads[0].size());
}

/**
 * @brief A full wire sequence — placeholder prime, real frames, closing
 *        boundary — is consumed end to end by the strict parser: every
 *        part is well-formed and payload counts line up.
 */
void test_stream_sequence_is_strict_parser_consumable()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    int slot = f.service.start_stream(channel);

    // Model the transport pulling each part in order as it is pushed.
    std::string wire = drain(*raw);
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    wire += drain(*raw);
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    wire += drain(*raw);
    f.service.stop_stream(static_cast<uint8_t>(slot));
    wire += drain(*raw);

    StrictPartParser parser;
    parser.parse(wire);

    TEST_ASSERT_TRUE(parser.ok);
    TEST_ASSERT_EQUAL_UINT32(3, parser.payloads.size());
    TEST_ASSERT_EQUAL_size_t(sizeof(EXPECTED_PLACEHOLDER_JPEG),
                             parser.payloads[0].size());
    TEST_ASSERT_EQUAL_size_t(sizeof(FAKE_JPEG), parser.payloads[1].size());
    TEST_ASSERT_EQUAL_size_t(sizeof(FAKE_JPEG),
                             parser.payloads[2].size());
}

/**
 * @brief The seeded placeholder part follows the freshest-frame mailbox
 *        semantics: a real frame pushed while it is still pending is
 *        dropped (the oldest part is kept), and once the placeholder is
 *        drained the next real frame goes through and replaces it.
 */
void test_real_frame_replaces_placeholder()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.service.start_stream(channel);
    TEST_ASSERT_TRUE(raw->has_pending_part());

    // The placeholder occupies the mailbox: a frame pushed before the
    // transport has pulled it is dropped.
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    TEST_ASSERT_TRUE(raw->has_pending_part());
    std::string part = drain(*raw);
    TEST_ASSERT_NOT_NULL(strstr(part.c_str(), "Content-Length: 159"));
    TEST_ASSERT_EQUAL_size_t(16 + 26 + 23 + sizeof(EXPECTED_PLACEHOLDER_JPEG) + 2,
                             part.size());

    // Once the placeholder is consumed, the next frame goes through
    // and becomes the pending part.
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    TEST_ASSERT_TRUE(raw->has_pending_part());
    part = drain(*raw);
    TEST_ASSERT_NOT_NULL(strstr(part.c_str(), "Content-Length: 8"));
    TEST_ASSERT_EQUAL_MEMORY(FAKE_JPEG,
                             part.c_str() + part.size() - sizeof(FAKE_JPEG) - 2,
                             sizeof(FAKE_JPEG));
}

/**
 * @brief deliver_frame() pushes a complete multipart JPEG part into the
 *        channel (after the seeded placeholder part is drained).
 */
void test_deliver_frame_single_stream()
{
    TestFixture f;
    f.service.init(4);

    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.service.start_stream(channel);
    drain(*raw);  // seeded placeholder part

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
    drain(*raw);  // seeded placeholder part

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
    drain(*raw);  // seeded placeholder part

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
    drain(*raw);  // seeded placeholder part

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
// Delivered-frame counter
// ============================================================

/**
 * @brief The delivered-frame counter starts at zero.
 */
void test_delivered_frames_counter_starts_at_zero()
{
    TestFixture f;

    TEST_ASSERT_EQUAL_UINT32(0, f.service.total_frames_delivered());
}

/**
 * @brief Each deliver_frame() counts once, with or without viewers;
 *        a null/empty delivery does not count.
 */
void test_delivered_frames_counter_counts_every_delivery()
{
    TestFixture f;

    // No viewers: the frame still counts (preview rate telemetry).
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    TEST_ASSERT_EQUAL_UINT32(1, f.service.total_frames_delivered());

    f.service.init(4);
    auto channel = std::make_shared<StreamFrameChannel>();
    int slot = f.service.start_stream(channel);
    TEST_ASSERT_TRUE(slot >= 0);

    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    f.service.deliver_frame(FAKE_JPEG, sizeof(FAKE_JPEG));
    TEST_ASSERT_EQUAL_UINT32(3, f.service.total_frames_delivered());

    // Rejected deliveries do not count.
    f.service.deliver_frame(nullptr, 0);
    TEST_ASSERT_EQUAL_UINT32(3, f.service.total_frames_delivered());
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
    RUN_TEST(test_start_stream_seeds_placeholder_part);
    RUN_TEST(test_start_stream_placeholder_is_strict_parser_consumable);
    RUN_TEST(test_real_frame_replaces_placeholder);
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
    RUN_TEST(test_stream_sequence_is_strict_parser_consumable);
    RUN_TEST(test_concurrent_slot_mutation_and_delivery);
    RUN_TEST(test_delivered_frames_counter_starts_at_zero);
    RUN_TEST(test_delivered_frames_counter_counts_every_delivery);

    return UNITY_END();
}
