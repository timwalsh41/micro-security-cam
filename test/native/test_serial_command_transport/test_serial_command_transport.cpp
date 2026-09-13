#include <unity.h>
#include "serial_command_transport.h"
#include "command_handler.h"
#include "mock_log_backend.h"
#include "mock_serial_hal.h"
#include "time_service.h"
#include "logging_service.h"
#include <cstring>
#include <cstdint>
#include <map>
#include <string>

// ============================================================
// TestFixture
// ============================================================

struct TestFixture
{
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    CommandHandler handler;
    MockSerialHal serial;
    SerialCommandTransport transport;
    std::map<std::string, std::string> config;

    TestFixture()
        : logger(log_backend, time)
        , handler(logger)
        , transport(serial, time)
    {
        set_mock_millis(0);

        // Register handler for STATUS/GET_STATUS
        handler.register_handler(
            CommandCode::GET_STATUS,
            [](const uint8_t*, size_t, uint8_t* response, size_t& responseLen) -> CommandResult
            {
                // Mirrors handle_get_status: writes status info as response string.
                const char* msg = "OK";
                std::memcpy(response, msg, 2);
                responseLen = 2;
                return CommandResult::SUCCESS;
            });

        // Register handler for TAKE_PHOTO
        handler.register_handler(
            CommandCode::TAKE_PHOTO,
            [](const uint8_t*, size_t, uint8_t*, size_t&) -> CommandResult
            {
                // Mirrors handle_take_photo: always returns success (no HW in test)
                return CommandResult::SUCCESS;
            });

        // Register handler for SET_TIME
        handler.register_handler(
            CommandCode::SET_TIME,
            [](const uint8_t* payload, size_t len, uint8_t*, size_t&) -> CommandResult
            {
                // Mirrors handle_set_time: checks for 4-byte timestamp payload
                if (len < sizeof(uint32_t))
                {
                    return CommandResult::INVALID_ARGS;
                }
                return CommandResult::SUCCESS;
            });

        // Register handler for SET_CONFIG_VALUE
        handler.register_handler(
            CommandCode::SET_CONFIG_VALUE,
            [this](const uint8_t* payload, size_t len,
                    uint8_t*, size_t&) -> CommandResult
            {
                // Mirrors handle_set_config_value: uses memchr bounded by len
                // to find '=', then treats the value as a C string.
                if (payload == nullptr || len == 0)
                {
                    return CommandResult::INVALID_ARGS;
                }
                const char* data = reinterpret_cast<const char*>(payload);
                const char* eq = static_cast<const char*>(
                    memchr(data, '=', len));
                if (eq == nullptr || eq == data)
                {
                    return CommandResult::INVALID_ARGS;
                }
                std::string key(data, static_cast<size_t>(eq - data));
                const char* val = eq + 1;
                this->config[key] = val;
                return CommandResult::SUCCESS;
            });

        // Register handler for GET_CONFIG_VALUE
        handler.register_handler(
            CommandCode::GET_CONFIG_VALUE,
            [this](const uint8_t* payload, size_t /*len*/,
                    uint8_t* response, size_t& responseLen) -> CommandResult
            {
                // Mirrors handle_get_config_value: ignores len, treats
                // payload as a null-terminated C string.
                if (payload == nullptr)
                {
                    return CommandResult::INVALID_ARGS;
                }
                const char* key = reinterpret_cast<const char*>(payload);
                auto it = this->config.find(key);
                if (it == this->config.end())
                {
                    return CommandResult::INVALID_ARGS;
                }
                const std::string& value = it->second;
                std::memcpy(response, value.c_str(), value.size());
                responseLen = value.size();
                return CommandResult::SUCCESS;
            });

        handler.add_transport(transport);
    }

    void inject_line(const char* line)
    {
        serial.inject_input(line);
    }
};

// ============================================================
// Tests
// ============================================================

/**
 * @brief No serial data available returns no request.
 */
void test_no_data_no_request()
{
    TestFixture f;
    TEST_ASSERT_FALSE(f.transport.has_request());
}

/**
 * @brief Partial line without newline does not produce a request.
 */
void test_partial_line_no_request()
{
    TestFixture f;
    f.inject_line("STATUS");
    TEST_ASSERT_FALSE(f.transport.has_request());
}

/**
 * @brief Complete line after newline produces a request.
 */
void test_newline_triggers_request()
{
    TestFixture f;
    f.inject_line("STATUS\n");
    TEST_ASSERT_TRUE(f.transport.has_request());
}

/**
 * @brief STATUS command is parsed as GET_STATUS.
 */
void test_status_command()
{
    TestFixture f;
    f.inject_line("STATUS\n");

    TEST_ASSERT_TRUE(f.transport.has_request());

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::GET_STATUS),
        static_cast<uint8_t>(code));
    TEST_ASSERT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT32(0, payload_len);
}

/**
 * @brief PHOTO command is parsed as TAKE_PHOTO.
 */
void test_photo_command()
{
    TestFixture f;
    f.inject_line("PHOTO\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::TAKE_PHOTO),
        static_cast<uint8_t>(code));
    TEST_ASSERT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT32(0, payload_len);
}

/**
 * @brief RECORD_START command is parsed as START_RECORDING.
 */
void test_record_start_command()
{
    TestFixture f;
    f.inject_line("RECORD_START\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::START_RECORDING),
        static_cast<uint8_t>(code));
}

/**
 * @brief RECORD_STOP command is parsed as STOP_RECORDING.
 */
void test_record_stop_command()
{
    TestFixture f;
    f.inject_line("RECORD_STOP\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::STOP_RECORDING),
        static_cast<uint8_t>(code));
}

/**
 * @brief TIME command with decimal timestamp produces SET_TIME with
 *        4-byte little-endian payload.
 */
void test_time_command()
{
    TestFixture f;
    f.inject_line("TIME 1234567890\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::SET_TIME),
        static_cast<uint8_t>(code));
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT32(4, payload_len);

    // Verify little-endian: 1234567890 = 0x499602D2
    TEST_ASSERT_EQUAL_UINT8(0xD2, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x96, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0x49, payload[3]);
}

/**
 * @brief GET command with section.key produces GET_CONFIG_VALUE
 *        with the key string as payload.
 */
void test_get_config_value_command()
{
    TestFixture f;
    f.inject_line("GET wifi.ssid\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::GET_CONFIG_VALUE),
        static_cast<uint8_t>(code));
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT32(9, payload_len);
    TEST_ASSERT_EQUAL_MEMORY("wifi.ssid", payload, payload_len);
}

/**
 * @brief SET command with key=value produces SET_CONFIG_VALUE
 *        with the full assignment string as payload.
 */
void test_set_config_value_command()
{
    TestFixture f;
    f.inject_line("SET wifi.ssid=MyNetwork\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::SET_CONFIG_VALUE),
        static_cast<uint8_t>(code));
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT32(19, payload_len);
    TEST_ASSERT_EQUAL_MEMORY("wifi.ssid=MyNetwork", payload, payload_len);
}

/**
 * @brief Unknown text command produces an unrecognized code (0).
 */
void test_unknown_command()
{
    TestFixture f;
    f.inject_line("BOGUS\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(code));
}

/**
 * @hline Empty line (just newline) does not produce a request.
 */
void test_empty_line_skipped()
{
    TestFixture f;
    f.inject_line("\n");

    TEST_ASSERT_FALSE(f.transport.has_request());
}

/**
 * @brief Success response with data sends "OK <data>\n".
 */
void test_response_ok_with_data()
{
    TestFixture f;
    f.serial.clear_buffers();

    const uint8_t data[] = "value";
    f.transport.send_response(CommandResult::SUCCESS, data, 5);

    const char* output = f.serial.output_as_string();
    TEST_ASSERT_EQUAL_STRING("OK value\n", output);
}

/**
 * @brief Success response without data sends "OK\n".
 */
void test_response_ok_no_data()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.transport.send_response(CommandResult::SUCCESS, nullptr, 0);

    const char* output = f.serial.output_as_string();
    TEST_ASSERT_EQUAL_STRING("OK\n", output);
}

/**
 * @brief Data responses up to MAX_RESPONSE_SIZE bytes are sent intact.
 *
 * GET_CONFIG returns a full serialized config (up to
 * CommandHandler::MAX_RESPONSE_SIZE bytes); the wire output must
 * carry the whole payload, not just the first line-buffer's worth.
 */
void test_response_ok_large_data_not_truncated()
{
    TestFixture f;
    f.serial.clear_buffers();

    uint8_t data[CommandHandler::MAX_RESPONSE_SIZE];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = static_cast<uint8_t>('a' + (i % 26));
    }

    f.transport.send_response(CommandResult::SUCCESS, data, sizeof(data));

    std::string expected = "OK ";
    expected.append(reinterpret_cast<const char*>(data), sizeof(data));
    expected += "\n";

    TEST_ASSERT_EQUAL(expected.size(), f.serial.output_buffer.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        reinterpret_cast<const uint8_t*>(expected.data()),
        f.serial.output_buffer.data(), expected.size());
}

/**
 * @brief Error response sends "ERR <code>\n".
 */
void test_response_error()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.transport.send_response(CommandResult::INVALID_ARGS, nullptr, 0);

    const char* output = f.serial.output_as_string();
    TEST_ASSERT_EQUAL_STRING("ERR 02\n", output);
}

/**
 * @brief SD_UNAVAILABLE renders as "ERR 04\n" on the serial wire.
 */
void test_response_sd_unavailable()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.transport.send_response(CommandResult::SD_UNAVAILABLE, nullptr, 0);

    const char* output = f.serial.output_as_string();
    TEST_ASSERT_EQUAL_STRING("ERR 04\n", output);
}

/**
 * @brief Serial CONFIG_GET dispatches GET_CONFIG and emits the full config.
 *
 * The serial interface is unaffected by the web transport's
 * blank-password refusal of config commands.
 */
void test_config_get_serial_unaffected()
{
    TestFixture f;
    f.handler.register_handler(
        CommandCode::GET_CONFIG,
        [](const uint8_t*, size_t, uint8_t* response,
           size_t& responseLen) -> CommandResult
        {
            const char* ini = "[camera]\nframesize=10\n";
            responseLen = strlen(ini);
            std::memcpy(response, ini, responseLen);
            return CommandResult::SUCCESS;
        });

    f.serial.clear_buffers();
    f.inject_line("CONFIG_GET\n");
    f.handler.tick();

    const char* output = f.serial.output_as_string();
    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] CONFIG_GET\r\nOK [camera]\nframesize=10\n\n",
        output);
}

/**
 * @brief Serial CONFIG_SAVE dispatches SET_CONFIG with the destination
 *        payload and succeeds without any password gate.
 */
void test_config_save_serial_unaffected()
{
    TestFixture f;
    f.handler.register_handler(
        CommandCode::SET_CONFIG,
        [](const uint8_t* payload, size_t len,
           uint8_t*, size_t&) -> CommandResult
        {
            if (payload == nullptr || len != 3)
            {
                return CommandResult::INVALID_ARGS;
            }
            return std::memcmp(payload, "nvs", 3) == 0
                       ? CommandResult::SUCCESS
                       : CommandResult::INVALID_ARGS;
        });

    f.serial.clear_buffers();
    f.inject_line("CONFIG_SAVE nvs\n");
    f.handler.tick();

    const char* output = f.serial.output_as_string();
    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] CONFIG_SAVE nvs\r\nOK\n",
        output);
}

/**
 * @brief tick() drives the full cycle: read request → dispatch → send response.
 */
void test_tick_full_cycle()
{
    TestFixture f;
    f.serial.clear_buffers();
    f.inject_line("STATUS\n");

    f.handler.tick();

    const char* output = f.serial.output_as_string();
    TEST_ASSERT_EQUAL_STRING("[00000000] [ECHO] [INFO ] STATUS\r\nOK OK\n", output);
}

/**
 * @hline Multiple commands in sequence are processed correctly.
 */
void test_multiple_commands_sequence()
{
    TestFixture f;
    f.serial.clear_buffers();
    f.inject_line("PHOTO\nSTATUS\n");

    // First command: PHOTO
    f.handler.tick();

    // Second command: STATUS
    f.handler.tick();

    const char* output = f.serial.output_as_string();
    // PHOTO returns "OK\n", STATUS returns "OK OK\n" (data "OK");
    // each command line is echoed before its response.
    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] PHOTO\r\nOK\n"
        "[00000000] [ECHO] [INFO ] STATUS\r\nOK OK\n",
        output);
}

/**
 * @brief Leading spaces in a command line are ignored.
 */
void test_leading_spaces()
{
    TestFixture f;
    f.inject_line("  STATUS\n");
    f.transport.has_request();

    CommandCode code;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    f.transport.read_request(code, payload, payload_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandCode::GET_STATUS),
        static_cast<uint8_t>(code));
}

/**
 * @brief SET a config value, then GET it through the full tick cycle.
 *        Tests that state set by SET_CONFIG_VALUE persists and is
 *        retrievable via GET_CONFIG_VALUE.
 */
void test_set_then_get_config_value()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.serial.inject_input("SET wifi.ssid=test\n");
    f.handler.tick();
    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] SET wifi.ssid=test\r\nOK\n",
        f.serial.output_as_string());

    f.serial.clear_buffers();

    f.serial.inject_input("GET wifi.ssid\n");
    f.handler.tick();

    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] GET wifi.ssid\r\nOK test\n",
        f.serial.output_as_string());
}

/**
 * @brief Echo is not emitted until the line terminator is received.
 */
void test_echo_deferred_until_newline()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.inject_line("STATUS");
    TEST_ASSERT_FALSE(f.transport.has_request());
    TEST_ASSERT_EQUAL_UINT8(0, f.serial.output_buffer.size());

    f.inject_line("\n");
    TEST_ASSERT_TRUE(f.transport.has_request());
    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] STATUS\r\n",
        f.serial.output_as_string());
}

/**
 * @brief The complete line is echoed as one continuous batch including
 *        timestamp, subsystem, severity, and CRLF terminator.
 */
void test_echo_full_log_line_format()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.inject_line("STATUS\n");
    f.transport.has_request();

    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] STATUS\r\n",
        f.serial.output_as_string());
}

/**
 * @brief Echo uses the current mock time as its timestamp.
 */
void test_echo_timestamp_from_time_service()
{
    TestFixture f;
    set_mock_millis(1234);
    f.serial.clear_buffers();

    f.inject_line("PHOTO\n");
    f.transport.has_request();

    TEST_ASSERT_EQUAL_STRING(
        "[00001234] [ECHO] [INFO ] PHOTO\r\n",
        f.serial.output_as_string());
}

/**
 * @brief Echo does not re-emit the line terminator itself.
 */
void test_echo_crlf_input_single_echo()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.inject_line("STATUS\r\n");
    f.transport.has_request();

    TEST_ASSERT_EQUAL_STRING(
        "[00000000] [ECHO] [INFO ] STATUS\r\n",
        f.serial.output_as_string());
}

/**
 * @brief Disabling echo suppresses the input line entirely while still
 *        producing the command response.
 */
void test_echo_disabled()
{
    TestFixture f;
    f.serial.clear_buffers();
    f.transport.set_echo_enabled(false);

    f.inject_line("STATUS\n");
    f.handler.tick();

    TEST_ASSERT_EQUAL_STRING("OK OK\n", f.serial.output_as_string());
}

/**
 * @brief Blank lines are not echoed (and produce no request).
 */
void test_echo_blank_line_silent()
{
    TestFixture f;
    f.serial.clear_buffers();

    f.inject_line("\n");
    TEST_ASSERT_FALSE(f.transport.has_request());
    TEST_ASSERT_EQUAL_UINT8(0, f.serial.output_buffer.size());
}

// ============================================================
// Test runner
// ============================================================

void setUp() {}
void tearDown() {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_no_data_no_request);
    RUN_TEST(test_partial_line_no_request);
    RUN_TEST(test_newline_triggers_request);
    RUN_TEST(test_status_command);
    RUN_TEST(test_photo_command);
    RUN_TEST(test_record_start_command);
    RUN_TEST(test_record_stop_command);
    RUN_TEST(test_time_command);
    RUN_TEST(test_get_config_value_command);
    RUN_TEST(test_set_config_value_command);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_empty_line_skipped);
    RUN_TEST(test_response_ok_with_data);
    RUN_TEST(test_response_ok_no_data);
    RUN_TEST(test_response_ok_large_data_not_truncated);
    RUN_TEST(test_response_error);
    RUN_TEST(test_response_sd_unavailable);
    RUN_TEST(test_config_get_serial_unaffected);
    RUN_TEST(test_config_save_serial_unaffected);
    RUN_TEST(test_tick_full_cycle);
    RUN_TEST(test_multiple_commands_sequence);
    RUN_TEST(test_leading_spaces);
    RUN_TEST(test_set_then_get_config_value);
    RUN_TEST(test_echo_deferred_until_newline);
    RUN_TEST(test_echo_full_log_line_format);
    RUN_TEST(test_echo_timestamp_from_time_service);
    RUN_TEST(test_echo_crlf_input_single_echo);
    RUN_TEST(test_echo_disabled);
    RUN_TEST(test_echo_blank_line_silent);

    return UNITY_END();
}
