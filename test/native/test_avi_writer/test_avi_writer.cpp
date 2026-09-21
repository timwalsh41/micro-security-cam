#include <unity.h>
#include "avi_writer.h"
#include <cstring>

// Constants matching AviWriter private members for test assertions
static constexpr uint32_t TEST_AVI_HEADER_SIZE = 224;
static constexpr uint8_t TEST_CHUNK_HEADER_SIZE = 8;
static constexpr uint32_t TEST_INDEX_ENTRY_SIZE = 16;
static constexpr uint32_t TEST_MOVI_DATA_OFFSET = 4;

// ============================================================
// Helper: verify a FourCC at a given offset in a buffer
// ============================================================
static void assert_four_cc(const AviBytes& buf, size_t offset, const char expected[4])
{
    TEST_ASSERT_EQUAL_UINT8(expected[0], buf.data[offset]);
    TEST_ASSERT_EQUAL_UINT8(expected[1], buf.data[offset + 1]);
    TEST_ASSERT_EQUAL_UINT8(expected[2], buf.data[offset + 2]);
    TEST_ASSERT_EQUAL_UINT8(expected[3], buf.data[offset + 3]);
}

// Helper: read a uint32 from a byte buffer at a given offset
static uint32_t read_u32(const uint8_t* data, size_t offset)
{
    uint32_t val;
    memcpy(&val, &data[offset], sizeof(uint32_t));
    return val;
}

// ============================================================
// Default state
// ============================================================

/**
 * @brief Before open(), is_open() returns false.
 */
void test_initial_not_open()
{
    AviWriter writer;
    TEST_ASSERT_FALSE(writer.is_open());
}

/**
 * @brief Before open(), frame_count() returns 0.
 */
void test_initial_frame_count_zero()
{
    AviWriter writer;
    TEST_ASSERT_EQUAL_UINT32(0, writer.frame_count());
}

/**
 * @brief Before open(), file_size() returns 0.
 */
void test_initial_file_size_zero()
{
    AviWriter writer;
    TEST_ASSERT_EQUAL_UINT32(0, writer.file_size());
}

/**
 * @brief Before open(), path() returns nullptr.
 */
void test_initial_path_null()
{
    AviWriter writer;
    TEST_ASSERT_NULL(writer.path());
}

// ============================================================
// open() — success / failure
// ============================================================

/**
 * @brief open() returns true and sets state.
 */
void test_open_success()
{
    AviWriter writer;
    TEST_ASSERT_TRUE(writer.open("/test.avi", 10, 640, 480));
    TEST_ASSERT_TRUE(writer.is_open());
    TEST_ASSERT_EQUAL_UINT32(0, writer.frame_count());
}

/**
 * @brief open() stores the path.
 */
void test_open_stores_path()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    TEST_ASSERT_EQUAL_STRING("/test.avi", writer.path());
}

/**
 * @brief open() when already open returns false.
 */
void test_open_already_open_returns_false()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    TEST_ASSERT_FALSE(writer.open("/test2.avi", 10, 640, 480));
}

// ============================================================
// Header structure
// ============================================================

/**
 * @brief Header has correct size.
 */
void test_header_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(224, hdr.len);
}

/**
 * @brief Header starts with RIFF marker.
 */
void test_header_riff_marker()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    assert_four_cc(hdr, 0, "RIFF");
}

/**
 * @brief Header has AVI identifier.
 */
void test_header_avi_marker()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    assert_four_cc(hdr, 8, "AVI ");
}

/**
 * @brief Header has correct dimensions in avih.
 */
void test_header_dimensions()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(640, read_u32(hdr.data, 64));
    TEST_ASSERT_EQUAL_UINT32(480, read_u32(hdr.data, 68));
}

/**
 * @brief Header has correct FPS in avih dwMicroSecPerFrame.
 */
void test_header_fps()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(100000, read_u32(hdr.data, 32));  // 1000000 / 10
}

/**
 * @brief Header has MJPG codec in strh.
 */
void test_header_mjpg_codec()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    assert_four_cc(hdr, 108, "vids");
    assert_four_cc(hdr, 112, "MJPG");
}

/**
 * @brief Header has correct FPS in strh dwRate.
 */
void test_header_strh_rate()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(10, read_u32(hdr.data, 132));
}

/**
 * @brief Header has correct dimensions in strf BITMAPINFOHEADER.
 */
void test_header_strf_dimensions()
{
    AviWriter writer;
    writer.open("/test.avi", 15, 320, 240);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(320, read_u32(hdr.data, 176));
    TEST_ASSERT_EQUAL_UINT32(240, read_u32(hdr.data, 180));
}

/**
 * @brief Header has MJPG in strf biCompression.
 */
void test_header_strf_mjpg()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    assert_four_cc(hdr, 188, "MJPG");
}

/**
 * @brief Header initial frame count is 0.
 */
void test_header_initial_frame_count_zero()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(0, read_u32(hdr.data, 48));
}

/**
 * @brief Header has LIST hdrl.
 */
void test_header_hdrl_list()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    assert_four_cc(hdr, 12, "LIST");
    assert_four_cc(hdr, 20, "hdrl");
}

/**
 * @brief Header has LIST movi.
 */
void test_header_movi_list()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    assert_four_cc(hdr, 212, "LIST");
    assert_four_cc(hdr, 220, "movi");
}

/**
 * @brief Header has avih with correct data size (56).
 */
void test_header_avih_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(56, read_u32(hdr.data, 28));
}

/**
 * @brief Header has strh with correct data size (56).
 */
void test_header_strh_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(56, read_u32(hdr.data, 104));
}

/**
 * @brief Header has strf with correct data size (40).
 */
void test_header_strf_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(40, read_u32(hdr.data, 168));
}

/**
 * @brief Header has dwQuality = 0xFFFFFFFF in strh.
 */
void test_header_strh_quality()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, read_u32(hdr.data, 148));
}

/**
 * @brief Header has dwSampleSize = 0 in strh.
 */
void test_header_strh_sample_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(0, read_u32(hdr.data, 152));
}

/**
 * @brief Header has correct avih dwFlags.
 *
 * AVIF_HASINDEX = 0x10, AVIF_WASCAPKEYFRAME = 0x100,
 * AVIF_TRUSTCKTYPE = 0x800 → combined = 0x910.
 */
void test_header_avih_flags()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(0x910, read_u32(hdr.data, 44));
}

// ============================================================
// write_frame() — basic
// ============================================================

/**
 * @brief write_frame() before open() returns false.
 */
void test_write_frame_before_open_returns_false()
{
    AviWriter writer;
    uint8_t jpeg[10] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(writer.write_frame(jpeg, 10));
}

/**
 * @brief write_frame() with null data returns false.
 */
void test_write_frame_null_data_returns_false()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    TEST_ASSERT_FALSE(writer.write_frame(nullptr, 10));
}

/**
 * @brief write_frame() with zero length returns false.
 */
void test_write_frame_zero_length_returns_false()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[10] = {};
    TEST_ASSERT_FALSE(writer.write_frame(jpeg, 0));
}

/**
 * @brief write_frame() returns true on success.
 */
void test_write_frame_success()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[100];
    memset(jpeg, 0xAB, sizeof(jpeg));
    TEST_ASSERT_TRUE(writer.write_frame(jpeg, sizeof(jpeg)));
}

/**
 * @brief write_frame() increments frame count.
 */
void test_write_frame_increments_count()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[50];
    memset(jpeg, 0, sizeof(jpeg));

    writer.write_frame(jpeg, sizeof(jpeg));
    TEST_ASSERT_EQUAL_UINT32(1, writer.frame_count());

    writer.write_frame(jpeg, sizeof(jpeg));
    TEST_ASSERT_EQUAL_UINT32(2, writer.frame_count());
}

// ============================================================
// write_frame() — chunk format
// ============================================================

/**
 * @brief Frame chunk starts with "00dc" FourCC marker.
 */
void test_frame_chunk_marker()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[100];
    memset(jpeg, 0xAB, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));

    AviBytes chunk = writer.last_frame_chunk();
    assert_four_cc(chunk, 0, "00dc");
}

/**
 * @brief Frame chunk has correct data size field.
 */
void test_frame_chunk_data_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[100];
    memset(jpeg, 0xAB, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));

    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_EQUAL_UINT32(100, read_u32(chunk.data, 4));
}

/**
 * @brief Frame chunk contains correct JPEG data.
 */
void test_frame_chunk_data_content()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[50];
    for (uint8_t i = 0; i < 50; i++)
    {
        jpeg[i] = i;
    }
    writer.write_frame(jpeg, sizeof(jpeg));

    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(jpeg, &chunk.data[8], 50);
}

// ============================================================
// write_frame() — DWORD alignment
// ============================================================

/**
 * @brief Frame with size already DWORD-aligned has no padding.
 * 8 (header) + 96 (data) = 104 bytes, 104 % 4 == 0
 */
void test_dword_align_no_padding()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[96];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));

    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_EQUAL_UINT32(104, chunk.len);
}

/**
 * @brief Frame needing 1 byte of padding.
 * 8 (header) + 97 (data) = 105 → padded to 108
 */
void test_dword_align_one_byte_padding()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[97];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));

    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_EQUAL_UINT32(108, chunk.len);
    // Verify padding byte is zero
    TEST_ASSERT_EQUAL_UINT8(0, chunk.data[105]);
    TEST_ASSERT_EQUAL_UINT8(0, chunk.data[106]);
    TEST_ASSERT_EQUAL_UINT8(0, chunk.data[107]);
}

/**
 * @brief Frame needing 2 bytes of padding.
 * 8 (header) + 98 (data) = 106 → padded to 108
 */
void test_dword_align_two_bytes_padding()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[98];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));

    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_EQUAL_UINT32(108, chunk.len);
    TEST_ASSERT_EQUAL_UINT8(0, chunk.data[106]);
    TEST_ASSERT_EQUAL_UINT8(0, chunk.data[107]);
}

/**
 * @brief Frame needing 3 bytes of padding.
 * 8 (header) + 99 (data) = 107 → padded to 108
 */
void test_dword_align_three_bytes_padding()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[99];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));

    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_EQUAL_UINT32(108, chunk.len);
    TEST_ASSERT_EQUAL_UINT8(0, chunk.data[107]);
}

/**
 * @brief Small frame: 1 byte JPEG → 8+1=9 → padded to 12.
 */
void test_dword_align_small_frame()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg = 0xFF;
    writer.write_frame(&jpeg, 1);

    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_EQUAL_UINT32(12, chunk.len);
}

// ============================================================
// Multiple frames
// ============================================================

/**
 * @brief Multiple frames accumulate correct data size.
 */
void test_multiple_frames_data_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    // Frame 1: 100 bytes → chunk = 108 (DWORD aligned: 8+100=108)
    uint8_t jpeg1[100];
    memset(jpeg1, 0, sizeof(jpeg1));
    writer.write_frame(jpeg1, sizeof(jpeg1));

    // Frame 2: 50 bytes → chunk = 60 (DWORD aligned: 8+50=58 → padded to 60)
    uint8_t jpeg2[50];
    memset(jpeg2, 0, sizeof(jpeg2));
    writer.write_frame(jpeg2, sizeof(jpeg2));

    // file_size = header(224) + frame1(108) + frame2(60) + idx1_header(8) + index(32)
    TEST_ASSERT_EQUAL_UINT32(224 + 108 + 60 + 8 + 32, writer.file_size());
}

/**
 * @brief Multiple frames produce correct index entries.
 */
void test_multiple_frames_index_entries()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    uint8_t jpeg1[100];
    memset(jpeg1, 0, sizeof(jpeg1));
    writer.write_frame(jpeg1, sizeof(jpeg1));

    uint8_t jpeg2[50];
    memset(jpeg2, 0, sizeof(jpeg2));
    writer.write_frame(jpeg2, sizeof(jpeg2));

    writer.close();

    AviBytes idx = writer.index();
    // idx1 header (8) + 2 entries (32) = 40 bytes
    TEST_ASSERT_EQUAL_UINT32(40, idx.len);

    // Index entry layout (16 bytes each after idx1 header):
    //   ckid(4) + flags(4) + offset(4) + size(4)
    // First entry at idx.data + 8:
    TEST_ASSERT_EQUAL_UINT32(4, read_u32(idx.data, 16));    // entry[0].offset (from movi data)
    TEST_ASSERT_EQUAL_UINT32(100, read_u32(idx.data, 20));  // entry[0].size

    // Second entry at idx.data + 24:
    TEST_ASSERT_EQUAL_UINT32(112, read_u32(idx.data, 32));  // entry[1].offset (4 + 108)
    TEST_ASSERT_EQUAL_UINT32(50, read_u32(idx.data, 36));   // entry[1].size
}

// ============================================================
// close() — header patching
// ============================================================

/**
 * @brief close() patches RIFF size in header.
 */
void test_close_patches_riFF_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes hdr = writer.patched_header();
    uint32_t expected = writer.file_size() - 8;
    TEST_ASSERT_EQUAL_UINT32(expected, read_u32(hdr.data, 4));
}

/**
 * @brief close() patches frame count in avih.
 */
void test_close_patches_avih_frame_count()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes hdr = writer.patched_header();
    TEST_ASSERT_EQUAL_UINT32(3, read_u32(hdr.data, 48));
}

/**
 * @brief close() patches frame count in strh.
 */
void test_close_patches_strh_frame_count()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes hdr = writer.patched_header();
    TEST_ASSERT_EQUAL_UINT32(1, read_u32(hdr.data, 140));
}

/**
 * @brief close() patches movi list size.
 */
void test_close_patches_movi_list_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    // Frame: 100 bytes JPEG → chunk = 108 bytes (DWORD aligned)
    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes hdr = writer.patched_header();
    // movi list size = 4 ("movi") + 108 (frame data) = 112
    TEST_ASSERT_EQUAL_UINT32(112, read_u32(hdr.data, 216));
}

/**
 * @brief close() builds index data.
 */
void test_close_builds_index()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes idx = writer.index();
    TEST_ASSERT_NOT_NULL(idx.data);
    TEST_ASSERT_GREATER_THAN(0u, idx.len);
}

/**
 * @brief close() sets is_open() to false.
 */
void test_close_sets_not_open()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[10] = {};
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();
    TEST_ASSERT_FALSE(writer.is_open());
}

/**
 * @brief close() when not open returns false.
 */
void test_close_when_not_open_returns_false()
{
    AviWriter writer;
    TEST_ASSERT_FALSE(writer.close());
}

// ============================================================
// Index format
// ============================================================

/**
 * @brief Index starts with idx1 marker.
 */
void test_index_has_idx1_marker()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[50];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes idx = writer.index();
    assert_four_cc(idx, 0, "idx1");
}

/**
 * @brief Index has correct size field.
 */
void test_index_has_correct_size_field()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[50];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes idx = writer.index();
    TEST_ASSERT_EQUAL_UINT32(32, read_u32(idx.data, 4));  // 2 entries × 16 bytes
}

/**
 * @brief Index entries have correct "00dc" FourCC chunk ID.
 */
void test_index_entries_chunk_id()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[50];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes idx = writer.index();
    // First entry starts at offset 8 (after idx1 header)
    assert_four_cc(idx, 8, "00dc");
}

/**
 * @brief Index entries have AVIIF_KEYFRAME flag set.
 */
void test_index_entries_keyframe_flag()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[50];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.close();

    AviBytes idx = writer.index();
    // AVIIF_KEYFRAME = 0x10
    TEST_ASSERT_EQUAL_UINT32(0x10, read_u32(idx.data, 12));
}

// ============================================================
// Edge cases
// ============================================================

/**
 * @brief Single frame recording produces valid output.
 */
void test_single_frame()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    uint8_t jpeg[200];
    memset(jpeg, 0xFF, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));

    TEST_ASSERT_EQUAL_UINT32(1, writer.frame_count());

    writer.close();

    // Verify header frame count patched
    AviBytes hdr = writer.patched_header();
    TEST_ASSERT_EQUAL_UINT32(1, read_u32(hdr.data, 48));
    TEST_ASSERT_EQUAL_UINT32(1, read_u32(hdr.data, 140));

    // Verify index has exactly one entry
    AviBytes idx = writer.index();
    TEST_ASSERT_EQUAL_UINT32(16, read_u32(idx.data, 4));  // 1 entry × 16 bytes
}

/**
 * @brief Close without any frames produces valid but empty AVI.
 */
void test_close_no_frames()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.close();

    TEST_ASSERT_EQUAL_UINT32(0, writer.frame_count());

    AviBytes hdr = writer.patched_header();
    TEST_ASSERT_EQUAL_UINT32(0, read_u32(hdr.data, 48));

    // Index should have idx1 header with size 0
    AviBytes idx = writer.index();
    TEST_ASSERT_EQUAL_UINT32(0, read_u32(idx.data, 4));
}

/**
 * @brief Header accessor returns null before open.
 */
void test_header_null_before_open()
{
    AviWriter writer;
    AviBytes hdr = writer.header();
    TEST_ASSERT_NULL(hdr.data);
    TEST_ASSERT_EQUAL_UINT32(0, hdr.len);
}

/**
 * @brief last_frame_chunk returns null before any write_frame.
 */
void test_last_frame_chunk_null_before_write()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes chunk = writer.last_frame_chunk();
    TEST_ASSERT_NULL(chunk.data);
}

/**
 * @brief Index returns null before close.
 */
void test_index_null_before_close()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    AviBytes idx = writer.index();
    TEST_ASSERT_NULL(idx.data);
}

/**
 * @brief Different FPS values produce correct header.
 */
void test_different_fps()
{
    AviWriter writer;
    writer.open("/test.avi", 15, 320, 240);

    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(1000000u / 15, read_u32(hdr.data, 32));  // ~66666
    TEST_ASSERT_EQUAL_UINT32(15, read_u32(hdr.data, 132));
}

/**
 * @brief Different dimensions produce correct header.
 */
void test_different_dimensions()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 1280, 720);

    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(1280, read_u32(hdr.data, 64));
    TEST_ASSERT_EQUAL_UINT32(720, read_u32(hdr.data, 68));
    TEST_ASSERT_EQUAL_UINT32(1280, read_u32(hdr.data, 176));
    TEST_ASSERT_EQUAL_UINT32(720, read_u32(hdr.data, 180));
}

/**
 * @brief file_size accounts for all components.
 */
void test_file_size_formula()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    // Two frames: 100 bytes and 50 bytes
    uint8_t jpeg1[100];
    uint8_t jpeg2[50];
    memset(jpeg1, 0, sizeof(jpeg1));
    memset(jpeg2, 0, sizeof(jpeg2));
    writer.write_frame(jpeg1, sizeof(jpeg1));
    writer.write_frame(jpeg2, sizeof(jpeg2));

    // header(224) + chunk1(108) + chunk2(60) + idx1_header(8) + 2_entries(32)
    // chunk2: 8+50=58 → padded to 60 for DWORD alignment
    uint32_t expected = 224 + 108 + 60 + 8 + 32;
    TEST_ASSERT_EQUAL_UINT32(expected, writer.file_size());
}

/**
 * @brief Large recording with 1000 frames produces valid index.
 */
void test_many_frames()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    uint8_t jpeg[200];
    memset(jpeg, 0xAB, sizeof(jpeg));

    const uint32_t NUM_FRAMES = 1000;
    for (uint32_t i = 0; i < NUM_FRAMES; i++)
    {
        writer.write_frame(jpeg, sizeof(jpeg));
    }

    TEST_ASSERT_EQUAL_UINT32(NUM_FRAMES, writer.frame_count());
    writer.close();

    // Verify frame count patched in header
    AviBytes hdr = writer.patched_header();
    TEST_ASSERT_EQUAL_UINT32(NUM_FRAMES, read_u32(hdr.data, 48));
    TEST_ASSERT_EQUAL_UINT32(NUM_FRAMES, read_u32(hdr.data, 140));

    // Verify index has correct number of entries
    AviBytes idx = writer.index();
    TEST_ASSERT_EQUAL_UINT32(NUM_FRAMES * 16, read_u32(idx.data, 4));

    // Verify last index entry offset is correct
    // Each chunk = 208 bytes (8 header + 200 data) padded to 208
    // Offset = 4 (movi initial offset) + 207 * 208
    uint32_t expectedLastOffset = TEST_MOVI_DATA_OFFSET + (NUM_FRAMES - 1) * 208;
    uint32_t lastEntryOffset = TEST_CHUNK_HEADER_SIZE + (NUM_FRAMES - 1) * TEST_INDEX_ENTRY_SIZE;
    TEST_ASSERT_EQUAL_UINT32(expectedLastOffset, read_u32(idx.data, lastEntryOffset + 8));
}

/**
 * @brief Multiple open/write/close cycles produce correct state each time.
 */
void test_multiple_open_close_cycles()
{
    AviWriter writer;

    // --- Cycle 1 ---
    TEST_ASSERT_TRUE(writer.open("/test1.avi", 10, 640, 480));

    uint8_t jpeg1[100];
    memset(jpeg1, 0xA1, sizeof(jpeg1));
    writer.write_frame(jpeg1, sizeof(jpeg1));
    writer.write_frame(jpeg1, sizeof(jpeg1));

    TEST_ASSERT_EQUAL_UINT32(2, writer.frame_count());
    TEST_ASSERT_TRUE(writer.is_open());
    TEST_ASSERT_EQUAL_STRING("/test1.avi", writer.path());
    writer.close();
    TEST_ASSERT_FALSE(writer.is_open());

    // --- Cycle 2 ---
    TEST_ASSERT_TRUE(writer.open("/test2.avi", 15, 320, 240));

    TEST_ASSERT_EQUAL_UINT32(0, writer.frame_count());  // Counter reset
    TEST_ASSERT_TRUE(writer.is_open());
    TEST_ASSERT_EQUAL_STRING("/test2.avi", writer.path());
    TEST_ASSERT_EQUAL_UINT32(15, read_u32(writer.header().data, 132));  // New FPS

    uint8_t jpeg2[50];
    memset(jpeg2, 0xA2, sizeof(jpeg2));
    writer.write_frame(jpeg2, sizeof(jpeg2));

    TEST_ASSERT_EQUAL_UINT32(1, writer.frame_count());
    writer.close();
    TEST_ASSERT_FALSE(writer.is_open());

    // --- Cycle 3 (no frames written) ---
    TEST_ASSERT_TRUE(writer.open("/test3.avi", 30, 1280, 720));
    TEST_ASSERT_EQUAL_UINT32(0, writer.frame_count());

    AviBytes hdr3 = writer.header();
    TEST_ASSERT_EQUAL_UINT32(1280, read_u32(hdr3.data, 64));
    TEST_ASSERT_EQUAL_UINT32(720, read_u32(hdr3.data, 68));

    writer.close();
    TEST_ASSERT_FALSE(writer.is_open());
}

/**
 * @brief Frame data integrity is preserved across many frames.
 */
void test_many_frames_data_integrity()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);

    // Write 500 frames where each has a unique byte pattern
    const uint32_t NUM_FRAMES = 500;
    const size_t JPEG_SIZE = 150;
    uint8_t jpeg[JPEG_SIZE];

    for (uint32_t i = 0; i < NUM_FRAMES; i++)
    {
        memset(jpeg, (uint8_t)(i & 0xFF), JPEG_SIZE);
        writer.write_frame(jpeg, JPEG_SIZE);
    }

    TEST_ASSERT_EQUAL_UINT32(NUM_FRAMES, writer.frame_count());

    // Verify frame count patched in header
    writer.close();
    AviBytes hdr = writer.patched_header();
    TEST_ASSERT_EQUAL_UINT32(NUM_FRAMES, read_u32(hdr.data, 48));

    // Verify that the last frame chunk before close contains correct data
    AviBytes chunk = writer.last_frame_chunk();
    // Last frame had pattern (NUM_FRAMES - 1) & 0xFF
    uint8_t expectedByte = (uint8_t)((NUM_FRAMES - 1) & 0xFF);

    // Data starts at offset 8 (after chunk header)
    TEST_ASSERT_EQUAL_UINT8(expectedByte, chunk.data[8]);
    // Verify all bytes in the frame
    for (size_t j = 0; j < JPEG_SIZE; j++)
    {
        TEST_ASSERT_EQUAL_UINT8(expectedByte, chunk.data[8 + j]);
    }
}

/**
 * @brief File size after close with no frames is header + empty idx1.
 */
void test_file_size_after_close_no_frames()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.close();

    // With 0 frames: header(224) + idx1_header(8) + 0 index entries = 232
    TEST_ASSERT_EQUAL_UINT32(TEST_AVI_HEADER_SIZE + TEST_CHUNK_HEADER_SIZE, writer.file_size());

    AviBytes hdr = writer.patched_header();
    TEST_ASSERT_EQUAL_UINT32(TEST_AVI_HEADER_SIZE, hdr.len);

    AviBytes idx = writer.index();
    // idx1 chunk: header(8) + index data(0) = 8 bytes
    TEST_ASSERT_EQUAL_UINT32(8, idx.len);
    TEST_ASSERT_EQUAL_UINT32(0, read_u32(idx.data, 4));  // 0 entries
}

// ============================================================
// Audio stream tests
// ============================================================

/**
 * @brief open_audio() enables audio and changes header layout.
 */
void test_open_audio_enables_audio()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    TEST_ASSERT_TRUE(writer.open_audio(16000, 16, 1));
    // Header should now be 336 bytes for dual-stream
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(336, hdr.len);
}

/**
 * @brief open_audio() returns false before open() is called.
 */
void test_open_audio_returns_false_before_open()
{
    AviWriter writer;
    TEST_ASSERT_FALSE(writer.open_audio(16000, 16, 1));
}

/**
 * @brief open_audio() returns false after frames have been written.
 */
void test_open_audio_returns_false_after_frames()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    uint8_t jpeg[10];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    TEST_ASSERT_FALSE(writer.open_audio(16000, 16, 1));
}

/**
 * @brief Dual-stream header is 336 bytes.
 */
void test_audio_header_size_dual_stream()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    AviBytes hdr = writer.header();
    TEST_ASSERT_EQUAL_UINT32(336, hdr.len);
}

/**
 * @brief Dual-stream header has audio strl at the expected offset.
 */
void test_audio_header_has_audio_strl()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    AviBytes hdr = writer.header();
    // Audio strl LIST at offset 212
    assert_four_cc(hdr, 212, "LIST");
    // Audio strl "strl" at offset 220
    assert_four_cc(hdr, 220, "strl");
    // Audio strh "auds" at offset 232
    assert_four_cc(hdr, 232, "auds");
}

/**
 * @brief Dual-stream header has dwStreams = 2.
 */
void test_audio_header_has_two_streams()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    AviBytes hdr = writer.header();
    // avih dwStreams at offset 56
    TEST_ASSERT_EQUAL_UINT32(2, read_u32(hdr.data, 56));
}

/**
 * @brief write_audio_chunk() returns true on success.
 */
void test_write_audio_chunk_success()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    int16_t audio[100];
    memset(audio, 0x42, sizeof(audio));
    TEST_ASSERT_TRUE(writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio)));
}

/**
 * @brief Audio chunk starts with "01wb" FourCC marker.
 */
void test_write_audio_chunk_marker()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    int16_t audio[50];
    memset(audio, 0x42, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    AviBytes chunk = writer.last_audio_chunk();
    assert_four_cc(chunk, 0, "01wb");
}

/**
 * @brief Audio chunk has correct data size field.
 */
void test_write_audio_chunk_data_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    int16_t audio[50];
    memset(audio, 0x42, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    AviBytes chunk = writer.last_audio_chunk();
    TEST_ASSERT_EQUAL_UINT32(100, read_u32(chunk.data, 4));  // 50 * 2 bytes
}

/**
 * @brief Audio chunk contains correct PCM data.
 */
void test_write_audio_chunk_data_content()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    int16_t audio[25];
    for (int i = 0; i < 25; i++) audio[i] = i;
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    AviBytes chunk = writer.last_audio_chunk();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        reinterpret_cast<const uint8_t*>(audio),
        &chunk.data[8], sizeof(audio));
}

/**
 * @brief Audio chunk is DWORD-aligned.
 */
void test_write_audio_chunk_dword_alignment()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    // 25 samples * 2 bytes = 50 bytes -> 8+50=58 -> padded to 60
    int16_t audio[25];
    memset(audio, 0, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    AviBytes chunk = writer.last_audio_chunk();
    TEST_ASSERT_EQUAL_UINT32(60, chunk.len);
}

/**
 * @brief write_audio_chunk() returns false when audio is not enabled.
 */
void test_write_audio_chunk_returns_false_without_audio()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    int16_t audio[10];
    TEST_ASSERT_FALSE(writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio)));
}

/**
 * @brief write_audio_chunk() returns false before open().
 */
void test_write_audio_chunk_returns_false_before_open()
{
    AviWriter writer;
    int16_t audio[10];
    TEST_ASSERT_FALSE(writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio)));
}

/**
 * @brief last_audio_chunk() returns null before any write_audio_chunk().
 */
void test_last_audio_chunk_null_before_write()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    AviBytes chunk = writer.last_audio_chunk();
    TEST_ASSERT_NULL(chunk.data);
}

/**
 * @brief last_audio_chunk() returns valid data after write_audio_chunk().
 */
void test_last_audio_chunk_valid_after_write()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    int16_t audio[10];
    memset(audio, 0x42, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    AviBytes chunk = writer.last_audio_chunk();
    TEST_ASSERT_NOT_NULL(chunk.data);
    TEST_ASSERT_GREATER_THAN(0u, chunk.len);
}

/**
 * @brief Dual-stream file size accounts for both video and audio.
 */
void test_dual_stream_file_size()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));  // video frame
    int16_t audio[50];
    memset(audio, 0, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));  // audio chunk
    // header(336) + video_chunk(108) + audio_chunk(108) + idx1(8) + 2_entries(32)
    // audio: 8 header + 100 data = 108 bytes (already DWORD aligned)
    TEST_ASSERT_EQUAL_UINT32(336 + 108 + 108 + 8 + 32, writer.file_size());
}

/**
 * @brief Dual-stream close patches both video and audio frame counts.
 */
void test_dual_stream_close_patches_header()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    int16_t audio[100];
    memset(audio, 0, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    writer.close();
    AviBytes hdr = writer.patched_header();
    // Video frame count at offset 48 (avih) and 140 (strh)
    TEST_ASSERT_EQUAL_UINT32(2, read_u32(hdr.data, 48));
    TEST_ASSERT_EQUAL_UINT32(2, read_u32(hdr.data, 140));
    // Audio sample count at offset 264 (audio strh dwLength)
    // 200 bytes / 2 bytes per sample = 100 samples
    TEST_ASSERT_EQUAL_UINT32(100, read_u32(hdr.data, 264));
}

/**
 * @brief Index has both "00dc" and "01wb" entries.
 */
void test_dual_stream_index_has_both_types()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    int16_t audio[50];
    memset(audio, 0, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    writer.close();
    AviBytes idx = writer.index();
    // First entry: "00dc" at offset 8
    assert_four_cc(idx, 8, "00dc");
    // Second entry: "01wb" at offset 24
    assert_four_cc(idx, 24, "01wb");
}

/**
 * @brief Interleaved video and audio chunks have correct index offsets.
 */
void test_dual_stream_interleaved_index()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    uint8_t jpeg[100];
    memset(jpeg, 0, sizeof(jpeg));
    writer.write_frame(jpeg, sizeof(jpeg));
    int16_t audio[50];
    memset(audio, 0, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    writer.close();
    AviBytes idx = writer.index();
    // Video chunk at movi offset 4, size 100
    TEST_ASSERT_EQUAL_UINT32(4, read_u32(idx.data, 16));   // entry[0].offset
    TEST_ASSERT_EQUAL_UINT32(100, read_u32(idx.data, 20));  // entry[0].size
    // Audio chunk at movi offset 4+108=112, size 100
    TEST_ASSERT_EQUAL_UINT32(112, read_u32(idx.data, 32));  // entry[1].offset
    TEST_ASSERT_EQUAL_UINT32(100, read_u32(idx.data, 36));  // entry[1].size
}

/**
 * @brief Audio-only chunks work correctly.
 */
void test_audio_only_chunks()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    int16_t audio[100];
    memset(audio, 0x42, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));
    // No video frames, 2 audio chunks
    TEST_ASSERT_EQUAL_UINT32(0, writer.frame_count());
    writer.close();
    AviBytes idx = writer.index();
    // 2 audio entries
    TEST_ASSERT_EQUAL_UINT32(32, read_u32(idx.data, 4));  // 2 * 16
    assert_four_cc(idx, 8, "01wb");
    assert_four_cc(idx, 24, "01wb");
}

/**
 * @brief Audio chunk updates sample count correctly.
 */
void test_audio_chunk_updates_sample_count()
{
    AviWriter writer;
    writer.open("/test.avi", 10, 640, 480);
    writer.open_audio(16000, 16, 1);
    int16_t audio[100];
    memset(audio, 0, sizeof(audio));
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));  // 100 samples
    writer.write_audio_chunk(
        reinterpret_cast<const uint8_t*>(audio), sizeof(audio));  // 100 more
    writer.close();
    AviBytes hdr = writer.patched_header();
    // Audio sample count at offset 264
    TEST_ASSERT_EQUAL_UINT32(200, read_u32(hdr.data, 264));
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    // Default state
    RUN_TEST(test_initial_not_open);
    RUN_TEST(test_initial_frame_count_zero);
    RUN_TEST(test_initial_file_size_zero);
    RUN_TEST(test_initial_path_null);

    // open()
    RUN_TEST(test_open_success);
    RUN_TEST(test_open_stores_path);
    RUN_TEST(test_open_already_open_returns_false);

    // Header structure
    RUN_TEST(test_header_size);
    RUN_TEST(test_header_riff_marker);
    RUN_TEST(test_header_avi_marker);
    RUN_TEST(test_header_dimensions);
    RUN_TEST(test_header_fps);
    RUN_TEST(test_header_mjpg_codec);
    RUN_TEST(test_header_strh_rate);
    RUN_TEST(test_header_strf_dimensions);
    RUN_TEST(test_header_strf_mjpg);
    RUN_TEST(test_header_initial_frame_count_zero);
    RUN_TEST(test_header_hdrl_list);
    RUN_TEST(test_header_movi_list);
    RUN_TEST(test_header_avih_size);
    RUN_TEST(test_header_strh_size);
    RUN_TEST(test_header_strf_size);
    RUN_TEST(test_header_strh_quality);
    RUN_TEST(test_header_strh_sample_size);
    RUN_TEST(test_header_avih_flags);

    // write_frame() basic
    RUN_TEST(test_write_frame_before_open_returns_false);
    RUN_TEST(test_write_frame_null_data_returns_false);
    RUN_TEST(test_write_frame_zero_length_returns_false);
    RUN_TEST(test_write_frame_success);
    RUN_TEST(test_write_frame_increments_count);

    // write_frame() chunk format
    RUN_TEST(test_frame_chunk_marker);
    RUN_TEST(test_frame_chunk_data_size);
    RUN_TEST(test_frame_chunk_data_content);

    // DWORD alignment
    RUN_TEST(test_dword_align_no_padding);
    RUN_TEST(test_dword_align_one_byte_padding);
    RUN_TEST(test_dword_align_two_bytes_padding);
    RUN_TEST(test_dword_align_three_bytes_padding);
    RUN_TEST(test_dword_align_small_frame);

    // Multiple frames
    RUN_TEST(test_multiple_frames_data_size);
    RUN_TEST(test_multiple_frames_index_entries);

    // close() header patching
    RUN_TEST(test_close_patches_riFF_size);
    RUN_TEST(test_close_patches_avih_frame_count);
    RUN_TEST(test_close_patches_strh_frame_count);
    RUN_TEST(test_close_patches_movi_list_size);
    RUN_TEST(test_close_builds_index);
    RUN_TEST(test_close_sets_not_open);
    RUN_TEST(test_close_when_not_open_returns_false);

    // Index format
    RUN_TEST(test_index_has_idx1_marker);
    RUN_TEST(test_index_has_correct_size_field);
    RUN_TEST(test_index_entries_chunk_id);
    RUN_TEST(test_index_entries_keyframe_flag);

    // Edge cases
    RUN_TEST(test_single_frame);
    RUN_TEST(test_close_no_frames);
    RUN_TEST(test_header_null_before_open);
    RUN_TEST(test_last_frame_chunk_null_before_write);
    RUN_TEST(test_index_null_before_close);
    RUN_TEST(test_different_fps);
    RUN_TEST(test_different_dimensions);
    RUN_TEST(test_file_size_formula);

    // Coverage gaps
    RUN_TEST(test_many_frames);
    RUN_TEST(test_multiple_open_close_cycles);
    RUN_TEST(test_many_frames_data_integrity);
    RUN_TEST(test_file_size_after_close_no_frames);

    // Audio stream tests
    RUN_TEST(test_open_audio_enables_audio);
    RUN_TEST(test_open_audio_returns_false_before_open);
    RUN_TEST(test_open_audio_returns_false_after_frames);
    RUN_TEST(test_audio_header_size_dual_stream);
    RUN_TEST(test_audio_header_has_audio_strl);
    RUN_TEST(test_audio_header_has_two_streams);
    RUN_TEST(test_write_audio_chunk_success);
    RUN_TEST(test_write_audio_chunk_marker);
    RUN_TEST(test_write_audio_chunk_data_size);
    RUN_TEST(test_write_audio_chunk_data_content);
    RUN_TEST(test_write_audio_chunk_dword_alignment);
    RUN_TEST(test_write_audio_chunk_returns_false_without_audio);
    RUN_TEST(test_write_audio_chunk_returns_false_before_open);
    RUN_TEST(test_last_audio_chunk_null_before_write);
    RUN_TEST(test_last_audio_chunk_valid_after_write);
    RUN_TEST(test_dual_stream_file_size);
    RUN_TEST(test_dual_stream_close_patches_header);
    RUN_TEST(test_dual_stream_index_has_both_types);
    RUN_TEST(test_dual_stream_interleaved_index);
    RUN_TEST(test_audio_only_chunks);
    RUN_TEST(test_audio_chunk_updates_sample_count);

    return UNITY_END();
}
