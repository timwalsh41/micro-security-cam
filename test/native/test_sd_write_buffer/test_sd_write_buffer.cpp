#include <unity.h>
#include "sd_write_buffer.h"
#include "sd_controller.h"
#include "time_service.h"
#include "mock_sd_card_hal.h"
#include <cstring>
#include <vector>
#include <Arduino.h>
#include "hal/time_hal.h"

using hal::millis_t;

// ============================================================
// Test helpers
// ============================================================

/**
 * @brief Helper: build a fake AVI frame chunk with the given payload size.
 *
 * Creates a chunk with the 0x00dc marker, size field, and payload data.
 * The chunk is DWORD-aligned (padded with zeros if needed).
 *
 * @param payloadSize Size of the fake JPEG payload (excluding 8-byte header).
 * @param pattern Fill byte for the payload.
 * @param outChunk Output vector containing the formatted chunk.
 */
static void build_fake_chunk(size_t payloadSize, uint8_t pattern,
                              std::vector<uint8_t> &outChunk)
{
    // 8-byte AVI chunk header + payload, DWORD-aligned
    size_t rawSize = 8 + payloadSize;
    size_t alignedSize = (rawSize + 3) & ~static_cast<size_t>(3);
    outChunk.resize(alignedSize, 0);

    // Write chunk marker: 0x00dc
    uint32_t marker = 0x00dc;
    memcpy(&outChunk[0], &marker, 4);

    // Write data size (payload only)
    uint32_t dataSize = static_cast<uint32_t>(payloadSize);
    memcpy(&outChunk[4], &dataSize, 4);

    // Fill payload
    memset(&outChunk[8], pattern, payloadSize);
}

// ============================================================
// Default state
// ============================================================

/**
 * @brief Before open(), is_open() returns false.
 */
void test_initial_not_open()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);

    TEST_ASSERT_FALSE(buf.is_open());
}

/**
 * @brief Before open(), buffered_bytes() returns 0.
 */
void test_initial_buffered_bytes_zero()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);

    TEST_ASSERT_EQUAL_UINT32(0, buf.buffered_bytes());
}

// ============================================================
// open() — success / failure
// ============================================================

/**
 * @brief open() with valid path returns true and sets is_open().
 */
void test_open_success()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);

    TEST_ASSERT_TRUE(buf.open("/test.avi"));
    TEST_ASSERT_TRUE(buf.is_open());
}

/**
 * @brief open() with null path returns false.
 */
void test_open_null_path_returns_false()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);

    TEST_ASSERT_FALSE(buf.open(nullptr));
    TEST_ASSERT_FALSE(buf.is_open());
}

// ============================================================
// buffer_frame() — basic buffering
// ============================================================

/**
 * @brief buffer_frame() before open() returns false.
 */
void test_buffer_frame_before_open_returns_false()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);

    std::vector<uint8_t> chunk;
    build_fake_chunk(100, 0xAB, chunk);
    TEST_ASSERT_FALSE(buf.buffer_frame(chunk.data(), chunk.size()));
}

/**
 * @brief buffer_frame() with null data returns false.
 */
void test_buffer_frame_null_data_returns_false()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    TEST_ASSERT_FALSE(buf.buffer_frame(nullptr, 10));
}

/**
 * @brief buffer_frame() with zero length returns false.
 */
void test_buffer_frame_zero_length_returns_false()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    uint8_t data[10] = {};
    TEST_ASSERT_FALSE(buf.buffer_frame(data, 0));
}

/**
 * @brief A small frame is buffered (no SD write yet).
 */
void test_buffer_frame_accumulates_in_ram()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    std::vector<uint8_t> chunk;
    build_fake_chunk(100, 0xAB, chunk);
    TEST_ASSERT_TRUE(buf.buffer_frame(chunk.data(), chunk.size()));

    // Data should be in the buffer, not yet flushed to SD
    TEST_ASSERT_EQUAL(chunk.size(), buf.buffered_bytes());
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);
}

/**
 * @brief Multiple small frames accumulate correctly.
 */
void test_multiple_frames_accumulate()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    std::vector<uint8_t> chunk1, chunk2;
    build_fake_chunk(100, 0xAA, chunk1);
    build_fake_chunk(200, 0xBB, chunk2);

    buf.buffer_frame(chunk1.data(), chunk1.size());
    TEST_ASSERT_EQUAL(chunk1.size(), buf.buffered_bytes());

    buf.buffer_frame(chunk2.data(), chunk2.size());
    TEST_ASSERT_EQUAL(chunk1.size() + chunk2.size(), buf.buffered_bytes());

    // Still no SD writes
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);
}

// ============================================================
// Auto-flush at RAMSIZE threshold
// ============================================================

/**
 * @brief When buffer exceeds RAMSIZE, data is flushed to SD.
 */
void test_auto_flush_at_ramsize_threshold()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Fill buffer to just below RAMSIZE
    size_t fillSize = SdWriteBuffer::RAMSIZE - 100;
    std::vector<uint8_t> chunk1;
    build_fake_chunk(fillSize - 8, 0xAA, chunk1);  // -8 for chunk header
    buf.buffer_frame(chunk1.data(), chunk1.size());
    TEST_ASSERT_EQUAL(chunk1.size(), buf.buffered_bytes());
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);

    // Add a chunk that pushes past RAMSIZE → triggers flush
    std::vector<uint8_t> chunk2;
    build_fake_chunk(200, 0xBB, chunk2);
    buf.buffer_frame(chunk2.data(), chunk2.size());

    // The first chunk should have been flushed, buffer now holds chunk2
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/test.avi", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(chunk2.size(), buf.buffered_bytes());
}

/**
 * @brief Chunk exactly at RAMSIZE boundary triggers flush.
 */
void test_auto_flush_at_exact_ramsize()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Add a chunk that exactly fills RAMSIZE
    std::vector<uint8_t> chunk;
    build_fake_chunk(SdWriteBuffer::RAMSIZE - 8, 0xCC, chunk);
    buf.buffer_frame(chunk.data(), chunk.size());

    // Should have been flushed (highPoint reached RAMSIZE)
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, buf.buffered_bytes());
}

// ============================================================
// Oversized frame (larger than RAMSIZE)
// ============================================================

/**
 * @brief A chunk larger than RAMSIZE is written directly to SD.
 */
void test_oversized_chunk_written_directly()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Create a chunk larger than RAMSIZE
    std::vector<uint8_t> bigChunk;
    build_fake_chunk(SdWriteBuffer::RAMSIZE + 100, 0xDD, bigChunk);
    TEST_ASSERT_TRUE(buf.buffer_frame(bigChunk.data(), bigChunk.size()));

    // Should have been written directly, buffer stays empty
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, buf.buffered_bytes());
}

/**
 * @brief Oversized chunk flushes any pending data first.
 */
void test_oversized_chunk_flushes_pending_first()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Buffer some data first
    std::vector<uint8_t> smallChunk;
    build_fake_chunk(100, 0xAA, smallChunk);
    buf.buffer_frame(smallChunk.data(), smallChunk.size());
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);

    // Now add an oversized chunk → should flush pending, then write directly
    std::vector<uint8_t> bigChunk;
    build_fake_chunk(SdWriteBuffer::RAMSIZE + 50, 0xBB, bigChunk);
    buf.buffer_frame(bigChunk.data(), bigChunk.size());

    // Two SD writes: one for pending data, one for the oversized chunk
    TEST_ASSERT_EQUAL_INT(2, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, buf.buffered_bytes());
}

// ============================================================
// flush() — manual flush
// ============================================================

/**
 * @brief flush() with no data returns true (no-op).
 */
void test_flush_empty_returns_true()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    TEST_ASSERT_TRUE(buf.flush());
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);
}

/**
 * @brief flush() before open() returns false.
 */
void test_flush_before_open_returns_false()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);

    TEST_ASSERT_FALSE(buf.flush());
}

/**
 * @brief flush() writes buffered data to SD and resets buffer.
 */
void test_flush_writes_to_sd()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Buffer a chunk
    std::vector<uint8_t> chunk;
    build_fake_chunk(100, 0xAB, chunk);
    buf.buffer_frame(chunk.data(), chunk.size());
    TEST_ASSERT_EQUAL(chunk.size(), buf.buffered_bytes());

    // Flush
    TEST_ASSERT_TRUE(buf.flush());
    TEST_ASSERT_EQUAL_UINT32(0, buf.buffered_bytes());
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/test.avi", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(hal::FileOpenMode::WRITE, sd_hal.last_open_mode);

    // Verify the data written matches the chunk
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(chunk.size(), sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk.data(), sd_hal.last_write_data, chunk.size());
}

/**
 * @brief flush() after partial buffer still writes correctly.
 */
void test_flush_after_partial_fill()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Buffer a chunk smaller than RAMSIZE
    std::vector<uint8_t> chunk;
    build_fake_chunk(50, 0xCC, chunk);
    buf.buffer_frame(chunk.data(), chunk.size());

    // Flush
    TEST_ASSERT_TRUE(buf.flush());
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(chunk.size(), sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk.data(), sd_hal.last_write_data, chunk.size());
}

// ============================================================
// Multiple flush cycles
// ============================================================

/**
 * @brief Multiple flush cycles produce correct sequential writes.
 */
void test_multiple_flush_cycles()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // First cycle: buffer + flush
    std::vector<uint8_t> chunk1;
    build_fake_chunk(100, 0x11, chunk1);
    buf.buffer_frame(chunk1.data(), chunk1.size());
    buf.flush();
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);

    // Second cycle: buffer + flush
    std::vector<uint8_t> chunk2;
    build_fake_chunk(200, 0x22, chunk2);
    buf.buffer_frame(chunk2.data(), chunk2.size());
    buf.flush();
    TEST_ASSERT_EQUAL_INT(2, sd_hal.write_call_count);

    // Verify second write data
    TEST_ASSERT_EQUAL(chunk2.size(), sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk2.data(), sd_hal.last_write_data, chunk2.size());
}

// ============================================================
// reset()
// ============================================================

/**
 * @brief reset() clears buffer state and discards data.
 */
void test_reset_clears_buffer()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    std::vector<uint8_t> chunk;
    build_fake_chunk(100, 0xDD, chunk);
    buf.buffer_frame(chunk.data(), chunk.size());
    TEST_ASSERT_EQUAL(chunk.size(), buf.buffered_bytes());

    buf.reset();
    TEST_ASSERT_EQUAL_UINT32(0, buf.buffered_bytes());
    TEST_ASSERT_FALSE(buf.is_open());
}

/**
 * @brief After reset(), buffer_frame() returns false (not open).
 */
void test_reset_prevents_further_buffering()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");
    buf.reset();

    std::vector<uint8_t> chunk;
    build_fake_chunk(100, 0xEE, chunk);
    TEST_ASSERT_FALSE(buf.buffer_frame(chunk.data(), chunk.size()));
}

// ============================================================
// Data integrity
// ============================================================

/**
 * @brief Buffered data matches the original chunk exactly.
 */
void test_data_integrity_small_chunk()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Build a chunk with a known pattern
    std::vector<uint8_t> chunk;
    build_fake_chunk(50, 0xAB, chunk);

    buf.buffer_frame(chunk.data(), chunk.size());
    buf.flush();

    // Verify every byte written to SD matches
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk.data(), sd_hal.last_write_data, chunk.size());
}

/**
 * @brief Two sequential flushes produce two correct writes.
 */
void test_data_integrity_two_flushes()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    std::vector<uint8_t> chunk1, chunk2;
    build_fake_chunk(100, 0xAA, chunk1);
    build_fake_chunk(200, 0xBB, chunk2);

    // First flush
    buf.buffer_frame(chunk1.data(), chunk1.size());
    buf.flush();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk1.data(), sd_hal.last_write_data, chunk1.size());

    // Second flush
    buf.buffer_frame(chunk2.data(), chunk2.size());
    buf.flush();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk2.data(), sd_hal.last_write_data, chunk2.size());
}

// ============================================================
// Edge cases
// ============================================================

/**
 * @brief Single-byte chunk is buffered and flushed correctly.
 */
void test_single_byte_chunk()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Smallest possible chunk: 8-byte header + 1 byte payload → padded to 12
    std::vector<uint8_t> chunk;
    build_fake_chunk(1, 0xFF, chunk);
    TEST_ASSERT_EQUAL_UINT32(12, chunk.size());

    buf.buffer_frame(chunk.data(), chunk.size());
    TEST_ASSERT_EQUAL_UINT32(12, buf.buffered_bytes());

    buf.flush();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk.data(), sd_hal.last_write_data, chunk.size());
}

/**
 * @brief Chunk that exactly fills the remaining buffer space.
 */
void test_chunk_fills_exact_remaining_space()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // First chunk: fill most of the buffer
    std::vector<uint8_t> chunk1;
    build_fake_chunk(4000, 0xAA, chunk1);
    buf.buffer_frame(chunk1.data(), chunk1.size());
    size_t firstSize = chunk1.size();

    // Second chunk: exactly fills the remaining RAMSIZE
    size_t remaining = SdWriteBuffer::RAMSIZE - firstSize;
    std::vector<uint8_t> chunk2;
    build_fake_chunk(remaining - 8, 0xBB, chunk2);  // -8 for chunk header
    buf.buffer_frame(chunk2.data(), chunk2.size());

    // Should have triggered auto-flush with the full buffer contents
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL(SdWriteBuffer::RAMSIZE, sd_hal.last_write_len);
}

/**
 * @brief Flush after auto-flush writes the remaining data correctly.
 */
void test_flush_after_auto_flush()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Fill past RAMSIZE to trigger auto-flush
    std::vector<uint8_t> chunk1;
    build_fake_chunk(SdWriteBuffer::RAMSIZE - 8, 0xAA, chunk1);
    buf.buffer_frame(chunk1.data(), chunk1.size());
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);  // auto-flushed

    // Buffer one more chunk
    std::vector<uint8_t> chunk2;
    build_fake_chunk(100, 0xBB, chunk2);
    buf.buffer_frame(chunk2.data(), chunk2.size());

    // Manual flush writes the remaining
    buf.flush();
    TEST_ASSERT_EQUAL_INT(2, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL(chunk2.size(), sd_hal.last_write_len);
}

/**
 * @brief SD write failure returns false from flush.
 */
void test_flush_sd_write_failure()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    std::vector<uint8_t> chunk;
    build_fake_chunk(100, 0xCC, chunk);
    buf.buffer_frame(chunk.data(), chunk.size());

    // Configure HAL to fail on write
    sd_hal.open_result = false;

    TEST_ASSERT_FALSE(buf.flush());
}

/**
 * @brief SD write failure on auto-flush returns false from buffer_frame.
 */
void test_auto_flush_sd_write_failure()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Fill to just below RAMSIZE
    std::vector<uint8_t> chunk1;
    build_fake_chunk(SdWriteBuffer::RAMSIZE - 100 - 8, 0xAA, chunk1);
    buf.buffer_frame(chunk1.data(), chunk1.size());

    // Configure HAL to fail
    sd_hal.open_result = false;

    // Next chunk triggers auto-flush which fails
    std::vector<uint8_t> chunk2;
    build_fake_chunk(200, 0xBB, chunk2);
    TEST_ASSERT_FALSE(buf.buffer_frame(chunk2.data(), chunk2.size()));
}

// ============================================================
// Multiple auto-flush cycles
// ============================================================

/**
 * @brief Filling the buffer multiple times produces correct sequential writes.
 */
void test_multiple_auto_flush_cycles()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController sd_ctrl(time, sd_hal);
    SdWriteBuffer buf(sd_ctrl);
    buf.open("/test.avi");

    // Fill the buffer to trigger auto-flush 3 times
    for (int i = 0; i < 3; i++)
    {
        std::vector<uint8_t> chunk;
        build_fake_chunk(SdWriteBuffer::RAMSIZE - 8, static_cast<uint8_t>(0x10 + i), chunk);
        buf.buffer_frame(chunk.data(), chunk.size());
    }

    // 3 auto-flushes should have occurred
    TEST_ASSERT_EQUAL_INT(3, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, buf.buffered_bytes());
}

// ============================================================
// main
// ============================================================

int main()
{
    UNITY_BEGIN();

    // Default state
    RUN_TEST(test_initial_not_open);
    RUN_TEST(test_initial_buffered_bytes_zero);

    // open()
    RUN_TEST(test_open_success);
    RUN_TEST(test_open_null_path_returns_false);

    // buffer_frame() — basic
    RUN_TEST(test_buffer_frame_before_open_returns_false);
    RUN_TEST(test_buffer_frame_null_data_returns_false);
    RUN_TEST(test_buffer_frame_zero_length_returns_false);
    RUN_TEST(test_buffer_frame_accumulates_in_ram);
    RUN_TEST(test_multiple_frames_accumulate);

    // Auto-flush at RAMSIZE threshold
    RUN_TEST(test_auto_flush_at_ramsize_threshold);
    RUN_TEST(test_auto_flush_at_exact_ramsize);

    // Oversized frames
    RUN_TEST(test_oversized_chunk_written_directly);
    RUN_TEST(test_oversized_chunk_flushes_pending_first);

    // flush()
    RUN_TEST(test_flush_empty_returns_true);
    RUN_TEST(test_flush_before_open_returns_false);
    RUN_TEST(test_flush_writes_to_sd);
    RUN_TEST(test_flush_after_partial_fill);

    // Multiple flush cycles
    RUN_TEST(test_multiple_flush_cycles);

    // reset()
    RUN_TEST(test_reset_clears_buffer);
    RUN_TEST(test_reset_prevents_further_buffering);

    // Data integrity
    RUN_TEST(test_data_integrity_small_chunk);
    RUN_TEST(test_data_integrity_two_flushes);

    // Edge cases
    RUN_TEST(test_single_byte_chunk);
    RUN_TEST(test_chunk_fills_exact_remaining_space);
    RUN_TEST(test_flush_after_auto_flush);
    RUN_TEST(test_flush_sd_write_failure);
    RUN_TEST(test_auto_flush_sd_write_failure);

    // Multiple auto-flush cycles
    RUN_TEST(test_multiple_auto_flush_cycles);

    UNITY_END();
    return 0;
}
