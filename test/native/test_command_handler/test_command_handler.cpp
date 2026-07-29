#include <unity.h>
#include "command_handler.h"
#include "command_transport.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include "logging_service.h"
#include <cstring>
#include <vector>

// ============================================================
// MockCommandTransport
// ============================================================

/**
 * @brief Simple mock transport for unit testing CommandHandler.
 *
 * Allows injecting a pending request and inspecting the
 * response that was sent back.
 */
class MockCommandTransport : public CommandTransport
{
    public:
        bool has_request() override
        {
            return m_has_request;
        }

        void read_request(CommandCode& code,
                          const uint8_t*& payload,
                          size_t& payload_len) override
        {
            code = m_pending_code;
            payload = m_pending_payload;
            payload_len = m_pending_payload_len;
            m_has_request = false;
        }

        void send_response(CommandResult code,
                           const uint8_t* data,
                           size_t len) override
        {
            m_response_code = code;
            if (data && len > 0)
            {
                m_response_data.assign(data, data + len);
            }
            else
            {
                m_response_data.clear();
            }
        }

        void set_request(CommandCode code,
                         const uint8_t* payload = nullptr,
                         size_t len = 0)
        {
            m_pending_code = code;
            m_pending_payload = payload;
            m_pending_payload_len = len;
            m_has_request = true;
        }

        bool m_has_request{false};
        CommandCode m_pending_code{CommandCode::GET_STATUS};
        const uint8_t* m_pending_payload{nullptr};
        size_t m_pending_payload_len{0};
        CommandResult m_response_code{CommandResult::SUCCESS};
        std::vector<uint8_t> m_response_data;
};

// ============================================================
// TestFixture
// ============================================================

struct TestFixture
{
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    CommandHandler handler;
    MockCommandTransport transport;

    TestFixture()
        : logger(log_backend, time)
        , handler(logger)
    {
        handler.add_transport(transport);
    }
};

// ============================================================
// Tests
// ============================================================

/**
 * @brief Dispatch returns UNRECOGNIZED when no handlers are registered.
 */
void test_initial_state_no_handlers()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.handler.dispatch(
        CommandCode::GET_STATUS, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::UNRECOGNIZED),
        static_cast<uint8_t>(result));
}

/**
 * @brief Register a handler and dispatch to it.
 *        Verifies the payload is forwarded and the handler's
 *        result is returned.
 */
void test_register_and_dispatch()
{
    TestFixture f;

    CommandCode received_code = CommandCode::GET_STATUS;
    const uint8_t* received_payload = nullptr;
    size_t received_len = 0;

    f.handler.register_handler(
        CommandCode::GET_STATUS,
        [&](const uint8_t* payload, size_t len,
            uint8_t* response, size_t& responseLen) -> CommandResult
        {
            received_payload = payload;
            received_len = len;
            return CommandResult::SUCCESS;
        });

    uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.handler.dispatch(
        CommandCode::GET_STATUS,
        payload, sizeof(payload),
        response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_PTR(payload, received_payload);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), received_len);
}

/**
 * @brief Dispatching an unregistered code returns UNRECOGNIZED
 *        even when other handlers are registered.
 */
void test_dispatch_unrecognized_code()
{
    TestFixture f;

    f.handler.register_handler(
        CommandCode::GET_STATUS,
        [](const uint8_t*, size_t, uint8_t*, size_t&) -> CommandResult
        {
            return CommandResult::SUCCESS;
        });

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::UNRECOGNIZED),
        static_cast<uint8_t>(result));
}

/**
 * @brief tick() polls the transport, dispatches, and sends
 *        the response back.
 */
void test_tick_polls_transports()
{
    TestFixture f;

    f.handler.register_handler(
        CommandCode::GET_STATUS,
        [](const uint8_t*, size_t, uint8_t* response, size_t& responseLen) -> CommandResult
        {
            response[0] = 0xAA;
            responseLen = 1;
            return CommandResult::SUCCESS;
        });

    // No request pending — tick should do nothing
    f.handler.tick();
    TEST_ASSERT_FALSE(f.transport.m_has_request);

    // Set up a request
    f.transport.set_request(CommandCode::GET_STATUS);
    f.handler.tick();

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(f.transport.m_response_code));
    TEST_ASSERT_EQUAL_UINT32(1, f.transport.m_response_data.size());
    TEST_ASSERT_EQUAL_UINT8(0xAA, f.transport.m_response_data[0]);
}

/**
 * @brief Multiple handlers can be registered and dispatched independently.
 */
void test_multiple_handlers()
{
    TestFixture f;

    bool get_status_called = false;
    bool take_photo_called = false;

    f.handler.register_handler(
        CommandCode::GET_STATUS,
        [&](const uint8_t*, size_t, uint8_t*, size_t&) -> CommandResult
        {
            get_status_called = true;
            return CommandResult::SUCCESS;
        });

    f.handler.register_handler(
        CommandCode::TAKE_PHOTO,
        [&](const uint8_t*, size_t, uint8_t*, size_t&) -> CommandResult
        {
            take_photo_called = true;
            return CommandResult::SUCCESS;
        });

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    f.handler.dispatch(
        CommandCode::GET_STATUS, nullptr, 0, response, response_len);
    TEST_ASSERT_TRUE(get_status_called);
    TEST_ASSERT_FALSE(take_photo_called);

    f.handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);
    TEST_ASSERT_TRUE(take_photo_called);
}

/**
 * @brief Handler writes data into the response buffer and
 *        it is returned correctly through dispatch.
 */
void test_response_buffer()
{
    TestFixture f;

    f.handler.register_handler(
        CommandCode::GET_STATUS,
        [](const uint8_t*, size_t, uint8_t* response, size_t& responseLen) -> CommandResult
        {
            const char* msg = "OK";
            std::memcpy(response, msg, 2);
            responseLen = 2;
            return CommandResult::SUCCESS;
        });

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.handler.dispatch(
        CommandCode::GET_STATUS, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_UINT32(2, response_len);
    TEST_ASSERT_EQUAL_UINT8('O', response[0]);
    TEST_ASSERT_EQUAL_UINT8('K', response[1]);
}

/**
 * @brief Multiple transports registered — each is polled independently
 *        and receives the correct response without cross-talk.
 */
void test_multi_transport_no_interference()
{
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger{log_backend, time};
    CommandHandler handler{logger};

    MockCommandTransport transport_a;
    MockCommandTransport transport_b;

    handler.add_transport(transport_a);
    handler.add_transport(transport_b);

    // Register handlers
    handler.register_handler(
        CommandCode::GET_STATUS,
        [](const uint8_t*, size_t, uint8_t* response, size_t& responseLen) -> CommandResult
        {
            response[0] = 0xAA;
            responseLen = 1;
            return CommandResult::SUCCESS;
        });

    handler.register_handler(
        CommandCode::TAKE_PHOTO,
        [](const uint8_t*, size_t, uint8_t*, size_t&) -> CommandResult
        {
            return CommandResult::SUCCESS;
        });

    // Set requests on both transports
    transport_a.set_request(CommandCode::GET_STATUS);
    transport_b.set_request(CommandCode::TAKE_PHOTO);

    // Single tick should process both
    handler.tick();

    // Verify transport_a got GET_STATUS result (SUCCESS with 0xAA data)
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(transport_a.m_response_code));
    TEST_ASSERT_EQUAL_UINT32(1, transport_a.m_response_data.size());
    TEST_ASSERT_EQUAL_UINT8(0xAA, transport_a.m_response_data[0]);

    // Verify transport_b got TAKE_PHOTO result (SUCCESS, no data)
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(transport_b.m_response_code));
    TEST_ASSERT_TRUE(transport_b.m_response_data.empty());

    // Both transports should have consumed their requests
    TEST_ASSERT_FALSE(transport_a.m_has_request);
    TEST_ASSERT_FALSE(transport_b.m_has_request);
}

// ============================================================
// Test runner
// ============================================================

void setUp() {}
void tearDown() {}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_initial_state_no_handlers);
    RUN_TEST(test_register_and_dispatch);
    RUN_TEST(test_dispatch_unrecognized_code);
    RUN_TEST(test_tick_polls_transports);
    RUN_TEST(test_multiple_handlers);
    RUN_TEST(test_response_buffer);
    RUN_TEST(test_multi_transport_no_interference);

    return UNITY_END();
}
