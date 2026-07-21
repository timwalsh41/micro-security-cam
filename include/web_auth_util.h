#ifndef WEB_AUTH_UTIL_H
#define WEB_AUTH_UTIL_H

/**
 * @file web_auth_util.h
 * @brief Shared HTTP Basic Auth validation utilities.
 *
 * Provides a single implementation of Base64-decoded Basic Auth
 * verification used by both WebServerService and WebCommandTransport.
 */

/**
 * @brief Validates HTTP Basic Auth credentials against a password.
 *
 * Expects @p auth_header in the form `"Basic <base64(user:password)>"`.
 * Decodes the Base64 payload and compares the password portion after
 * the `:` separator against @p password.
 *
 * @param auth_header  Full Authorization header value (with "Basic " prefix).
 *                     Pass nullptr or empty string for unauthenticated requests.
 * @param password     Expected password. Pass empty string to allow all requests
 *                     (no password configured).
 * @return true if credentials match or no password is configured.
 */
bool check_basic_auth(const char* auth_header, const char* password);

#endif // WEB_AUTH_UTIL_H
