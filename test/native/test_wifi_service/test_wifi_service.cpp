#include <unity.h>
#include "wifi_service.h"
#include "mock_wifi_hal.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstring>
#include <string>

/**
 * @file test_wifi_service.cpp
 * @brief Native unit tests for WifiService.
 *
 * Exercises STA initialization, timeout fallback to AP mode,
 * quick-connect on second poll, and is_connected() behaviour.
 *
 * The native hal::delay_ms() delegates to mock Arduino delay(),
 * which increments mock_millis instantly — no real-time waits.
 */

// ============================================================
// Test fixture
// ============================================================

struct TestFixture
{
    MockWifiHal wifi_hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    WifiService wifi;

    TestFixture()
        : logger(log_backend, time)
        , wifi(wifi_hal, time, logger)
    {
    }
};

// ============================================================
// Helpers
// ============================================================

static ConfigData::WifiConfig make_sta_config(const char* ssid,
                                              const char* password)
{
    ConfigData::WifiConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, ssid, CONFIG_FIELD_MAX_LENGTH - 1);
    strncpy(cfg.password, password, CONFIG_FIELD_MAX_LENGTH - 1);
    return cfg;
}

static ConfigData::ApConfig make_ap_config(const char* ssid,
                                           const char* password)
{
    ConfigData::ApConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, ssid, CONFIG_FIELD_MAX_LENGTH - 1);
    strncpy(cfg.password, password, CONFIG_FIELD_MAX_LENGTH - 1);
    return cfg;
}

static bool any_log_contains(MockLogBackend& backend,
                             const std::string& needle)
{
    for (const auto& msg : backend.messages)
    {
        if (msg.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// ============================================================
// setUp / tearDown
// ============================================================

void setUp()
{
    // Nothing global to reset; each test uses its own TestFixture.
}

void tearDown()
{
}

// ============================================================
// test_init_sta_success
//
// Mock WiFi connects on the first poll.  Verify is_connected()
// returns true, local_ip() returns the mock IP, and begin_ap()
// was never called.
// ============================================================

void test_init_sta_success()
{
    TestFixture f;

    f.wifi_hal.status_result = hal::WifiStatus::CONNECTED;
    f.wifi_hal.connected_result = true;
    strcpy(f.wifi_hal.ip_buffer, "10.0.0.42");

    ConfigData::WifiConfig sta = make_sta_config("HomeNet", "secret123");
    ConfigData::ApConfig ap = make_ap_config("ESP32-Cam", "ap12345");

    f.wifi.init(sta, ap, 30);

    TEST_ASSERT_TRUE(f.wifi.is_connected());
    TEST_ASSERT_EQUAL_STRING("10.0.0.42", f.wifi.local_ip());
    TEST_ASSERT_EQUAL_INT(1, f.wifi_hal.begin_sta_call_count);
    TEST_ASSERT_EQUAL_INT(0, f.wifi_hal.begin_ap_call_count);
    TEST_ASSERT_EQUAL_INT(1, f.wifi_hal.mode_call_count);
    TEST_ASSERT_EQUAL_STRING("HomeNet", f.wifi_hal.last_ssid);
    TEST_ASSERT_TRUE(any_log_contains(f.log_backend, "connected"));
}

// ============================================================
// test_init_sta_timeout_falls_back_to_ap
//
// Mock WiFi never connects (status always DISCONNECTED).
// After timeout_secs, WifiService falls back to AP mode.
// is_connected() should remain false.
// ============================================================

void test_init_sta_timeout_falls_back_to_ap()
{
    TestFixture f;

    f.wifi_hal.status_result = hal::WifiStatus::DISCONNECTED;
    f.wifi_hal.connected_result = false;
    strcpy(f.wifi_hal.ip_buffer, "192.168.4.1");

    ConfigData::WifiConfig sta = make_sta_config("HomeNet", "secret123");
    ConfigData::ApConfig ap = make_ap_config("ESP32-Cam", "ap12345");

    // Short timeout to keep test fast.  The native delay_ms() is
    // non-blocking (mock), so even 2s of simulated time finishes
    // instantly.
    f.wifi.init(sta, ap, 2);

    TEST_ASSERT_FALSE(f.wifi.is_connected());
    TEST_ASSERT_EQUAL_INT(1, f.wifi_hal.begin_ap_call_count);
    TEST_ASSERT_EQUAL_INT(1, f.wifi_hal.begin_sta_call_count);
    TEST_ASSERT_EQUAL_STRING("ESP32-Cam", f.wifi_hal.last_ssid);
    TEST_ASSERT_TRUE(any_log_contains(f.log_backend, "AP started"));
}

// ============================================================
// Quick-connect helper mock
//
// Returns DISCONNECTED on the first status() call, then
// CONNECTED on all subsequent calls.  This lets us verify
// that WifiService polls correctly across multiple iterations.
// ============================================================

class QuickConnectWifiHal : public MockWifiHal
{
    public:
        mutable int m_status_call_count{0};

        hal::WifiStatus status() const override
        {
            m_status_call_count++;
            if (m_status_call_count == 1)
            {
                return hal::WifiStatus::DISCONNECTED;
            }
            return hal::WifiStatus::CONNECTED;
        }
};

// ============================================================
// test_init_sta_quick_connect
//
// WiFi does not connect on the first poll but connects on the
// second.  Verify is_connected() is true after init.
// ============================================================

void test_init_sta_quick_connect()
{
    QuickConnectWifiHal hal;

    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger(log_backend, time);
    WifiService wifi(hal, time, logger);

    strcpy(hal.ip_buffer, "10.0.0.99");

    ConfigData::WifiConfig sta = make_sta_config("QuickNet", "pass123");
    ConfigData::ApConfig ap = make_ap_config("ESP32-Cam", "ap12345");

    wifi.init(sta, ap, 30);

    TEST_ASSERT_TRUE(wifi.is_connected());
    TEST_ASSERT_EQUAL_STRING("10.0.0.99", wifi.local_ip());
    TEST_ASSERT_EQUAL_INT(1, hal.begin_sta_call_count);
    TEST_ASSERT_EQUAL_INT(0, hal.begin_ap_call_count);
    TEST_ASSERT_EQUAL_INT(2, hal.m_status_call_count);
    TEST_ASSERT_TRUE(any_log_contains(log_backend, "connected"));
}

// ============================================================
// test_is_connected_reflects_hal
//
// After a successful STA init, is_connected() returns true.
// After a failed init (timeout), is_connected() returns false.
// ============================================================

void test_is_connected_reflects_hal()
{
    // --- Case 1: successful connection ---
    {
        TestFixture f;

        f.wifi_hal.status_result = hal::WifiStatus::CONNECTED;
        f.wifi_hal.connected_result = true;
        strcpy(f.wifi_hal.ip_buffer, "10.0.0.1");

        ConfigData::WifiConfig sta = make_sta_config("Net", "pw");
        ConfigData::ApConfig ap = make_ap_config("AP", "pw");

        f.wifi.init(sta, ap, 30);

        TEST_ASSERT_TRUE(f.wifi.is_connected());
    }

    // --- Case 2: timeout, no connection ---
    {
        TestFixture f;

        f.wifi_hal.status_result = hal::WifiStatus::DISCONNECTED;
        f.wifi_hal.connected_result = false;
        strcpy(f.wifi_hal.ip_buffer, "192.168.4.1");

        ConfigData::WifiConfig sta = make_sta_config("Net", "pw");
        ConfigData::ApConfig ap = make_ap_config("AP", "pw");

        f.wifi.init(sta, ap, 2);

        TEST_ASSERT_FALSE(f.wifi.is_connected());
    }
}

// ============================================================
// Test runner
// ============================================================

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_sta_success);
    RUN_TEST(test_init_sta_timeout_falls_back_to_ap);
    RUN_TEST(test_init_sta_quick_connect);
    RUN_TEST(test_is_connected_reflects_hal);

    return UNITY_END();
}
