#ifndef MOCK_WEB_SERVER_HAL_H
#define MOCK_WEB_SERVER_HAL_H

#include "hal/web_server_hal.h"
#include <cstring>
#include <vector>
#include <string>

/**
 * @file mock_web_server_hal.h
 * @brief Configurable mock WebServerHal for native unit tests.
 *
 * Captures route registrations and simulates request/response
 * for testing route handlers without an actual HTTP server.
 */
class MockWebServerHal : public hal::WebServerHal
{
    public:
        // ============================================================
        // Configurable return values
        // ============================================================

        /** Value returned by begin(). */
        bool begin_result{true};

        /** Simulated request URI. */
        char request_uri[128]{"/"};

        /** Simulated request method. */
        hal::HttpMethod request_method{hal::HttpMethod::GET};

        /** Simulated query parameter storage. */
        char arg_buffer[128]{};
        char arg_name_buf[64]{};

        /** Simulated header value storage. */
        char header_buffer[128]{};
        char header_name_buf[64]{};

        /** Simulated Basic Auth header. */
        char auth_buffer[128]{};

        // ============================================================
        // Call tracking
        // ============================================================

        int begin_call_count{0};
        int handle_client_call_count{0};
        int stop_call_count{0};

        struct RouteRegistration
        {
            std::string uri;
            hal::HttpMethod method;
        };
        std::vector<RouteRegistration> registered_routes;

        struct ResponseRecord
        {
            int code{0};
            std::string content_type;
            std::string body;
        };
        std::vector<ResponseRecord> responses;

        /** Response code to return from send(). */
        int last_send_code{200};
        std::string last_send_content;

        // ============================================================
        // Interface implementation
        // ============================================================

        bool begin(uint16_t /*port*/) override
        {
            begin_call_count++;
            return begin_result;
        }

        void on(const char* uri, hal::HttpMethod method,
                void (* /*handler*/)()) override
        {
            registered_routes.push_back({uri, method});
        }

        void handle_client() override
        {
            handle_client_call_count++;
        }

        void stop() override
        {
            stop_call_count++;
        }

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
            // Simple mock: return stored arg if name matches
            if (strcmp(name, arg_name_buf) == 0)
            {
                return arg_buffer;
            }
            return "";
        }

        const char* header(const char* name) const override
        {
            if (strcmp(name, header_name_buf) == 0)
            {
                return header_buffer;
            }
            return "";
        }

        const char* auth_header() const override
        {
            return auth_buffer;
        }

        void send(int code, const char* content_type,
                  const char* content) override
        {
            last_send_code = code;
            last_send_content = content ? content : "";
            ResponseRecord rec;
            rec.code = code;
            rec.content_type = content_type ? content_type : "";
            rec.body = content ? content : "";
            responses.push_back(rec);
        }

        void send(int code, const char* content_type,
                  const uint8_t* data, size_t len) override
        {
            last_send_code = code;
            last_send_content = std::string(reinterpret_cast<const char*>(data), len);
            ResponseRecord rec;
            rec.code = code;
            rec.content_type = content_type ? content_type : "";
            rec.body = last_send_content;
            responses.push_back(rec);
        }

        void send_header(int /*code*/, const char* /*content_type*/,
                         size_t /*content_length*/) override
        {
            // no-op for mock
        }

        void send_content(const uint8_t* data, size_t len) override
        {
            last_send_content = std::string(reinterpret_cast<const char*>(data), len);
        }

        // ============================================================
        // Helper methods for test setup
        // ============================================================

        /** Configure a simulated query argument for the next arg() call. */
        void set_arg(const char* name, const char* value)
        {
            strncpy(arg_name_buf, name, sizeof(arg_name_buf) - 1);
            strncpy(arg_buffer, value, sizeof(arg_buffer) - 1);
        }

        /** Configure a simulated header for the next header() call. */
        void set_header(const char* name, const char* value)
        {
            strncpy(header_name_buf, name, sizeof(header_name_buf) - 1);
            strncpy(header_buffer, value, sizeof(header_buffer) - 1);
        }
};

#endif // MOCK_WEB_SERVER_HAL_H
