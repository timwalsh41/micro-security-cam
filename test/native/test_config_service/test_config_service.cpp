#include <unity.h>
#include "config_service.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <Arduino.h>
#include <string>
#include "string.h"

// ============================================================
// Test fixtures
// ============================================================

static MockLogBackend mock_backend;
static RealTimeService real_time;

// ============================================================
// Helper: create a LoggingService wired to the mock backend
// ============================================================

static LoggingService make_logger()
{
    return LoggingService(mock_backend, real_time);
}

// Helper: find a substring in any logged message
static bool any_log_contains(const std::string& needle)
{
    for (const auto& msg : mock_backend.messages)
    {
        if (msg.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// ============================================================
// Default config (before any load)
// ============================================================

/**
 * @brief config() returns defaults before any load.
 */
void test_config_has_defaults_before_load()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    const ConfigData& cfg = svc.config();

    TEST_ASSERT_EQUAL_STRING("", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("", cfg.wifi.password);
    TEST_ASSERT_EQUAL_STRING("", cfg.ap.ssid);
    TEST_ASSERT_EQUAL_STRING("", cfg.ap.password);
    TEST_ASSERT_EQUAL_UINT16(80, cfg.web.port);
    TEST_ASSERT_EQUAL_STRING("", cfg.web.password);
    TEST_ASSERT_EQUAL_STRING("VGA", cfg.camera.resolution);
}

// ============================================================
// Constants
// ============================================================

/**
 * @brief DEFAULT_CONFIG_PATH is /config.ini.
 */
void test_default_config_path()
{
    TEST_ASSERT_EQUAL_STRING("/config.ini", ConfigService::DEFAULT_CONFIG_PATH);
}

/**
 * @brief CONFIG_BUFFER_SIZE is 512.
 */
void test_config_buffer_size()
{
    TEST_ASSERT_EQUAL_UINT32(512, ConfigService::CONFIG_BUFFER_SIZE);
}

/**
 * @brief READ_TIMEOUT_MS is 5000.
 */
void test_read_timeout()
{
    TEST_ASSERT_EQUAL_UINT32(5000, ConfigService::READ_TIMEOUT_MS);
}

// ============================================================
// Successful load
// ============================================================

/**
 * @brief load() parses a valid config buffer.
 */
void test_load_valid_config()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    const char* ini =
        "[wifi]\n"
        "ssid=MyHome\n"
        "password=Secret\n"
        "[ap]\n"
        "ssid=ESP32\n"
        "ap_pass=FallBack\n"
        "[web]\n"
        "port=8080\n"
        "password=WebKey\n"
        "[camera]\n"
        "resolution=HD\n"
        "[video]\n"
        "fps=15\n"
        "resolution=HD\n"
        "clip_duration_s=120\n"
        "max_file_size_mb=200\n"
        "jpeg_quality=5\n";

    TEST_ASSERT_TRUE(svc.load(reinterpret_cast<const uint8_t*>(ini), strlen(ini)));

    const ConfigData& cfg = svc.config();
    TEST_ASSERT_EQUAL_STRING("MyHome", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("Secret", cfg.wifi.password);
    TEST_ASSERT_EQUAL_STRING("ESP32", cfg.ap.ssid);
    TEST_ASSERT_EQUAL_UINT16(8080, cfg.web.port);
    TEST_ASSERT_EQUAL_STRING("HD", cfg.camera.resolution);
    TEST_ASSERT_EQUAL_UINT16(15, cfg.video.fps);
    TEST_ASSERT_EQUAL_STRING("HD", cfg.video.resolution);
    TEST_ASSERT_EQUAL_UINT16(120, cfg.video.clip_duration_s);
    TEST_ASSERT_EQUAL_UINT16(200, cfg.video.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT8(5, cfg.video.jpeg_quality);
}

/**
 * @brief load() logs the parsed buffer size.
 */
void test_load_logs_parse_size()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    const char* ini = "[wifi]\nssid=Test\n";

    svc.load(reinterpret_cast<const uint8_t*>(ini), strlen(ini));

    TEST_ASSERT_TRUE(any_log_contains("17 bytes"));
}

/**
 * @brief load() logs the wifi ssid when set.
 */
void test_load_logs_wifi_ssid_when_set()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    const char* ini = "[wifi]\nssid=MyNet\n";

    svc.load(reinterpret_cast<const uint8_t*>(ini), strlen(ini));

    TEST_ASSERT_TRUE(any_log_contains("MyNet"));
}

/**
 * @brief load() logs the video config.
 */
void test_load_logs_video_config()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    const char* ini = "[video]\nfps=15\nresolution=HD\n";

    svc.load(reinterpret_cast<const uint8_t*>(ini), strlen(ini));

    TEST_ASSERT_TRUE(any_log_contains("15fps"));
    TEST_ASSERT_TRUE(any_log_contains("HD"));
}

// ============================================================
// Empty / null buffer
// ============================================================

/**
 * @brief load() returns false for an empty buffer.
 */
void test_load_empty_buffer_returns_false()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    TEST_ASSERT_FALSE(svc.load(nullptr, 0));
}

/**
 * @brief load() keeps defaults for an empty buffer.
 */
void test_load_empty_buffer_uses_defaults()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    svc.load(nullptr, 0);

    const ConfigData& cfg = svc.config();
    TEST_ASSERT_EQUAL_UINT16(80, cfg.web.port);
    TEST_ASSERT_EQUAL_STRING("VGA", cfg.camera.resolution);
    TEST_ASSERT_EQUAL_UINT16(10, cfg.video.fps);
    TEST_ASSERT_EQUAL_STRING("VGA", cfg.video.resolution);
}

/**
 * @brief load() logs a warning for an empty buffer.
 */
void test_load_empty_buffer_logs_warning()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    svc.load(nullptr, 0);

    TEST_ASSERT_TRUE(any_log_contains("using defaults"));
}

// ============================================================
// Partial config (missing sections)
// ============================================================

/**
 * @brief load() preserves defaults for missing sections.
 */
void test_load_partial_config_preserves_defaults()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    const char* ini = "[wifi]\nssid=Partial\n";

    svc.load(reinterpret_cast<const uint8_t*>(ini), strlen(ini));

    const ConfigData& cfg = svc.config();
    TEST_ASSERT_EQUAL_STRING("Partial", cfg.wifi.ssid);
    TEST_ASSERT_EQUAL_UINT16(80, cfg.web.port);
    TEST_ASSERT_EQUAL_STRING("VGA", cfg.camera.resolution);
    TEST_ASSERT_EQUAL_UINT16(10, cfg.video.fps);
    TEST_ASSERT_EQUAL_STRING("VGA", cfg.video.resolution);
}

// ============================================================
// Reload
// ============================================================

/**
 * @brief load() replaces the previous config on reload.
 */
void test_load_replaces_previous_config()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    const char* ini1 = "[wifi]\nssid=First\n";
    svc.load(reinterpret_cast<const uint8_t*>(ini1), strlen(ini1));
    TEST_ASSERT_EQUAL_STRING("First", svc.config().wifi.ssid);

    const char* ini2 = "[wifi]\nssid=Second\n";
    svc.load(reinterpret_cast<const uint8_t*>(ini2), strlen(ini2));
    TEST_ASSERT_EQUAL_STRING("Second", svc.config().wifi.ssid);
}

// ============================================================
// mutable_config
// ============================================================

/**
 * @brief mutable_config() matches config() before modification.
 */
void test_mutable_config_same_as_config_before_modify()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    const ConfigData& ro = svc.config();
    ConfigData& rw = svc.mutable_config();

    TEST_ASSERT_EQUAL_STRING(ro.wifi.ssid, rw.wifi.ssid);
    TEST_ASSERT_EQUAL_UINT16(ro.web.port, rw.web.port);
    TEST_ASSERT_EQUAL_STRING(ro.camera.resolution, rw.camera.resolution);
}

/**
 * @brief Changes via mutable_config() are visible through config().
 */
void test_mutable_config_modify_visible_through_config()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    svc.mutable_config().web.port = 9090;
    svc.mutable_config().video.fps = 20;
    strcpy(svc.mutable_config().wifi.ssid, "Modified");

    const ConfigData& cfg = svc.config();
    TEST_ASSERT_EQUAL_UINT16(9090, cfg.web.port);
    TEST_ASSERT_EQUAL_UINT16(20, cfg.video.fps);
    TEST_ASSERT_EQUAL_STRING("Modified", cfg.wifi.ssid);
}

/**
 * @brief load() replaces mutable_config() changes.
 */
void test_mutable_config_persists_across_loads()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    svc.mutable_config().web.port = 9090;

    const char* ini = "[wifi]\nssid=AfterLoad\n";
    svc.load(reinterpret_cast<const uint8_t*>(ini), strlen(ini));

    // load replaces entire config, so mutable change is gone
    TEST_ASSERT_EQUAL_UINT16(80, svc.config().web.port);
    TEST_ASSERT_EQUAL_STRING("AfterLoad", svc.config().wifi.ssid);
}

// ============================================================
// Test runner
// ============================================================

void setUp()
{
    mock_backend.messages.clear();
    mock_backend.write_call_count = 0;
    mock_backend.last_message.clear();
}

void tearDown()
{
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    // Defaults
    RUN_TEST(test_config_has_defaults_before_load);

    // Constants
    RUN_TEST(test_default_config_path);
    RUN_TEST(test_config_buffer_size);
    RUN_TEST(test_read_timeout);

    // Successful load
    RUN_TEST(test_load_valid_config);
    RUN_TEST(test_load_logs_parse_size);
    RUN_TEST(test_load_logs_wifi_ssid_when_set);
    RUN_TEST(test_load_logs_video_config);

    // Empty / null buffer
    RUN_TEST(test_load_empty_buffer_returns_false);
    RUN_TEST(test_load_empty_buffer_uses_defaults);
    RUN_TEST(test_load_empty_buffer_logs_warning);

    // Partial config
    RUN_TEST(test_load_partial_config_preserves_defaults);

    // Reload
    RUN_TEST(test_load_replaces_previous_config);

    // mutable_config
    RUN_TEST(test_mutable_config_same_as_config_before_modify);
    RUN_TEST(test_mutable_config_modify_visible_through_config);
    RUN_TEST(test_mutable_config_persists_across_loads);

    return UNITY_END();
}
