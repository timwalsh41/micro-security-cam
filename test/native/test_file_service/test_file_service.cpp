#include <unity.h>
#include "file_service.h"
#include "command_handler.h"
#include "mock_sd_card_hal.h"
#include "mock_web_server_hal.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
 * @brief LIST_FILES paginates over emittable entries with absolute
 *        continuation indices.
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

    // Start 0 of /20250101: entries 0..11 of 32 emittable entries.
    const char* p0 = "/20250101 0";
    size_t len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(p0), strlen(p0), response_buf, len));
    std::string json0(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json0.find("\"total\":32") != std::string::npos);
    TEST_ASSERT_TRUE(json0.find("\"start\":0") != std::string::npos);
    TEST_ASSERT_TRUE(json0.find("\"more\":true") != std::string::npos);
    TEST_ASSERT_TRUE(json0.find("\"next\":12") != std::string::npos);
    TEST_ASSERT_TRUE(json0.find("f00.dat") != std::string::npos);
    TEST_ASSERT_TRUE(json0.find("f09.dat") != std::string::npos);
    TEST_ASSERT_TRUE(json0.find("f10.dat") == std::string::npos);
    TEST_ASSERT_TRUE(json0.find("\"skipped_long_names\":0") !=
                     std::string::npos);

    // Continuation at 24 (last page): entries 24..31 → 8 entries.
    const char* p2 = "/20250101 24";
    len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(p2), strlen(p2), response_buf, len));
    std::string json2(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json2.find("\"more\":false") != std::string::npos);
    TEST_ASSERT_TRUE(json2.find("\"next\":32") != std::string::npos);
    TEST_ASSERT_TRUE(json2.find("f29.dat") != std::string::npos);
    TEST_ASSERT_TRUE(json2.find("f21.dat") == std::string::npos);

    // Beyond the end: empty page, no more.
    const char* p3 = "/20250101 1000";
    len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(p3), strlen(p3), response_buf, len));
    std::string json3(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json3.find("\"entries\":[]") != std::string::npos);
    TEST_ASSERT_TRUE(json3.find("\"more\":false") != std::string::npos);
}

/**
 * @brief Names too long to serialize are counted and excluded from the
 *        pagination sequence, so page math stays consistent and no
 *        short-named file is skipped or duplicated.
 */
void test_list_files_skips_long_names()
{
    TestFixture f;
    // Two normal files plus one whose name cannot fit the per-entry
    // scratch buffer (bound: 34 + name length + size digits > 175).
    f.sd_hal.seed_file("/20250101/a.dat", {0x01});
    f.sd_hal.seed_file("/20250101/b.dat", {0x01});
    std::string long_name(160, 'x');
    f.sd_hal.seed_file(("/20250101/" + long_name).c_str(), {0x01});

    const char* payload = "/20250101";
    size_t len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(payload), strlen(payload),
        response_buf, len));
    std::string json(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(json.find("\"skipped_long_names\":1") !=
                     std::string::npos);
    // The two seeded fixture files plus a.dat and b.dat are emittable.
    TEST_ASSERT_TRUE(json.find("\"total\":4") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("a.dat") != std::string::npos);
    TEST_ASSERT_TRUE(json.find(long_name) == std::string::npos);

    // Pagination over emittable entries only: the last emittable entry
    // is reachable at start = total - 1 with consistent page math.
    const char* last_payload = "/20250101 3";
    len = 0;
    TEST_ASSERT_EQUAL(CommandResult::SUCCESS, f.service.handle_list_files(
        reinterpret_cast<const uint8_t*>(last_payload),
        strlen(last_payload), response_buf, len));
    std::string last_json(reinterpret_cast<char*>(response_buf), len);
    TEST_ASSERT_TRUE(last_json.find("\"n\":\"b.dat\"") != std::string::npos);
    TEST_ASSERT_TRUE(last_json.find("\"more\":false") != std::string::npos);
    TEST_ASSERT_TRUE(last_json.find("\"next\":4") != std::string::npos);
}

/**
 * @brief When the response buffer fills mid-page, the continuation
 *        index resumes exactly at the first unemitted entry, so
 *        following the "next" chain covers every entry exactly once.
 */
void test_list_files_buffer_full_resumes_at_next_index()
{
    TestFixture f;
    // Long names (~97 chars) make each serialized entry ~132 bytes, so
    // a full 12-entry page cannot fit the 1280-byte response cap.
    std::vector<std::string> names;
    for (int i = 0; i < 10; i++)
    {
        char name[128];
        snprintf(name, sizeof(name), "f%02d_%s.dat", i,
                 std::string(90, 'n').c_str());
        names.push_back(name);
        f.sd_hal.seed_file(("/20250102/" + names.back()).c_str(), {0x01});
    }
    f.sd_hal.directories.insert("/20250102");

    std::vector<std::string> seen;
    uint32_t start = 0;
    bool more = true;
    for (int round = 0; more && round < 10; round++)
    {
        char payload[32];
        snprintf(payload, sizeof(payload), "/20250102 %u", start);
        size_t len = 0;
        TEST_ASSERT_EQUAL(CommandResult::SUCCESS,
            f.service.handle_list_files(
                reinterpret_cast<const uint8_t*>(payload), strlen(payload),
                response_buf, len));
        std::string json(reinterpret_cast<char*>(response_buf), len);

        for (const std::string& name : names)
        {
            if (json.find("\"n\":\"" + name + "\"") != std::string::npos)
            {
                seen.push_back(name);
            }
        }

        size_t more_pos = json.find("\"more\":true");
        more = (more_pos != std::string::npos);
        size_t next_pos = json.find("\"next\":");
        TEST_ASSERT_TRUE(next_pos != std::string::npos);
        uint32_t next = static_cast<uint32_t>(
            std::strtoul(json.c_str() + next_pos + 7, nullptr, 10));
        TEST_ASSERT_TRUE(next > start); // Continuation always advances.
        start = next;
    }

    // Following the continuation chain emitted every entry exactly once.
    TEST_ASSERT_EQUAL_UINT32(names.size(), seen.size());
    for (size_t i = 0; i < names.size(); i++)
    {
        TEST_ASSERT_EQUAL_STRING(names[i].c_str(), seen[i].c_str());
    }
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
 * @brief /files honors dir and start query parameters.
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

/**
 * @brief /logs paginates with the same start/continuation mechanism as
 *        /files, so more than PAGE_SIZE log files stay reachable.
 */
void test_logs_route_paginates()
{
    TestFixture f;
    f.use_password("secret");

    // 15 log files plus the seeded one: 16 emittable entries.
    for (int i = 0; i < 15; i++)
    {
        char path[48];
        snprintf(path, sizeof(path), "/logs/2025%04d.log", i);
        f.sd_hal.seed_file(path, {0x41});
    }

    // First page: 12 entries, continuation at 12.
    f.web.mock_request.set_arg("start", "0");
    TEST_ASSERT_TRUE(f.web.simulate_request("/logs", hal::HttpMethod::GET));
    std::string page0 = f.web.mock_request.responses[0].body;
    TEST_ASSERT_TRUE(page0.find("\"total\":16") != std::string::npos);
    TEST_ASSERT_TRUE(page0.find("\"more\":true") != std::string::npos);
    TEST_ASSERT_TRUE(page0.find("\"next\":12") != std::string::npos);

    // Continuation page covers the remaining 4 entries.
    f.web.mock_request.arg_count = 0;
    f.web.mock_request.set_auth("user", "secret");
    f.web.mock_request.set_arg("start", "12");
    TEST_ASSERT_TRUE(f.web.simulate_request("/logs", hal::HttpMethod::GET));
    std::string page1 = f.web.mock_request.responses[0].body;
    TEST_ASSERT_TRUE(page1.find("\"start\":12") != std::string::npos);
    TEST_ASSERT_TRUE(page1.find("\"more\":false") != std::string::npos);
    TEST_ASSERT_TRUE(page1.find("\"next\":16") != std::string::npos);
    TEST_ASSERT_TRUE(page1.find("20250101.log") != std::string::npos);
    TEST_ASSERT_TRUE(page1.find("20250014.log") != std::string::npos);
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
 * @brief /download stops serving when the requested path becomes the
 *        clip currently being recorded at serve time (rotation while
 *        the transfer is attached).
 */
void test_download_aborts_when_clip_activates_mid_transfer()
{
    TestFixture f;
    f.use_password("secret");

    bool recording_now = false;
    f.service.set_active_clip_provider(
        [&recording_now]() -> const char*
        {
            return recording_now
                ? "/20250101/20250101_120000_640x480_10fps.avi"
                : "";
        });
    f.web.mock_request.set_arg(
        "path", "/20250101/20250101_120000_640x480_10fps.avi");

    TEST_ASSERT_TRUE(f.web.simulate_request("/download", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(1, f.web.mock_request.data_responses.size());
    const MockRequestRef::DataResponseRecord& rec =
        f.web.mock_request.data_responses[0];

    // Before the clip activates, the filler serves body bytes.
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_UINT32(5, rec.filler(buf, sizeof(buf), 0));

    // Once the path is the active clip, the filler refuses further data.
    recording_now = true;
    TEST_ASSERT_EQUAL_UINT32(0, rec.filler(buf, sizeof(buf), 0));
    TEST_ASSERT_EQUAL_UINT32(0, rec.filler(buf, sizeof(buf), 4));
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
    RUN_TEST(test_list_files_skips_long_names);
    RUN_TEST(test_list_files_buffer_full_resumes_at_next_index);
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
    RUN_TEST(test_logs_route_paginates);

    // /download route
    RUN_TEST(test_download_serves_file_body);
    RUN_TEST(test_download_chunks_bounded_for_large_file);
    RUN_TEST(test_download_no_password_configured_refused);
    RUN_TEST(test_download_rejects_disallowed_paths);
    RUN_TEST(test_download_rejects_active_clip);
    RUN_TEST(test_download_aborts_when_clip_activates_mid_transfer);
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
