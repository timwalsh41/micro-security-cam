#include <unity.h>
#include "stream_frame_channel.h"
#include <cstring>
#include <string>
#include <vector>

// ============================================================
// Helpers
// ============================================================

/**
 * @brief Drains the whole pending part from a channel.
 */
static std::string drain(StreamFrameChannel& channel)
{
    std::string out;
    uint8_t buf[64];
    size_t n;
    while ((n = channel.pull(buf, sizeof(buf))) > 0)
    {
        out.append(reinterpret_cast<const char*>(buf), n);
    }
    return out;
}

static const uint8_t PART[] = "--CAM_BOUNDARY\r\nContent-Length: 4\r\n\r\nDATA\r\n";

// ============================================================
// Tests
// ============================================================

/**
 * @brief A fresh channel is empty and open.
 */
void test_initial_state()
{
    StreamFrameChannel channel;

    TEST_ASSERT_FALSE(channel.has_pending_part());
    TEST_ASSERT_FALSE(channel.is_closed());

    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(0, channel.pull(buf, sizeof(buf)));
}

/**
 * @brief push_part() buffers a part; pull() returns it intact across
 *        partial pulls of any size.
 */
void test_push_then_pull_in_chunks()
{
    StreamFrameChannel channel;

    TEST_ASSERT_TRUE(channel.push_part(PART, sizeof(PART) - 1));
    TEST_ASSERT_TRUE(channel.has_pending_part());

    // Byte-by-byte pulls must reassemble the part exactly.
    std::string out;
    uint8_t byte;
    while (channel.pull(&byte, 1) == 1)
    {
        out.push_back(static_cast<char>(byte));
    }
    TEST_ASSERT_EQUAL_STRING_LEN(PART, out.c_str(), sizeof(PART) - 1);
    TEST_ASSERT_FALSE(channel.has_pending_part());
}

/**
 * @brief A single large pull returns the whole part; an oversized pull
 *        is capped at the pending size.
 */
void test_pull_caps_at_part_size()
{
    StreamFrameChannel channel;
    TEST_ASSERT_TRUE(channel.push_part(PART, sizeof(PART) - 1));

    uint8_t buf[512];
    size_t n = channel.pull(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(sizeof(PART) - 1, n);
    TEST_ASSERT_EQUAL_MEMORY(PART, buf, n);
    TEST_ASSERT_FALSE(channel.has_pending_part());
}

/**
 * @brief A push while the previous part is still pending is dropped and
 *        reported; the channel keeps the older part.
 */
void test_push_drops_when_part_pending()
{
    StreamFrameChannel channel;
    TEST_ASSERT_TRUE(channel.push_part(PART, sizeof(PART) - 1));

    const uint8_t second[] = "SECOND";
    TEST_ASSERT_FALSE(channel.push_part(second, sizeof(second) - 1));

    std::string out = drain(channel);
    TEST_ASSERT_EQUAL_STRING_LEN(PART, out.c_str(), sizeof(PART) - 1);

    // After the part is consumed, pushes are accepted again.
    TEST_ASSERT_TRUE(channel.push_part(second, sizeof(second) - 1));
    out = drain(channel);
    TEST_ASSERT_EQUAL_STRING_LEN("SECOND", out.c_str(), 6);
}

/**
 * @brief close() rejects further pushes; a pending part still drains.
 */
void test_close_drains_pending_then_ends()
{
    StreamFrameChannel channel;
    TEST_ASSERT_TRUE(channel.push_part(PART, sizeof(PART) - 1));
    channel.close();
    TEST_ASSERT_TRUE(channel.is_closed());

    // Pushes after close are rejected.
    TEST_ASSERT_FALSE(channel.push_part(PART, sizeof(PART) - 1));

    // The pending part still drains completely.
    std::string out = drain(channel);
    TEST_ASSERT_EQUAL_STRING_LEN(PART, out.c_str(), sizeof(PART) - 1);
    TEST_ASSERT_FALSE(channel.has_pending_part());

    // Once empty, the closed channel reports finished to the consumer.
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(0, channel.pull(buf, sizeof(buf)));
}

/**
 * @brief Degenerate arguments are rejected without corrupting state.
 */
void test_degenerate_arguments()
{
    StreamFrameChannel channel;

    TEST_ASSERT_FALSE(channel.push_part(nullptr, 4));
    TEST_ASSERT_FALSE(channel.push_part(PART, 0));

    TEST_ASSERT_EQUAL_size_t(0, channel.pull(nullptr, 4));
    uint8_t buf[4];
    TEST_ASSERT_EQUAL_size_t(0, channel.pull(buf, 0));

    TEST_ASSERT_FALSE(channel.has_pending_part());
}

/**
 * @brief Channel capacity is reused across parts (no unbounded growth).
 */
void test_capacity_reused()
{
    StreamFrameChannel channel;
    for (int i = 0; i < 100; i++)
    {
        TEST_ASSERT_TRUE(channel.push_part(PART, sizeof(PART) - 1));
        std::string out = drain(channel);
        TEST_ASSERT_EQUAL_size_t(sizeof(PART) - 1, out.size());
    }
    TEST_ASSERT_FALSE(channel.has_pending_part());
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_initial_state);
    RUN_TEST(test_push_then_pull_in_chunks);
    RUN_TEST(test_pull_caps_at_part_size);
    RUN_TEST(test_push_drops_when_part_pending);
    RUN_TEST(test_close_drains_pending_then_ends);
    RUN_TEST(test_degenerate_arguments);
    RUN_TEST(test_capacity_reused);

    return UNITY_END();
}
