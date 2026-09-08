#ifndef MOCK_WEB_SERVER_HAL_H
#define MOCK_WEB_SERVER_HAL_H

#include "hal/web_server_hal.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <functional>

/**
 * @file mock_web_server_hal.h
 * @brief Configurable mock WebServerHal and RequestRef for native unit tests.
 *
 * Captures route registrations (with handlers) and simulates request
 * dispatch via simulate_request(). Tests configure the mock request
 * state on MockWebServerHal::mock_request before calling simulate_request(),
 * then inspect mock_request.responses for the handler output.
 */

/**
 * @class MockRequestRef
 * @brief RequestRef implementation for native tests.
 *
 * All request state (URI, method, args, headers, auth) is configurable
 * via public members and set_* helpers. Response data is captured in
 * the responses vector for test assertions.
 */
class MockRequestRef : public hal::RequestRef
{
    public:
        // ============================================================
        // Configurable request state
        // ============================================================

        /** Simulated request URI. */
        char request_uri[128]{"/"};

        /** Simulated request method. */
        hal::HttpMethod request_method{hal::HttpMethod::GET};

        /** Simulated query parameters (name → value). */
        static constexpr size_t MAX_ARGS = 4;

        struct ArgEntry
        {
            char name[64]{};
            char value[128]{};
        };
        ArgEntry args[MAX_ARGS]{};
        size_t arg_count{0};

        /** Simulated header (name → value). */
        char header_name[64]{};
        char header_value[128]{};

        /** Simulated Basic Auth header value (with "Basic " prefix). */
        char auth_value[128]{};

        // ============================================================
        // Response tracking
        // ============================================================

        /** Record of a send() call made by the handler. */
        struct ResponseRecord
        {
            int code{0};
            std::string content_type;
            std::string body;
        };
        std::vector<ResponseRecord> responses;

        /** Record of an attached streaming response. */
        struct StreamResponseRecord
        {
            std::shared_ptr<StreamFrameChannel> channel;
            std::function<void()> on_end;
        };
        std::vector<StreamResponseRecord> stream_responses;

        /** Value returned by send_stream_response(). */
        bool send_stream_result{true};

        // ============================================================
        // Interface implementation
        // ============================================================

        const char* uri() const override
        {
            return request_uri;
        }

        hal::HttpMethod method() const override
        {
            return request_method;
        }

        const char* arg(const char* name) const override
        {
            if (name != nullptr)
            {
                for (size_t i = 0; i < arg_count; i++)
                {
                    if (strcmp(name, args[i].name) == 0)
                    {
                        return args[i].value;
                    }
                }
            }
            return "";
        }

        const char* header(const char* name) const override
        {
            if (name != nullptr && strcmp(name, header_name) == 0)
            {
                return header_value;
            }
            return "";
        }

        const char* auth_header() const override
        {
            return auth_value;
        }

        void send(int code, const char* content_type,
                  const char* content) override
        {
            ResponseRecord rec;
            rec.code = code;
            rec.content_type = content_type ? content_type : "";
            rec.body = content ? content : "";
            responses.push_back(rec);
        }

        void send(int code, const char* content_type,
                  const uint8_t* data, size_t len) override
        {
            ResponseRecord rec;
            rec.code = code;
            rec.content_type = content_type ? content_type : "";
            rec.body = std::string(reinterpret_cast<const char*>(data), len);
            responses.push_back(rec);
        }

        void send_unauthorized() override
        {
            ResponseRecord rec;
            rec.code = 401;
            rec.content_type = "application/json";
            rec.body = "{\"error\":\"authentication required\"}";
            responses.push_back(rec);
        }

        bool send_stream_response(
            std::shared_ptr<StreamFrameChannel> channel,
            const char* boundary,
            std::function<void()> on_end) override
        {
            if (!send_stream_result || !channel || boundary == nullptr)
            {
                return false;
            }
            StreamResponseRecord rec;
            rec.channel = channel;
            rec.on_end = std::move(on_end);
            stream_responses.push_back(std::move(rec));
            return true;
        }

        /**
         * @brief Simulates the transport ending a streaming response
         *        (client disconnect or completed stream).
         *
         * Invokes the recorded end callback, which frees the viewer
         * slot, exactly as the transport's response destruction would.
         */
        void end_stream_response(size_t index)
        {
            if (index < stream_responses.size() &&
                stream_responses[index].on_end)
            {
                stream_responses[index].on_end();
            }
        }

        // ============================================================
        // Helper methods for test setup
        // ============================================================
        void set_arg(const char* name, const char* value)
        {
            if (name == nullptr || arg_count >= MAX_ARGS)
            {
                return;
            }
            strncpy(args[arg_count].name, name,
                    sizeof(args[arg_count].name) - 1);
            args[arg_count].name[sizeof(args[arg_count].name) - 1] = '\0';
            strncpy(args[arg_count].value, value,
                    sizeof(args[arg_count].value) - 1);
            args[arg_count].value[sizeof(args[arg_count].value) - 1] = '\0';
            arg_count++;
        }

        /** Configure a simulated header. */
        void set_header(const char* name, const char* value)
        {
            strncpy(header_name, name, sizeof(header_name) - 1);
            header_name[sizeof(header_name) - 1] = '\0';
            strncpy(header_value, value, sizeof(header_value) - 1);
            header_value[sizeof(header_value) - 1] = '\0';
        }

        /** Configure a simulated Basic Auth header from user:password. */
        void set_auth(const char* user, const char* password)
        {
            char raw[256];
            snprintf(raw, sizeof(raw), "%s:%s", user, password);

            static const char* TABLE =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789+/";

            size_t len = strlen(raw);
            size_t out = 0;
            auth_value[out++] = 'B';
            auth_value[out++] = 'a';
            auth_value[out++] = 's';
            auth_value[out++] = 'i';
            auth_value[out++] = 'c';
            auth_value[out++] = ' ';

            unsigned int accum = 0;
            int bits = 0;
            for (size_t i = 0; i < len && out < sizeof(auth_value) - 1; i++)
            {
                accum = (accum << 8) | static_cast<unsigned char>(raw[i]);
                bits += 8;
                while (bits >= 6 && out < sizeof(auth_value) - 1)
                {
                    bits -= 6;
                    auth_value[out++] =
                        TABLE[(accum >> bits) & 0x3F];
                }
            }
            if (bits > 0 && out < sizeof(auth_value) - 1)
            {
                auth_value[out++] =
                    TABLE[(accum << (6 - bits)) & 0x3F];
            }
            while (out % 4 != 0 && out < sizeof(auth_value) - 1)
            {
                auth_value[out++] = '=';
            }
            auth_value[out] = '\0';
        }
};

/**
 * @class MockWebServerHal
 * @brief Configurable mock WebServerHal for native unit tests.
 *
 * Stores route registrations with their std::function handlers.
 * Tests configure mock_request state, then call simulate_request()
 * to invoke a route's handler with that mock request.
 */
class MockWebServerHal : public hal::WebServerHal
{
    public:
        // ============================================================
        // Configurable return values
        // ============================================================

        /** Value returned by init(). */
        bool init_result{true};

        // ============================================================
        // Call tracking
        // ============================================================

        int init_call_count{0};
        int stop_call_count{0};

        /** Record of a route registration. */
        struct RouteRecord
        {
            std::string uri;
            hal::HttpMethod method;
            std::function<void(hal::RequestRef&)> handler;
        };
        std::vector<RouteRecord> registered_routes;

        /**
         * @brief Mock request object shared with simulate_request().
         *
         * Configure state on this object before calling simulate_request().
         * After the call, inspect mock_request.responses for handler output.
         */
        MockRequestRef mock_request;

        // ============================================================
        // Interface implementation
        // ============================================================

        bool init(uint16_t /*port*/) override
        {
            init_call_count++;
            return init_result;
        }

        void on(const char* uri, hal::HttpMethod method,
                std::function<void(hal::RequestRef&)> handler) override
        {
            registered_routes.push_back({uri, method, std::move(handler)});
        }

        void stop() override
        {
            stop_call_count++;
        }

        // ============================================================
        // Simulation helper
        // ============================================================

        /**
         * @brief Simulates a request matching the given URI and method.
         *
         * Finds the registered handler and invokes it with mock_request.
         * The test should configure mock_request before calling this.
         *
         * @param uri     Request URI to match.
         * @param method  HTTP method to match.
         * @return true if a handler was found and invoked.
         */
        bool simulate_request(const char* uri, hal::HttpMethod method)
        {
            // Clear responses from any previous simulation
            mock_request.responses.clear();
            mock_request.stream_responses.clear();

            for (auto& route : registered_routes)
            {
                if (route.uri == uri && route.method == method)
                {
                    route.handler(mock_request);
                    return true;
                }
            }
            return false;
        }
};

#endif // MOCK_WEB_SERVER_HAL_H
