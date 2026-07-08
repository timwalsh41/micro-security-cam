#include <unity.h>
#include "ini_parser.h"
#include <cstring>
#include <cstdio>

// ============================================================
// Default values
// ============================================================

void test_defaults_has_empty_wifi_ssid()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_STRING("", d.wifi.ssid);
}

void test_defaults_has_empty_wifi_password()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_STRING("", d.wifi.password);
}

void test_defaults_has_empty_ap_ssid()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_STRING("", d.ap.ssid);
}

void test_defaults_has_empty_ap_password()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_STRING("", d.ap.password);
}

void test_defaults_has_web_port_80()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_UINT16(80, d.web.port);
}

void test_defaults_has_empty_web_password()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_STRING("", d.web.password);
}

void test_defaults_has_camera_interval_20000()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_UINT32(20000, d.camera.capture_interval_ms);
}

void test_defaults_has_video_fps_10()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_UINT16(10, d.video.fps);
}

void test_defaults_has_video_resolution_vga()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_STRING("VGA", d.video.resolution);
}

void test_defaults_has_video_clip_duration_60()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_UINT16(60, d.video.clip_duration_s);
}

void test_defaults_has_video_max_file_size_0()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_UINT16(0, d.video.max_file_size_mb);
}

void test_defaults_has_video_jpeg_quality_10()
{
    ConfigData d = IniParser::defaults();
    TEST_ASSERT_EQUAL_UINT8(10, d.video.jpeg_quality);
}

// ============================================================
// Empty / null input
// ============================================================

void test_empty_input_returns_defaults()
{
    ConfigData d = IniParser::parse("", 0);
    TEST_ASSERT_EQUAL_STRING("", d.wifi.ssid);
    TEST_ASSERT_EQUAL_UINT16(80, d.web.port);
    TEST_ASSERT_EQUAL_UINT32(20000, d.camera.capture_interval_ms);
    TEST_ASSERT_EQUAL_UINT16(10, d.video.fps);
    TEST_ASSERT_EQUAL_STRING("VGA", d.video.resolution);
}

void test_null_input_returns_defaults()
{
    ConfigData d = IniParser::parse(nullptr, 0);
    TEST_ASSERT_EQUAL_STRING("", d.wifi.ssid);
}

// ============================================================
// Single section with one key
// ============================================================

void test_single_wifi_ssid()
{
    const char* ini = "[wifi]\nssid=MyNetwork\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("MyNetwork", d.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("", d.wifi.password);
}

void test_single_wifi_password()
{
    const char* ini = "[wifi]\npassword=Secret123\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Secret123", d.wifi.password);
}

// ============================================================
// Multiple sections
// ============================================================

void test_multiple_sections()
{
    const char* ini =
        "[wifi]\n"
        "ssid=HomeNet\n"
        "password=Pass123\n"
        "[ap]\n"
        "ssid=ESP32\n"
        "password=Fallback\n"
        "[web]\n"
        "port=8080\n"
        "password=WebPass\n"
        "[camera]\n"
        "capture_interval_ms=5000\n"
        "[video]\n"
        "fps=15\n"
        "resolution=HD\n"
        "clip_duration_s=120\n"
        "max_file_size_mb=200\n"
        "jpeg_quality=5\n";

    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("HomeNet", d.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("Pass123", d.wifi.password);
    TEST_ASSERT_EQUAL_STRING("ESP32", d.ap.ssid);
    TEST_ASSERT_EQUAL_STRING("Fallback", d.ap.password);
    TEST_ASSERT_EQUAL_UINT16(8080, d.web.port);
    TEST_ASSERT_EQUAL_STRING("WebPass", d.web.password);
    TEST_ASSERT_EQUAL_UINT32(5000, d.camera.capture_interval_ms);
    TEST_ASSERT_EQUAL_UINT16(15, d.video.fps);
    TEST_ASSERT_EQUAL_STRING("HD", d.video.resolution);
    TEST_ASSERT_EQUAL_UINT16(120, d.video.clip_duration_s);
    TEST_ASSERT_EQUAL_UINT16(200, d.video.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT8(5, d.video.jpeg_quality);
}

// ============================================================
// Comments and blank lines
// ============================================================

void test_comments_ignored()
{
    const char* ini =
        "# This is a comment\n"
        "; So is this\n"
        "[wifi]\n"
        "ssid=Test\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

void test_blank_lines_ignored()
{
    const char* ini = "\n\n[wifi]\n\nssid=Test\n\n\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

// ============================================================
// Whitespace trimming
// ============================================================

void test_whitespace_around_key()
{
    const char* ini = "[wifi]\n  ssid  =  TestNet\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("TestNet", d.wifi.ssid);
}

void test_whitespace_around_value()
{
    const char* ini = "[wifi]\npassword=  secret  \n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("secret", d.wifi.password);
}

void test_whitespace_around_section_header()
{
    const char* ini = "  [wifi]  \nssid=Test\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

// ============================================================
// Unknown keys and sections
// ============================================================

void test_unknown_keys_ignored()
{
    const char* ini =
        "[wifi]\n"
        "ssid=Test\n"
        "unknown_key=value\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

void test_unknown_sections_ignored()
{
    const char* ini =
        "[unknown]\n"
        "key=value\n"
        "[wifi]\n"
        "ssid=Test\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

// ============================================================
// Missing keys retain defaults
// ============================================================

void test_missing_keys_retain_defaults()
{
    const char* ini =
        "[wifi]\n"
        "ssid=Test\n"
        "[web]\n"
        "port=9090\n";

    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("", d.wifi.password);
    TEST_ASSERT_EQUAL_STRING("", d.ap.ssid);
    TEST_ASSERT_EQUAL_UINT16(9090, d.web.port);
    TEST_ASSERT_EQUAL_UINT32(20000, d.camera.capture_interval_ms);
}

// ============================================================
// Malformed lines
// ============================================================

void test_malformed_line_without_equals_skipped()
{
    const char* ini =
        "[wifi]\n"
        "no_equals_sign\n"
        "ssid=Test\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

// ============================================================
// Edge cases
// ============================================================

void test_no_trailing_newline()
{
    const char* ini = "[wifi]\nssid=Test";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

void test_section_header_only()
{
    const char* ini = "[wifi]\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("", d.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("", d.wifi.password);
}

void test_value_contains_equals()
{
    const char* ini = "[wifi]\npassword=ab=cd\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("ab=cd", d.wifi.password);
}

void test_long_value_truncated_to_field_size()
{
    char ini_buf[256];
    snprintf(ini_buf, sizeof(ini_buf),
             "[wifi]\npassword=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");

    ConfigData d = IniParser::parse(ini_buf, strlen(ini_buf));
    TEST_ASSERT_EQUAL_INT(CONFIG_FIELD_MAX_LENGTH - 1,
                          static_cast<int>(strlen(d.wifi.password)));
}

void test_case_insensitive_section_names()
{
    const char* ini = "[WIFI]\nssid=Test\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

void test_case_insensitive_key_names()
{
    const char* ini = "[wifi]\nSSID=Test\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_STRING("Test", d.wifi.ssid);
}

void test_port_out_of_range_ignored()
{
    const char* ini = "[web]\nport=99999\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT16(80, d.web.port);
}

void test_camera_interval_zero()
{
    const char* ini = "[camera]\ncapture_interval_ms=0\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT32(0, d.camera.capture_interval_ms);
}

// ============================================================
// Video section parsing
// ============================================================

void test_video_all_keys()
{
    const char* ini =
        "[video]\n"
        "fps=15\n"
        "resolution=HD\n"
        "clip_duration_s=120\n"
        "max_file_size_mb=200\n"
        "jpeg_quality=5\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT16(15, d.video.fps);
    TEST_ASSERT_EQUAL_STRING("HD", d.video.resolution);
    TEST_ASSERT_EQUAL_UINT16(120, d.video.clip_duration_s);
    TEST_ASSERT_EQUAL_UINT16(200, d.video.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT8(5, d.video.jpeg_quality);
}

void test_video_partial_keys_retain_defaults()
{
    const char* ini =
        "[video]\n"
        "fps=20\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT16(20, d.video.fps);
    TEST_ASSERT_EQUAL_STRING("VGA", d.video.resolution);
    TEST_ASSERT_EQUAL_UINT16(60, d.video.clip_duration_s);
    TEST_ASSERT_EQUAL_UINT16(0, d.video.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT8(10, d.video.jpeg_quality);
}

void test_video_fps_out_of_range_ignored()
{
    const char* ini = "[video]\nfps=50\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT16(10, d.video.fps);
}

void test_video_fps_zero_ignored()
{
    const char* ini = "[video]\nfps=0\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT16(10, d.video.fps);
}

void test_video_fps_minimum_accepted()
{
    const char* ini = "[video]\nfps=1\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT16(1, d.video.fps);
}

void test_video_fps_maximum_accepted()
{
    const char* ini = "[video]\nfps=30\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT16(30, d.video.fps);
}

void test_video_jpeg_quality_out_of_range_ignored()
{
    const char* ini = "[video]\njpeg_quality=100\n";
    ConfigData d = IniParser::parse(ini, strlen(ini));
    TEST_ASSERT_EQUAL_UINT8(10, d.video.jpeg_quality);
}

// ============================================================
// Serialization
// ============================================================

void test_serialize_defaults()
{
    ConfigData d = IniParser::defaults();
    char buf[1024];
    size_t written = IniParser::serialize(d, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0u, written);
    TEST_ASSERT_LESS_THAN(sizeof(buf), written);

    // Verify it contains all section headers
    TEST_ASSERT_NOT_NULL(strstr(buf, "[wifi]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[ap]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[web]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[camera]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[video]"));

    // Verify default integer values appear
    TEST_ASSERT_NOT_NULL(strstr(buf, "port=80"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "capture_interval_ms=20000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "fps=10"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "resolution=VGA"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "clip_duration_s=60"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "max_file_size_mb=0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "jpeg_quality=10"));
}

void test_serialize_non_default_values()
{
    ConfigData d = IniParser::defaults();
    strcpy(d.wifi.ssid, "HomeNet");
    strcpy(d.wifi.password, "Pass123");
    strcpy(d.ap.ssid, "ESP32");
    strcpy(d.ap.password, "Fallback");
    d.web.port = 8080;
    strcpy(d.web.password, "WebPass");
    d.camera.capture_interval_ms = 5000;
    d.video.fps = 15;
    strcpy(d.video.resolution, "HD");
    d.video.clip_duration_s = 120;
    d.video.max_file_size_mb = 200;
    d.video.jpeg_quality = 5;

    char buf[1024];
    size_t written = IniParser::serialize(d, buf, sizeof(buf));

    TEST_ASSERT_GREATER_THAN(0u, written);
    TEST_ASSERT_NOT_NULL(strstr(buf, "ssid=HomeNet"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "password=Pass123"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "port=8080"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "capture_interval_ms=5000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "fps=15"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "resolution=HD"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "clip_duration_s=120"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "max_file_size_mb=200"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "jpeg_quality=5"));
}

void test_roundtrip_defaults()
{
    ConfigData original = IniParser::defaults();
    char buf[1024];
    IniParser::serialize(original, buf, sizeof(buf));

    ConfigData parsed = IniParser::parse(buf, strlen(buf));

    TEST_ASSERT_EQUAL_STRING(original.wifi.ssid, parsed.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING(original.wifi.password, parsed.wifi.password);
    TEST_ASSERT_EQUAL_STRING(original.ap.ssid, parsed.ap.ssid);
    TEST_ASSERT_EQUAL_STRING(original.ap.password, parsed.ap.password);
    TEST_ASSERT_EQUAL_UINT16(original.web.port, parsed.web.port);
    TEST_ASSERT_EQUAL_STRING(original.web.password, parsed.web.password);
    TEST_ASSERT_EQUAL_UINT32(original.camera.capture_interval_ms,
                             parsed.camera.capture_interval_ms);
    TEST_ASSERT_EQUAL_UINT16(original.video.fps, parsed.video.fps);
    TEST_ASSERT_EQUAL_STRING(original.video.resolution, parsed.video.resolution);
    TEST_ASSERT_EQUAL_UINT16(original.video.clip_duration_s,
                             parsed.video.clip_duration_s);
    TEST_ASSERT_EQUAL_UINT16(original.video.max_file_size_mb,
                             parsed.video.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT8(original.video.jpeg_quality,
                            parsed.video.jpeg_quality);
}

void test_roundtrip_custom_values()
{
    ConfigData original = IniParser::defaults();
    strcpy(original.wifi.ssid, "MySSID");
    strcpy(original.wifi.password, "MyPass");
    strcpy(original.ap.ssid, "FallbackAP");
    strcpy(original.ap.password, "ApPass");
    original.web.port = 9090;
    strcpy(original.web.password, "WebSecret");
    original.camera.capture_interval_ms = 30000;
    original.video.fps = 15;
    strcpy(original.video.resolution, "HD");
    original.video.clip_duration_s = 120;
    original.video.max_file_size_mb = 200;
    original.video.jpeg_quality = 5;

    char buf[1024];
    IniParser::serialize(original, buf, sizeof(buf));

    ConfigData parsed = IniParser::parse(buf, strlen(buf));

    TEST_ASSERT_EQUAL_STRING(original.wifi.ssid, parsed.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING(original.wifi.password, parsed.wifi.password);
    TEST_ASSERT_EQUAL_STRING(original.ap.ssid, parsed.ap.ssid);
    TEST_ASSERT_EQUAL_STRING(original.ap.password, parsed.ap.password);
    TEST_ASSERT_EQUAL_UINT16(original.web.port, parsed.web.port);
    TEST_ASSERT_EQUAL_STRING(original.web.password, parsed.web.password);
    TEST_ASSERT_EQUAL_UINT32(original.camera.capture_interval_ms,
                             parsed.camera.capture_interval_ms);
    TEST_ASSERT_EQUAL_UINT16(original.video.fps, parsed.video.fps);
    TEST_ASSERT_EQUAL_STRING(original.video.resolution, parsed.video.resolution);
    TEST_ASSERT_EQUAL_UINT16(original.video.clip_duration_s,
                             parsed.video.clip_duration_s);
    TEST_ASSERT_EQUAL_UINT16(original.video.max_file_size_mb,
                             parsed.video.max_file_size_mb);
    TEST_ASSERT_EQUAL_UINT8(original.video.jpeg_quality,
                            parsed.video.jpeg_quality);
}

void test_serialize_buffer_too_small_returns_bytes_needed()
{
    ConfigData d = IniParser::defaults();
    char buf[4]; // Way too small
    size_t needed = IniParser::serialize(d, buf, sizeof(buf));

    // Should return the full size needed, not the truncated size
    TEST_ASSERT_GREATER_THAN(sizeof(buf), needed);
}

void test_serialize_null_buffer_returns_bytes_needed()
{
    ConfigData d = IniParser::defaults();
    size_t needed1 = IniParser::serialize(d, nullptr, 0);

    char real_buf[1024];
    size_t needed2 = IniParser::serialize(d, real_buf, sizeof(real_buf));

    TEST_ASSERT_EQUAL(needed2, needed1);
}

void test_serialize_all_fields_present()
{
    ConfigData d = IniParser::defaults();
    strcpy(d.wifi.ssid, "W_SSID");
    strcpy(d.wifi.password, "W_PASS");
    strcpy(d.ap.ssid, "A_SSID");
    strcpy(d.ap.password, "A_PASS");
    d.web.port = 1234;
    strcpy(d.web.password, "W_PASS2");
    d.camera.capture_interval_ms = 7777;
    d.video.fps = 20;
    strcpy(d.video.resolution, "QVGA");
    d.video.clip_duration_s = 30;
    d.video.max_file_size_mb = 100;
    d.video.jpeg_quality = 20;

    char buf[1024];
    IniParser::serialize(d, buf, sizeof(buf));

    // Check every key=value pair
    TEST_ASSERT_NOT_NULL(strstr(buf, "[wifi]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "ssid=W_SSID"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "password=W_PASS"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[ap]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "ssid=A_SSID"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "password=A_PASS"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[web]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "port=1234"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "password=W_PASS2"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[camera]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "capture_interval_ms=7777"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "[video]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "fps=20"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "resolution=QVGA"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "clip_duration_s=30"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "max_file_size_mb=100"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "jpeg_quality=20"));
}

// ============================================================
// Test runner
// ============================================================

void setUp()
{
}

void tearDown()
{
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_defaults_has_empty_wifi_ssid);
    RUN_TEST(test_defaults_has_empty_wifi_password);
    RUN_TEST(test_defaults_has_empty_ap_ssid);
    RUN_TEST(test_defaults_has_empty_ap_password);
    RUN_TEST(test_defaults_has_web_port_80);
    RUN_TEST(test_defaults_has_empty_web_password);
    RUN_TEST(test_defaults_has_camera_interval_20000);
    RUN_TEST(test_defaults_has_video_fps_10);
    RUN_TEST(test_defaults_has_video_resolution_vga);
    RUN_TEST(test_defaults_has_video_clip_duration_60);
    RUN_TEST(test_defaults_has_video_max_file_size_0);
    RUN_TEST(test_defaults_has_video_jpeg_quality_10);

    RUN_TEST(test_empty_input_returns_defaults);
    RUN_TEST(test_null_input_returns_defaults);

    RUN_TEST(test_single_wifi_ssid);
    RUN_TEST(test_single_wifi_password);

    RUN_TEST(test_multiple_sections);

    RUN_TEST(test_comments_ignored);
    RUN_TEST(test_blank_lines_ignored);

    RUN_TEST(test_whitespace_around_key);
    RUN_TEST(test_whitespace_around_value);
    RUN_TEST(test_whitespace_around_section_header);

    RUN_TEST(test_unknown_keys_ignored);
    RUN_TEST(test_unknown_sections_ignored);

    RUN_TEST(test_missing_keys_retain_defaults);

    RUN_TEST(test_malformed_line_without_equals_skipped);

    RUN_TEST(test_no_trailing_newline);
    RUN_TEST(test_section_header_only);
    RUN_TEST(test_value_contains_equals);
    RUN_TEST(test_long_value_truncated_to_field_size);
    RUN_TEST(test_case_insensitive_section_names);
    RUN_TEST(test_case_insensitive_key_names);
    RUN_TEST(test_port_out_of_range_ignored);
    RUN_TEST(test_camera_interval_zero);

    // Video section parsing
    RUN_TEST(test_video_all_keys);
    RUN_TEST(test_video_partial_keys_retain_defaults);
    RUN_TEST(test_video_fps_out_of_range_ignored);
    RUN_TEST(test_video_fps_zero_ignored);
    RUN_TEST(test_video_fps_minimum_accepted);
    RUN_TEST(test_video_fps_maximum_accepted);
    RUN_TEST(test_video_jpeg_quality_out_of_range_ignored);

    // Serialization
    RUN_TEST(test_serialize_defaults);
    RUN_TEST(test_serialize_non_default_values);
    RUN_TEST(test_roundtrip_defaults);
    RUN_TEST(test_roundtrip_custom_values);
    RUN_TEST(test_serialize_buffer_too_small_returns_bytes_needed);
    RUN_TEST(test_serialize_null_buffer_returns_bytes_needed);
    RUN_TEST(test_serialize_all_fields_present);

    return UNITY_END();
}
