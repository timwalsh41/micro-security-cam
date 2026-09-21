#include <unity.h>
#include "file_service.h"
#include "command_handler.h"
#include "mock_sd_card_hal.h"
#include "mock_web_server_hal.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstring>
#include <string>

/**
 * @file test_file_service.cpp
 * @brief Native unit tests for FileService.
 *
 * Tests cover:
 *  - path validation (validity, listing allowlist, download allowlist)
 *  - LIST_FILES dispatch command (JSON shape, pagination, auth-adjacent
 *    argument handling, active-clip flag)
 *  - /files, /logs routes (auth policy, allowlist, JSON body)
 *  - /download route (auth policy, allowlist, traversal rejection,
 *    active-clip refusal, missing file, data-response body)
 *  - /log route (name validation, tail limiting, empty/missing file)
 */

// ============================================================
// Test fixture
// ============================================================

struct TestFixture
{
    MockSdCardHal sd_hal;
    RealTimeService time;
    MockLogBackend log_backend;
    LoggingService logger;
    CommandHandler cmd_handler;
    SdController sd_controller;
    FileService service;
    MockWebServerHal web;

    TestFixture()
        : logger(log_backend, time)
        , cmd_handler(logger)
        , sd_controller(time, sd_hal)
        , service(sd_controller, cmd_handler, logger)
    {
        // Mirror the composition root: card available at init time.
        sd_hal.init_result = hal::SdInitResult::SUCCESS;
        sd_controller.init_card();

        service.init(web, "");

        // Seed a standard card layout.
        sd_hal.directories.insert("/logs");
        sd_hal.seed_file("/logs/20250101.log", {0x31, 0x32, 0x33});
        sd_hal.directories.insert("/20250101");
        sd_hal.seed_file("/20250101/20250101_120000_640x480_10fps.avi",
                         {0x01, 0x02, 0x03, 0x04, 0x05});
        sd_hal.seed_file("/20250101/20250101_120100_640x480_10fps.jpg",
                         {0xAA, 0xBB});
        sd_hal.seed_file("/config.ini", {0x63, 0x6F, 0x6E, 0x66});
    }

    /**
     * @brief Re-registers routes with a configured password and
     *        authenticates the mock request with it.
     */
    void use_password(const char* password)
    {
        MockWebServerHal fresh_web;
        web = fresh_web;
        service.init(web, password);
        web.mock_request.set_auth("user", password);
    }
};

static uint8_t response_buf[CommandHandler::MAX_RESPONSE_SIZE];

// ============================================================
// is_valid_path
// ============================================================

/**
 * @brief Absolute paths without traversal are valid.
 */
void test_is_valid_path_accepts_absolute()
{
    TEST_ASSERT_TRUE(FileService::is_valid_path("/20250101/file.avi"));
    TEST_ASSERT_TRUE(FileService::is_valid_path("/"));
    TEST_ASSERT_TRUE(FileService::is_valid_path("/logs/20250101.log"));
    TEST_ASSERT_TRUE(FileService::is_valid_path("/20250101"));
}

/**
 * @brief Relative paths are rejected.
 */
void test_is_valid_path_rejects_relative()
{
    TEST_ASSERT_FALSE(FileService::is_valid_path("20250101/file.avi"));
    TEST_ASSERT_FALSE(FileService::is_valid_path(""));
    TEST_ASSERT_FALSE(FileService::is_valid_path(nullptr));
}

/**
 * @brief Traversal segments are rejected.
 */
void test_is_valid_path_rejects_traversal()
{
    TEST_ASSERT_FALSE(FileService::is_valid_path("/20250101/../config.ini"));
    TEST_ASSERT_FALSE(FileService::is_valid_path("/../x"));
    TEST_ASSERT_FALSE(FileService::is_valid_path("/./x"));
    TEST_ASSERT_FALSE(FileService::is_valid_path("/a/../b"));
}

/**
 * @brief Empty segments, trailing slashes, and backslashes are rejected.
 */
void test_is_valid_path_rejects_malformed_segments()
{
    TEST_ASSERT_FALSE(FileService::is_valid_path("/a//b"));
    TEST_ASSERT_FALSE(FileService::is_valid_path("/a/"));
    TEST_ASSERT_FALSE(FileService::is_valid_path("/a\\b"));
}

// ============================================================
// Listing allowlist
// ============================================================

/**
 * @brief Root, /logs, and date directories are listable.
 */
void test_listing_allowlist()
{
    TEST_ASSERT_TRUE(FileService::is_allowed_listing_dir("/"));
    TEST_ASSERT_TRUE(FileService::is_allowed_listing_dir("/logs"));
    TEST_ASSERT_TRUE(FileService::is_allowed_listing_dir("/20250101"));
    TEST_ASSERT_TRUE(FileService::is_allowed_listing_dir("/19991231"));
}

/**
 * @brief Non-date directories, traversal, and non-absolute paths are refused.
 */
void test_listing_allowlist_rejections()
{
    TEST_ASSERT_FALSE(FileService::is_allowed_listing_dir("/etc"));
    TEST_ASSERT_FALSE(FileService::is_allowed_listing_dir("/20250101/sub"));
    TEST_ASSERT_FALSE(FileService::is_allowed_listing_dir("/../"));
    TEST_ASSERT_FALSE(FileService::is_allowed_listing_dir("20250101"));
    TEST_ASSERT_FALSE(FileService::is_allowed_listing_dir(nullptr));
    TEST_ASSERT_FALSE(FileService::is_allowed_listing_dir("/2025010a"));
    TEST_ASSERT_FALSE(FileService::is_allowed_listing_dir("/202501011"));
}

// ============================================================
// Download allowlist
// ============================================================

/**
 * @brief Media files in date directories and log files are downloadable.
 */
void test_download_allowlist()
{
    TEST_ASSERT_TRUE(
        FileService::is_allowed_download_path(
            "/20250101/20250101_120000_640x480_10fps.avi"));
    TEST_ASSERT_TRUE(
        FileService::is_allowed_download_path(
            "/20250101/20250101_120100_640x480_10fps.jpg"));
    TEST_ASSERT_TRUE(
        FileService::is_allowed_download_path("/logs/20250101.log"));
}

/**
 * @brief Root-level files, traversal, and deep paths are refused.
 */
void test_download_allowlist_rejections()
{
    // /config.ini is never downloadable (credentials).
    TEST_ASSERT_FALSE(FileService::is_allowed_download_path("/config.ini"));
    TEST_ASSERT_FALSE(FileService::is_allowed_download_path("/other.txt"));
    TEST_ASSERT_FALSE(
        FileService::is_allowed_download_path("/20250101/../config.ini"));
    TEST_ASSERT_FALSE(
        FileService::is_allowed_download_path("/20250101/sub/x.avi"));
    TEST_ASSERT_FALSE(
        FileService::is_allowed_download_path("/logs/20250101.txt"));
    TEST_ASSERT_FALSE(
        FileService::is_allowed_download_path("/logs/x/20250101.log"));
    TEST_ASSERT_FALSE(FileService::is_allowed_download_path("/2025010a/x.avi"));
    TEST_ASSERT_FALSE(FileService::is_allowed_download_path(nullptr));
}

/**
 * @brief Log file names must be YYYYMMDD.log.
 */
void test_log_file_name_validation()
{
    TEST_ASSERT_TRUE(FileService::is_log_file_name("20250101.log"));
    TEST_ASSERT_FALSE(FileService::is_log_file_name("2025010.log"));
    TEST_ASSERT_FALSE(FileService::is_log_file_name("2025010a.log"));
    TEST_ASSERT_FALSE(FileService::is_log_file_name("20250101.ini"));
    TEST_ASSERT_FALSE(FileService::is_log_file_name(nullptr));
}

// ============================================================
// LIST_FILES command
// ============================================================

/**
 * @brief LIST_FILES with an empty payload lists the SD root.
 */
void test_list_files_empty_payload_lists_root()
{
    TestFixture f;
    size_t len = 0;
    CommandResult result = f.service.handle_list_files(
        nullptr, 0, response_buf, len);

    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, result);
    std::string json(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json.find("\"dir\":\"/\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"n\":\"20250101\",\"s\":0,\"d\":true") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"n\":\"logs\",\"s\":0,\"d\":true") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"n\":\"config.ini\"") != std::string::npos);
}

/**
 * @brief LIST_FILES of a date directory lists files with sizes.
 */
void test_list_files_date_dir_lists_files_with_sizes()
{
    TestFixture f;
    const char* payload = "/20250101";
    size_t len = 0;
    CommandResult result = f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(payload), strlen(payload),
        response_buf, len);

    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, result);
    std::string json(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json.find("\"total\":2") != std::string::npos);
    TEST_ASSERT_TRUE(
        json.find("\"n\":\"20250101_120000_640x480_10fps.avi\",\"s\":5,"
                  "\"d\":false,\"r\":false") != std::string::npos);
    TEST_ASSERT_TRUE(
        json.find("\"n\":\"20250101_120100_640x480_10fps.jpg\",\"s\":2") !=
        std::string::npos);
}

/**
 * @brief LIST_FILES rejects paths outside the allowlist.
 */
void test_list_files_rejects_disallowed_dir()
{
    TestFixture f;
    const char* payload = "../config.ini";
    size_t len = 0;
    CommandResult result = f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(payload), strlen(payload),
        response_buf, len);

    TEST_ASSERT_EQUAL(CommandResult::INVALID_ARGS, result);

    const char* payload2 = "/etc";
    result = f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(payload2), strlen(payload2),
        response_buf, len);
    TEST_ASSERT_EQUAL(CommandResult::INVALID_ARGS, result);
}

/**
 * @brief LIST_FILES reports SD_UNAVAILABLE when the card is absent.
 */
void test_list_files_sd_unavailable()
{
    TestFixture f;
    f.sd_hal.init_result = hal::SdInitResult::NO_SD_CARD;
    f.sd_controller.init_card();

    size_t len = 0;
    CommandResult result = f.service.handle_list_files(
        nullptr, 0, response_buf, len);
    TEST_ASSERT_EQUAL(CommandResult::SD_UNAVAILABLE, result);
}

/**
 * @brief LIST_FILES pages through a directory under the response cap.
 */
void test_list_files_pagination()
{
    TestFixture f;
    // Seed 30 small files in the date directory.
    for (int i = 0; i < 30; i++)
    {
        char path[96];
        snprintf(path, sizeof(path), "/20250101/f%02d.dat", i);
        f.sd_hal.seed_file(path, {0x01, 0x02});
    }

    size_t len = 0;
    CommandResult result = f.service.handle_list_files(
        nullptr, 0, response_buf, len); // placeholder to avoid unused warning
    (void)result;

    // Page 0 of /20250101
    const char* p0 = "/20250101 0";
    len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(p0), strlen(p0), response_buf, len));
    std::string json0(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json0.find("\"total\":32") != std::string::npos);
    TEST_ASSERT_TRUE(json0.find("\"has_more\":true") != std::string::npos);

    // Page 2 (last): entries 24..31 → 8 entries
    const char* p2 = "/20250101 2";
    len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(p2), strlen(p2), response_buf, len));
    std::string json2(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json2.find("\"has_more\":false") != std::string::npos);
    TEST_ASSERT_TRUE(json2.find("f29.dat") != std::string::npos);

    // Page 3 (beyond the end): empty entries
    const char* p3 = "/20250101 3";
    len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(p3), strlen(p3), response_buf, len));
    std::string json3(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json3.find("\"entries\":[]") != std::string::npos);
}

/**
 * @brief LIST_FILES flags the clip currently being recorded.
 */
void test_list_files_flags_active_clip()
{
    TestFixture f;
    f.service.set_active_clip_provider(
        []() -> const char*
        {
            return "/20250101/20250101_120000_640x480_10fps.avi";
        });

    const char* payload = "/20250101";
    size_t len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(payload), strlen(payload),
        response_buf, len));
    std::string json(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json.find(
        "\"n\":\"20250101_120000_640x480_10fps.avi\",\"s\":5,\"d\":false,"
        "\"r\":true") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(
        "\"n\":\"20250101_120100_640x480_10fps.jpg\",\"s\":2,\"d\":false,"
        "\"r\":false") != std::string::npos);
}

/**
 * @brief LIST_FILES reports an internal error when the listing fails.
 */
void test_list_files_listing_failure()
{
    TestFixture f;
    f.sd_hal.list_dir_result = false;

    size_t len = 0;
    CommandResult result = f.service.handle_list_files(
        nullptr, 0, response_buf, len);
    TEST_ASSERT_EQUAL(CommandResult::INTERNAL_ERROR, result);
}

// ============================================================
// /files route
// ============================================================

/**
 * @brief /files is refused with 401 when no web password is configured.
 */
void test_files_no_password_configured_refused()
{
    TestFixture f;
    TEST_ASSERT_TRUE(f.web.simulate_request("/files", hal::HttpMethod::GET));

    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.web.mock_request.responses[0].code);
}

/**
 * @brief /files challenges without credentials when a password is set.
 */
void test_files_requires_credentials()
{
    TestFixture f;
    f.use_password("secret");

    f.web.mock_request.auth_value[0] = '\0';
    TEST_ASSERT_TRUE(f.web.simulate_request("/files", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(401, f.web.mock_request.responses[0].code);

    f.web.mock_request.set_auth("user", "wrong");
    TEST_ASSERT_TRUE(f.web.simulate_request("/files", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(401, f.web.mock_request.responses[0].code);
}

/**
 * @brief /files serves the root listing JSON with valid credentials.
 */
void test_files_serves_listing_json()
{
    TestFixture f;
    f.use_password("secret");

    TEST_ASSERT_TRUE(f.web.simulate_request("/files", hal::HttpMethod::GET));

    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.web.mock_request.responses[0].code);
    TEST_ASSERT_EQUAL_STRING("application/json",
                             f.web.mock_request.responses[0].content_type.c_str());
    TEST_ASSERT_TRUE(f.web.mock_request.responses[0].body.find(
                         "\"dir\":\"/\"") != std::string::npos);
}

/**
 * @brief /files honors dir and page query parameters.
 */
void test_files_honors_dir_and_page()
{
    TestFixture f;
    f.use_password("secret");
    f.web.mock_request.set_arg("dir", "/logs");
    TEST_ASSERT_TRUE(f.web.simulate_request("/files", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(200, f.web.mock_request.responses[0].code);
    TEST_ASSERT_TRUE(f.web.mock_request.responses[0].body.find(
                         "\"dir\":\"/logs\"") != std::string::npos);
    TEST_ASSERT_TRUE(f.web.mock_request.responses[0].body.find(
                         "\"n\":\"20250101.log\"") != std::string::npos);

    // Disallowed dir → 403 (clear args so the earlier 'dir' is dropped)
    f.web.mock_request.arg_count = 0;
    f.web.mock_request.set_arg("dir", "/etc");
    TEST_ASSERT_TRUE(f.web.simulate_request("/files", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(403, f.web.mock_request.responses[0].code);
}

/**
 * @brief /files reports 503 when the card is unavailable.
 */
void test_files_sd_unavailable_503()
{
    TestFixture f;
    f.use_password("secret");
    f.sd_hal.init_result = hal::SdInitResult::NO_SD_CARD;
    f.sd_controller.init_card();

    TEST_ASSERT_TRUE(f.web.simulate_request("/files", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(503, f.web.mock_request.responses[0].code);
}

// ============================================================
// /logs route
// ============================================================

/**
 * @brief /logs lists the /logs directory (auth policy applies).
 */
void test_logs_route_lists_log_dir()
{
    TestFixture f;
    f.use_password("secret");

    TEST_ASSERT_TRUE(f.web.simulate_request("/logs", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(200, f.web.mock_request.responses[0].code);
    TEST_ASSERT_TRUE(f.web.mock_request.responses[0].body.find(
                         "\"dir\":\"/logs\"") != std::string::npos);
    TEST_ASSERT_TRUE(f.web.mock_request.responses[0].body.find(
                         "\"n\":\"20250101.log\",\"s\":3") != std::string::npos);
}

// ============================================================
// /download route
// ============================================================

/**
 * @brief /download transfers the file body through a data response.
 */
void test_download_serves_file_body()
{
    TestFixture f;
    f.use_password("secret");
    f.web.mock_request.set_arg(
        "path", "/20250101/20250101_120000_640x480_10fps.avi");

    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));

    // No plain response: the body went through the data response.
    TEST_ASSERT_EQUAL_INT(0, f.web.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.data_responses.size());

    const MockRequestRef::DataResponseRecord& rec =
        f.web.mock_request.data_responses[0];
    TEST_ASSERT_EQUAL_STRING("application/octet-stream",
                             rec.content_type.c_str());
    TEST_ASSERT_EQUAL_UINT32(5, rec.total_len);
    TEST_ASSERT_FALSE(rec.fill_mismatch);
    TEST_ASSERT_EQUAL_UINT8(0x01,
        static_cast<uint8_t>(rec.body[0]));
    TEST_ASSERT_EQUAL_UINT8(0x05,
        static_cast<uint8_t>(rec.body[4]));
}

/**
 * @brief /download bounds each SD read to DOWNLOAD_CHUNK_SIZE and
 *        serves the full body of a large file.
 */
void test_download_chunks_bounded_for_large_file()
{
    TestFixture f;
    f.use_password("secret");

    std::vector<uint8_t> big;
    for (size_t i = 0; i < 10240; i++)
    {
        big.push_back(static_cast<uint8_t>(i & 0xFF));
    }
    f.sd_hal.seed_file("/20250101/big.bin", big);

    f.web.mock_request.set_arg("path", "/20250101/big.bin");
    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));

    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.data_responses.size());
    const MockRequestRef::DataResponseRecord& rec =
        f.web.mock_request.data_responses[0];
    TEST_ASSERT_EQUAL_UINT32(10240, rec.total_len);
    TEST_ASSERT_FALSE(rec.fill_mismatch);

    // One filler call must not read more than the bounded chunk size,
    // even when the transport offers a much larger buffer. The filler
    // is offset-driven, so the final partial chunk is served by a
    // subsequent call at the matching offset.
    uint8_t buf[8192];
    TEST_ASSERT_EQUAL_UINT32(FileService::DOWNLOAD_CHUNK_SIZE,
                             rec.filler(buf, sizeof(buf), 0));
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x07, buf[7]);

    const size_t tail_offset = 10240 - (10240 % FileService::DOWNLOAD_CHUNK_SIZE);
    TEST_ASSERT_EQUAL_UINT32(10240 - tail_offset,
                             rec.filler(buf, sizeof(buf), tail_offset));
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[0]);
}

/**
 * @brief /download is refused without a configured password.
 */
void test_download_no_password_configured_refused()
{
    TestFixture f;
    f.web.mock_request.set_arg(
        "path", "/20250101/20250101_120000_640x480_10fps.avi");

    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.web.mock_request.responses[0].code);
}

/**
 * @brief /download rejects traversal and allowlist violations with 403.
 */
void test_download_rejects_disallowed_paths()
{
    TestFixture f;
    f.use_password("secret");

    const char* bad_paths[] =
    {
        "/config.ini",
        "/20250101/../config.ini",
        "/20250101/sub/x.avi",
        "/logs/20250101.txt",
        "20250101/x.avi",
        "/nondate/x.avi",
        ""
    };
    for (const char* path : bad_paths)
    {
        f.web.mock_request.set_arg("path", path);
        TEST_ASSERT_TRUE(
            f.web.simulate_request("/download", hal::HttpMethod::GET));
        TEST_ASSERT_EQUAL_INT(403, f.web.mock_request.responses.back().code);
    }
}

/**
 * @brief /download rejects the clip currently being recorded with 409.
 */
void test_download_rejects_active_clip()
{
    TestFixture f;
    f.use_password("secret");
    f.service.set_active_clip_provider(
        []() -> const char*
        {
            return "/20250101/20250101_120000_640x480_10fps.avi";
        });
    f.web.mock_request.set_arg(
        "path", "/20250101/20250101_120000_640x480_10fps.avi");

    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(409, f.web.mock_request.responses[0].code);
}

/**
 * @brief /download reports 404 for a missing file.
 */
void test_download_missing_file_404()
{
    TestFixture f;
    f.use_password("secret");
    f.web.mock_request.set_arg("path", "/20250101/nope.avi");

    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(404, f.web.mock_request.responses[0].code);
}

/**
 * @brief /download requires the path parameter.
 */
void test_download_missing_param_400()
{
    TestFixture f;
    f.use_password("secret");

    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(400, f.web.mock_request.responses[0].code);
}

/**
 * @brief /download reports 503 when the card is unavailable.
 */
void test_download_sd_unavailable_503()
{
    TestFixture f;
    f.use_password("secret");
    f.sd_hal.init_result = hal::SdInitResult::NO_SD_CARD;
    f.sd_controller.init_card();
    f.web.mock_request.set_arg(
        "path", "/20250101/20250101_120000_640x480_10fps.avi");

    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(503, f.web.mock_request.responses[0].code);
}

// ============================================================
// /log route
// ============================================================

/**
 * @brief /log serves the full content of a small log file.
 */
void test_log_serves_small_file()
{
    TestFixture f;
    f.use_password("secret");
    f.web.mock_request.set_arg("file", "20250101.log");

    TEST_ASSERT_TRUE(f.web.simulate_request("/log", hal::HttpMethod::GET));

    TEST_ASSERT_EQUAL_INT(0, f.web.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.data_responses.size());
    const MockRequestRef::DataResponseRecord& rec =
        f.web.mock_request.data_responses[0];
    TEST_ASSERT_EQUAL_STRING("text/plain", rec.content_type.c_str());
    TEST_ASSERT_EQUAL_UINT32(3, rec.total_len);
    TEST_ASSERT_EQUAL_UINT8('1', static_cast<uint8_t>(rec.body[0]));
}

/**
 * @brief /log serves only the last LOG_TAIL_MAX_BYTES of a large file.
 */
void test_log_serves_tail_of_large_file()
{
    TestFixture f;
    f.use_password("secret");

    // Seed a log larger than the tail limit with recognizable content:
    // the first byte is 'A', the last byte is 'Z'.
    std::vector<uint8_t> big;
    big.push_back('A');
    for (size_t i = 0; i < FileService::LOG_TAIL_MAX_BYTES + 10; i++)
    {
        big.push_back('.');
    }
    big.push_back('Z');
    f.sd_hal.seed_file("/logs/20250201.log", big);

    f.web.mock_request.set_arg("file", "20250201.log");
    TEST_ASSERT_TRUE(f.web.simulate_request("/log", hal::HttpMethod::GET));

    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.data_responses.size());
    const MockRequestRef::DataResponseRecord& rec =
        f.web.mock_request.data_responses[0];
    TEST_ASSERT_EQUAL_UINT32(FileService::LOG_TAIL_MAX_BYTES, rec.total_len);
    TEST_ASSERT_FALSE(rec.fill_mismatch);
    // Tail starts inside the filler region, never at the file head.
    TEST_ASSERT_EQUAL_UINT8('.',
        static_cast<uint8_t>(rec.body[0]));
    TEST_ASSERT_EQUAL_UINT8('Z',
        static_cast<uint8_t>(rec.body[rec.body.size() - 1]));
}

/**
 * @brief /log rejects names outside the YYYYMMDD.log pattern.
 */
void test_log_rejects_invalid_names()
{
    TestFixture f;
    f.use_password("secret");

    const char* bad_names[] =
    {
        "/etc/passwd",
        "../config.ini",
        "20250101.ini",
        "x.log",
        ""
    };
    for (const char* name : bad_names)
    {
        f.web.mock_request.set_arg("file", name);
        TEST_ASSERT_TRUE(
            f.web.simulate_request("/log", hal::HttpMethod::GET));
        TEST_ASSERT_EQUAL_INT(403, f.web.mock_request.responses.back().code);
    }
}

/**
 * @brief /log reports 404 for a missing log file.
 */
void test_log_missing_file_404()
{
    TestFixture f;
    f.use_password("secret");
    f.web.mock_request.set_arg("file", "19990101.log");

    TEST_ASSERT_TRUE(f.web.simulate_request("/log", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(404, f.web.mock_request.responses[0].code);
}

/**
 * @brief /log is refused without a configured password.
 */
void test_log_no_password_configured_refused()
{
    TestFixture f;
    f.web.mock_request.set_arg("file", "20250101.log");

    TEST_ASSERT_TRUE(f.web.simulate_request("/log", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(401, f.web.mock_request.responses[0].code);
}

// ============================================================
// main
// ============================================================

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();

    // is_valid_path
    RUN_TEST(test_is_valid_path_accepts_absolute);
    RUN_TEST(test_is_valid_path_rejects_relative);
    RUN_TEST(test_is_valid_path_rejects_traversal);
    RUN_TEST(test_is_valid_path_rejects_malformed_segments);

    // Listing allowlist
    RUN_TEST(test_listing_allowlist);
    RUN_TEST(test_listing_allowlist_rejections);

    // Download allowlist
    RUN_TEST(test_download_allowlist);
    RUN_TEST(test_download_allowlist_rejections);
    RUN_TEST(test_log_file_name_validation);

    // LIST_FILES command
    RUN_TEST(test_list_files_empty_payload_lists_root);
    RUN_TEST(test_list_files_date_dir_lists_files_with_sizes);
    RUN_TEST(test_list_files_rejects_disallowed_dir);
    RUN_TEST(test_list_files_sd_unavailable);
    RUN_TEST(test_list_files_pagination);
    RUN_TEST(test_list_files_flags_active_clip);
    RUN_TEST(test_list_files_listing_failure);

    // /files route
    RUN_TEST(test_files_no_password_configured_refused);
    RUN_TEST(test_files_requires_credentials);
    RUN_TEST(test_files_serves_listing_json);
    RUN_TEST(test_files_honors_dir_and_page);
    RUN_TEST(test_files_sd_unavailable_503);

    // /logs route
    RUN_TEST(test_logs_route_lists_log_dir);

    // /download route
    RUN_TEST(test_download_serves_file_body);
    RUN_TEST(test_download_chunks_bounded_for_large_file);
    RUN_TEST(test_download_no_password_configured_refused);
    RUN_TEST(test_download_rejects_disallowed_paths);
    RUN_TEST(test_download_rejects_active_clip);
    RUN_TEST(test_download_missing_file_404);
    RUN_TEST(test_download_missing_param_400);
    RUN_TEST(test_download_sd_unavailable_503);

    // /log route
    RUN_TEST(test_log_serves_small_file);
    RUN_TEST(test_log_serves_tail_of_large_file);
    RUN_TEST(test_log_rejects_invalid_names);
    RUN_TEST(test_log_missing_file_404);
    RUN_TEST(test_log_no_password_configured_refused);

    return UNITY_END();
}
