#include <unity.h>
#include "config_service.h"
#include "mock_log_backend.h"
#include "mock_sd_card_hal.h"
#include "mock_nvs_hal.h"
#include "sd_controller.h"
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
    TEST_ASSERT_EQUAL_STRING("MicroCam", cfg.ap.ssid);
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
 * @brief CONFIG_BUFFER_SIZE is 1024.
 */
void test_config_buffer_size()
{
    TEST_ASSERT_EQUAL_UINT32(1280, ConfigService::CONFIG_BUFFER_SIZE);
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
 * @brief load() parses the [logging] section into typed fields.
 */
void test_load_logging_config()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    const char* ini =
        "[logging]\n"
        "sd_enabled=false\n"
        "serial_enabled=false\n"
        "serial_min_level=warning\n"
        "sd_min_level=error\n"
        "retention_days=14\n";

    TEST_ASSERT_TRUE(svc.load(reinterpret_cast<const uint8_t*>(ini), strlen(ini)));

    const ConfigData& cfg = svc.config();
    TEST_ASSERT_EQUAL(SdEnabledMode::FALSE, cfg.logging.sd_enabled);
    TEST_ASSERT_FALSE(cfg.logging.serial_enabled);
    TEST_ASSERT_EQUAL_STRING("warning", cfg.logging.serial_min_level);
    TEST_ASSERT_EQUAL_STRING("error", cfg.logging.sd_min_level);
    TEST_ASSERT_EQUAL_UINT8(14, cfg.logging.retention_days);
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
// NVS / SD persistence
// ============================================================

/**
 * @brief Fixture wiring ConfigService to mock NVS and SD storage.
 */
struct PersistenceFixture
{
    MockSdCardHal sd_hal;
    SdController sd_controller;
    LoggingService log;
    ConfigService svc;
    MockNvsHal nvs;

    PersistenceFixture()
        : sd_controller(real_time, sd_hal)
        , log(mock_backend, real_time)
        , svc(log)
    {
        sd_hal.init_result = hal::SdInitResult::SUCCESS;
        sd_controller.init_card();
    }

    /** Serializes the service's live config to an INI byte vector. */
    std::vector<uint8_t> serialized() const
    {
        char buf[ConfigService::CONFIG_BUFFER_SIZE];
        size_t len = svc.serialize(buf, sizeof(buf));
        return std::vector<uint8_t>(buf, buf + len);
    }
};

/** Little-endian bytes of a schema version integer. */
static std::vector<uint8_t> version_bytes(uint32_t v)
{
    return std::vector<uint8_t>{
        static_cast<uint8_t>(v & 0xFFU),
        static_cast<uint8_t>((v >> 8) & 0xFFU),
        static_cast<uint8_t>((v >> 16) & 0xFFU),
        static_cast<uint8_t>((v >> 24) & 0xFFU)};
}

/**
 * @brief save_nvs() then load_nvs() on a fresh service round-trips values.
 */
void test_save_nvs_then_load_nvs_round_trip()
{
    PersistenceFixture f;
    f.svc.set_config_value("wifi.ssid", "Home");
    TEST_ASSERT_TRUE(f.svc.save_nvs(f.nvs));

    // Fresh service over the same NVS backing store (boot simulation).
    ConfigService fresh(f.log);
    TEST_ASSERT_TRUE(fresh.load_nvs(f.nvs));
    TEST_ASSERT_EQUAL_STRING("Home", fresh.config().wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("MicroCam", fresh.config().ap.ssid);
}

/**
 * @brief load_nvs() overlays setting-by-setting: absent keys retain values.
 */
void test_load_nvs_overlays_setting_by_setting()
{
    PersistenceFixture f;
    f.svc.set_config_value("wifi.password", "Secret");
    TEST_ASSERT_TRUE(f.svc.save_nvs(f.nvs));

    ConfigService fresh(f.log);
    TEST_ASSERT_TRUE(fresh.load_nvs(f.nvs));
    TEST_ASSERT_EQUAL_STRING("Secret", fresh.config().wifi.password);
    TEST_ASSERT_EQUAL_STRING("", fresh.config().wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("MicroCam", fresh.config().ap.ssid);
}

/**
 * @brief load_nvs() with no saved config returns false and changes nothing.
 */
void test_load_nvs_without_saved_config()
{
    PersistenceFixture f;

    TEST_ASSERT_FALSE(f.svc.load_nvs(f.nvs));
    TEST_ASSERT_EQUAL_STRING("", f.svc.config().wifi.ssid);
}

/**
 * @brief A saved config with a mismatched schema version is erased and
 *        the compiled-in defaults are kept.
 */
void test_load_nvs_schema_version_mismatch_discards()
{
    PersistenceFixture f;
    f.svc.set_config_value("wifi.ssid", "Stale");
    TEST_ASSERT_TRUE(f.svc.save_nvs(f.nvs));

    // Corrupt the stored schema version to simulate an older firmware.
    f.nvs.seed_blob(ConfigService::NVS_SCHEMA_KEY,
                    version_bytes(ConfigService::NVS_SCHEMA_VERSION + 1));

    // A fresh service (compiled-in defaults) must not pick up the
    // stale config; the stale data is erased.
    ConfigService fresh(f.log);
    TEST_ASSERT_FALSE(fresh.load_nvs(f.nvs));
    TEST_ASSERT_TRUE(f.nvs.blobs.find(ConfigService::NVS_KEY) ==
                     f.nvs.blobs.end());
    TEST_ASSERT_TRUE(f.nvs.blobs.find(ConfigService::NVS_SCHEMA_KEY) ==
                     f.nvs.blobs.end());
    TEST_ASSERT_TRUE(any_log_contains("schema version"));
    TEST_ASSERT_EQUAL_STRING("", fresh.config().wifi.ssid);
}

/**
 * @brief A saved config with no readable schema version is discarded.
 */
void test_load_nvs_missing_schema_version_discards()
{
    PersistenceFixture f;
    f.nvs.seed_blob(ConfigService::NVS_KEY,
                    {static_cast<uint8_t>('[')});

    TEST_ASSERT_FALSE(f.svc.load_nvs(f.nvs));
    TEST_ASSERT_TRUE(f.nvs.blobs.find(ConfigService::NVS_KEY) ==
                     f.nvs.blobs.end());
    TEST_ASSERT_EQUAL_STRING("", f.svc.config().wifi.ssid);
}

/**
 * @brief A failed NVS write leaves the RAM config intact.
 */
void test_save_nvs_failure_leaves_ram_config_intact()
{
    PersistenceFixture f;
    f.svc.set_config_value("wifi.ssid", "Home");
    f.nvs.write_result = false;

    TEST_ASSERT_FALSE(f.svc.save_nvs(f.nvs));
    TEST_ASSERT_EQUAL_STRING("Home", f.svc.config().wifi.ssid);
}

/**
 * @brief save_sd() then load_sd() on a fresh service round-trips values.
 */
void test_save_sd_then_load_sd_round_trip()
{
    PersistenceFixture f;
    f.svc.set_config_value("wifi.ssid", "Home");
    TEST_ASSERT_TRUE(f.svc.save_sd(f.sd_controller));

    ConfigService fresh(f.log);
    TEST_ASSERT_TRUE(fresh.load_sd(f.sd_controller));
    TEST_ASSERT_EQUAL_STRING("Home", fresh.config().wifi.ssid);
}

/**
 * @brief Boot overlay order: defaults < NVS < SD config file.
 */
void test_overlay_order_defaults_then_nvs_then_sd()
{
    PersistenceFixture f;

    // Defaults: wifi.ssid blank, ap.ssid "MicroCam".
    f.svc.set_config_value("wifi.ssid", "Home");
    f.svc.set_config_value("wifi.password", "Secret");
    TEST_ASSERT_TRUE(f.svc.save_nvs(f.nvs));

    // SD file overrides wifi.ssid and web.port, leaves the rest.
    const char* ini =
        "[wifi]\n"
        "ssid=Camp\n";
    f.sd_hal.files[ConfigService::DEFAULT_CONFIG_PATH] =
        std::vector<uint8_t>(ini, ini + strlen(ini));

    // Simulate the boot sequence on a defaults-fresh service sharing
    // the same NVS and SD backing stores.
    ConfigService fresh(f.log);
    TEST_ASSERT_TRUE(fresh.load_nvs(f.nvs));
    TEST_ASSERT_TRUE(fresh.load_sd(f.sd_controller));

    // SD wins over NVS on conflict.
    TEST_ASSERT_EQUAL_STRING("Camp", fresh.config().wifi.ssid);
    // NVS value survives where the SD file is silent.
    TEST_ASSERT_EQUAL_STRING("Secret", fresh.config().wifi.password);
    // Defaults survive where neither source speaks.
    TEST_ASSERT_EQUAL_STRING("MicroCam", fresh.config().ap.ssid);
}

/**
 * @brief Serialization round-trips every value through parse().
 */
void test_serialize_round_trip()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    svc.set_config_value("wifi.ssid", "Home");
    svc.set_config_value("wifi.password", "Secret");
    svc.set_config_value("web.password", "WebPass");
    svc.set_config_value("camera.resolution", "HD");

    char buf[ConfigService::CONFIG_BUFFER_SIZE];
    size_t len = svc.serialize(buf, sizeof(buf));

    ConfigData parsed = IniParser::parse(buf, len);
    TEST_ASSERT_EQUAL_STRING("Home", parsed.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("Secret", parsed.wifi.password);
    TEST_ASSERT_EQUAL_STRING("WebPass", parsed.web.password);
    TEST_ASSERT_EQUAL_STRING("HD", parsed.camera.resolution);
    TEST_ASSERT_EQUAL_STRING("MicroCam", parsed.ap.ssid);
}

// ============================================================
// Typed config values (numeric and boolean settings)
// ============================================================

/**
 * @brief Numeric and boolean settings read back formatted as in
 *        serialized INI text.
 */
void test_get_numeric_fields_formatted()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    char value[CONFIG_FIELD_MAX_LENGTH];

    TEST_ASSERT_TRUE(svc.get_config_value("web.port", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("80", value);

    TEST_ASSERT_TRUE(svc.get_config_value("web.max_streams", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("4", value);

    TEST_ASSERT_TRUE(svc.get_config_value("camera.grab_mode", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("ff", value);

    TEST_ASSERT_TRUE(svc.get_config_value("video.audio_enabled", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("false", value);
}

/**
 * @brief Numeric settings set/get round-trip through the live config.
 */
void test_set_get_numeric_round_trip()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    char value[CONFIG_FIELD_MAX_LENGTH];

    TEST_ASSERT_TRUE(svc.set_config_value("web.port", "8080"));
    TEST_ASSERT_TRUE(svc.get_config_value("web.port", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("8080", value);
    TEST_ASSERT_EQUAL_UINT16(8080, svc.config().web.port);

    TEST_ASSERT_TRUE(svc.set_config_value("web.max_streams", "2"));
    TEST_ASSERT_TRUE(svc.get_config_value("web.max_streams", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("2", value);
    TEST_ASSERT_EQUAL_UINT8(2, svc.config().web.max_streams);

    TEST_ASSERT_TRUE(svc.set_config_value("video.fps", "15"));
    TEST_ASSERT_TRUE(svc.get_config_value("video.fps", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("15", value);
    TEST_ASSERT_EQUAL_UINT16(15, svc.config().video.fps);

    TEST_ASSERT_TRUE(svc.set_config_value("video.clip_duration_s", "120"));
    TEST_ASSERT_TRUE(svc.get_config_value("video.clip_duration_s", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("120", value);
    TEST_ASSERT_EQUAL_UINT16(120, svc.config().video.clip_duration_s);

    TEST_ASSERT_TRUE(svc.set_config_value("video.max_file_size_mb", "100"));
    TEST_ASSERT_TRUE(svc.get_config_value("video.max_file_size_mb", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("100", value);
    TEST_ASSERT_EQUAL_UINT16(100, svc.config().video.max_file_size_mb);

    TEST_ASSERT_TRUE(svc.set_config_value("camera.jpeg_quality", "20"));
    TEST_ASSERT_TRUE(svc.get_config_value("camera.jpeg_quality", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("20", value);
    TEST_ASSERT_EQUAL_UINT8(20, svc.config().camera.jpeg_quality);

    TEST_ASSERT_TRUE(svc.set_config_value("video.jpeg_quality", "25"));
    TEST_ASSERT_TRUE(svc.get_config_value("video.jpeg_quality", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("25", value);
    TEST_ASSERT_EQUAL_UINT8(25, svc.config().video.jpeg_quality);

    TEST_ASSERT_TRUE(svc.set_config_value("video.mic_gain", "5"));
    TEST_ASSERT_TRUE(svc.get_config_value("video.mic_gain", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("5", value);
    TEST_ASSERT_EQUAL_UINT8(5, svc.config().video.mic_gain);
}

/**
 * @brief Boolean settings round-trip as "true"/"false" text.
 */
void test_set_get_bool_round_trip()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    char value[CONFIG_FIELD_MAX_LENGTH];

    TEST_ASSERT_TRUE(svc.set_config_value("video.audio_enabled", "true"));
    TEST_ASSERT_TRUE(svc.config().video.audio_enabled);
    TEST_ASSERT_TRUE(svc.get_config_value("video.audio_enabled", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);

    TEST_ASSERT_TRUE(svc.set_config_value("video.audio_enabled", "false"));
    TEST_ASSERT_FALSE(svc.config().video.audio_enabled);
    TEST_ASSERT_TRUE(svc.get_config_value("video.audio_enabled", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("false", value);

    // Case-insensitive, matching the INI parser.
    TEST_ASSERT_TRUE(svc.set_config_value("video.audio_enabled", "TRUE"));
    TEST_ASSERT_TRUE(svc.config().video.audio_enabled);
}

/**
 * @brief Hexadecimal grab_mode round-trips through get/set.
 */
void test_set_get_grab_mode_hex_round_trip()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    char value[CONFIG_FIELD_MAX_LENGTH];

    TEST_ASSERT_TRUE(svc.set_config_value("camera.grab_mode", "1a"));
    TEST_ASSERT_EQUAL_HEX8(0x1A, svc.config().camera.grab_mode);
    TEST_ASSERT_TRUE(svc.get_config_value("camera.grab_mode", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("1a", value);
}

/**
 * @brief camera.rotation round-trips through get/set; only 0 and 180
 *        are accepted.
 */
void test_set_get_rotation_round_trip()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    char value[CONFIG_FIELD_MAX_LENGTH];

    TEST_ASSERT_TRUE(svc.set_config_value("camera.rotation", "180"));
    TEST_ASSERT_EQUAL_UINT16(180, svc.config().camera.rotation);
    TEST_ASSERT_TRUE(svc.get_config_value("camera.rotation", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("180", value);

    TEST_ASSERT_TRUE(svc.set_config_value("camera.rotation", "0"));
    TEST_ASSERT_EQUAL_UINT16(0, svc.config().camera.rotation);
}

/**
 * @brief Out-of-range numeric values are rejected and leave the
 *        setting unchanged.
 */
void test_set_numeric_out_of_range_rejected()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    svc.set_config_value("web.port", "8080");

    TEST_ASSERT_FALSE(svc.set_config_value("web.port", "0"));
    TEST_ASSERT_EQUAL_UINT16(8080, svc.config().web.port);

    TEST_ASSERT_FALSE(svc.set_config_value("web.port", "65536"));
    TEST_ASSERT_EQUAL_UINT16(8080, svc.config().web.port);

    TEST_ASSERT_FALSE(svc.set_config_value("web.max_streams", "5"));
    TEST_ASSERT_EQUAL_UINT8(4, svc.config().web.max_streams);

    TEST_ASSERT_FALSE(svc.set_config_value("web.max_streams", "0"));
    TEST_ASSERT_EQUAL_UINT8(4, svc.config().web.max_streams);

    TEST_ASSERT_FALSE(svc.set_config_value("video.fps", "31"));
    TEST_ASSERT_EQUAL_UINT16(10, svc.config().video.fps);

    TEST_ASSERT_FALSE(svc.set_config_value("video.mic_gain", "9"));
    TEST_ASSERT_EQUAL_UINT8(2, svc.config().video.mic_gain);

    TEST_ASSERT_FALSE(svc.set_config_value("camera.jpeg_quality", "64"));
    TEST_ASSERT_EQUAL_UINT8(10, svc.config().camera.jpeg_quality);

    TEST_ASSERT_FALSE(svc.set_config_value("video.jpeg_quality", "64"));
    TEST_ASSERT_EQUAL_UINT8(10, svc.config().video.jpeg_quality);

    TEST_ASSERT_FALSE(svc.set_config_value("camera.grab_mode", "1ff"));
    TEST_ASSERT_EQUAL_HEX8(0xFF, svc.config().camera.grab_mode);

    TEST_ASSERT_FALSE(svc.set_config_value("camera.rotation", "90"));
    TEST_ASSERT_EQUAL_UINT16(0, svc.config().camera.rotation);
}

/**
 * @brief Unparseable numeric/boolean values are rejected.
 */
void test_set_numeric_unparseable_rejected()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    svc.set_config_value("video.fps", "15");

    TEST_ASSERT_FALSE(svc.set_config_value("web.port", "abc"));
    TEST_ASSERT_FALSE(svc.set_config_value("web.port", ""));
    TEST_ASSERT_FALSE(svc.set_config_value("video.fps", "12x"));
    TEST_ASSERT_FALSE(svc.set_config_value("video.audio_enabled", "yes"));

    // Rejected writes leave the live config unchanged.
    TEST_ASSERT_EQUAL_UINT16(15, svc.config().video.fps);
    TEST_ASSERT_EQUAL_UINT16(80, svc.config().web.port);
    TEST_ASSERT_FALSE(svc.config().video.audio_enabled);
}

/**
 * @brief Unknown keys are rejected by set and get.
 */
void test_unknown_typed_key_rejected()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    char value[CONFIG_FIELD_MAX_LENGTH];
    TEST_ASSERT_FALSE(svc.set_config_value("web.bogus", "1"));
    TEST_ASSERT_FALSE(svc.get_config_value("web.bogus", value, sizeof(value)));
    TEST_ASSERT_FALSE(svc.set_config_value("camera.bogus", "1"));
    TEST_ASSERT_FALSE(svc.get_config_value("camera.bogus", value, sizeof(value)));
}

/**
 * @brief A typed set survives a serialize/parse round trip.
 */
void test_typed_set_serialize_round_trip()
{
    LoggingService log = make_logger();
    ConfigService svc(log);

    svc.set_config_value("web.port", "8080");
    svc.set_config_value("video.fps", "15");
    svc.set_config_value("video.audio_enabled", "true");

    char buf[ConfigService::CONFIG_BUFFER_SIZE];
    size_t len = svc.serialize(buf, sizeof(buf));

    ConfigData parsed = IniParser::parse(buf, len);
    TEST_ASSERT_EQUAL_UINT16(8080, parsed.web.port);
    TEST_ASSERT_EQUAL_UINT16(15, parsed.video.fps);
    TEST_ASSERT_TRUE(parsed.video.audio_enabled);
}

// ============================================================
// SdEnabledMode enum tests
// ============================================================

/**
 * @brief Default sd_enabled is AFTER_SET_TIME.
 */
void test_sd_enabled_default_is_after_set_time()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    TEST_ASSERT_EQUAL(SdEnabledMode::AFTER_SET_TIME, svc.config().logging.sd_enabled);
}

/**
 * @brief set/get sd_enabled with "true" returns SdEnabledMode::TRUE.
 */
void test_sd_enabled_set_get_true()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    char value[CONFIG_FIELD_MAX_LENGTH];
    TEST_ASSERT_TRUE(svc.set_config_value("logging.sd_enabled", "true"));
    TEST_ASSERT_EQUAL(SdEnabledMode::TRUE, svc.config().logging.sd_enabled);
    TEST_ASSERT_TRUE(svc.get_config_value("logging.sd_enabled", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);
}

/**
 * @brief set/get sd_enabled with "false" returns SdEnabledMode::FALSE.
 */
void test_sd_enabled_set_get_false()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    char value[CONFIG_FIELD_MAX_LENGTH];
    TEST_ASSERT_TRUE(svc.set_config_value("logging.sd_enabled", "false"));
    TEST_ASSERT_EQUAL(SdEnabledMode::FALSE, svc.config().logging.sd_enabled);
    TEST_ASSERT_TRUE(svc.get_config_value("logging.sd_enabled", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("false", value);
}

/**
 * @brief set/get sd_enabled with "after_set_time" returns SdEnabledMode::AFTER_SET_TIME.
 */
void test_sd_enabled_set_get_after_set_time()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    char value[CONFIG_FIELD_MAX_LENGTH];
    TEST_ASSERT_TRUE(svc.set_config_value("logging.sd_enabled", "after_set_time"));
    TEST_ASSERT_EQUAL(SdEnabledMode::AFTER_SET_TIME, svc.config().logging.sd_enabled);
    TEST_ASSERT_TRUE(svc.get_config_value("logging.sd_enabled", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("after_set_time", value);
}

/**
 * @brief Unrecognized sd_enabled value is rejected.
 */
void test_sd_enabled_unrecognized_rejected()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    TEST_ASSERT_FALSE(svc.set_config_value("logging.sd_enabled", "invalid"));
    TEST_ASSERT_EQUAL(SdEnabledMode::AFTER_SET_TIME, svc.config().logging.sd_enabled);
}

/**
 * @brief sd_enabled case-insensitive parsing works.
 */
void test_sd_enabled_case_insensitive()
{
    LoggingService log = make_logger();
    ConfigService svc(log);
    TEST_ASSERT_TRUE(svc.set_config_value("logging.sd_enabled", "TRUE"));
    TEST_ASSERT_EQUAL(SdEnabledMode::TRUE, svc.config().logging.sd_enabled);
    TEST_ASSERT_TRUE(svc.set_config_value("logging.sd_enabled", "FALSE"));
    TEST_ASSERT_EQUAL(SdEnabledMode::FALSE, svc.config().logging.sd_enabled);
    TEST_ASSERT_TRUE(svc.set_config_value("logging.sd_enabled", "After_Set_Time"));
    TEST_ASSERT_EQUAL(SdEnabledMode::AFTER_SET_TIME, svc.config().logging.sd_enabled);
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
    RUN_TEST(test_load_logging_config);
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

    // NVS / SD persistence
    RUN_TEST(test_save_nvs_then_load_nvs_round_trip);
    RUN_TEST(test_load_nvs_overlays_setting_by_setting);
    RUN_TEST(test_load_nvs_without_saved_config);
    RUN_TEST(test_load_nvs_schema_version_mismatch_discards);
    RUN_TEST(test_load_nvs_missing_schema_version_discards);
    RUN_TEST(test_save_nvs_failure_leaves_ram_config_intact);
    RUN_TEST(test_save_sd_then_load_sd_round_trip);
    RUN_TEST(test_overlay_order_defaults_then_nvs_then_sd);
    RUN_TEST(test_serialize_round_trip);
    RUN_TEST(test_get_numeric_fields_formatted);
    RUN_TEST(test_set_get_numeric_round_trip);
    RUN_TEST(test_set_get_bool_round_trip);
    RUN_TEST(test_set_get_grab_mode_hex_round_trip);
    RUN_TEST(test_set_get_rotation_round_trip);
    RUN_TEST(test_set_numeric_out_of_range_rejected);
    RUN_TEST(test_set_numeric_unparseable_rejected);
    RUN_TEST(test_unknown_typed_key_rejected);
    RUN_TEST(test_typed_set_serialize_round_trip);

    // SdEnabledMode enum
    RUN_TEST(test_sd_enabled_default_is_after_set_time);
    RUN_TEST(test_sd_enabled_set_get_true);
    RUN_TEST(test_sd_enabled_set_get_false);
    RUN_TEST(test_sd_enabled_set_get_after_set_time);
    RUN_TEST(test_sd_enabled_unrecognized_rejected);
    RUN_TEST(test_sd_enabled_case_insensitive);

    return UNITY_END();
}
