#include "web_auth_util.h"
#include <cstring>
#include <cstdint>

// ============================================================
// Minimal Base64 decoder for HTTP Basic Auth
// ============================================================
//
// Decodes a Base64 string in-place or into a caller-provided buffer.
// No external library required — uses a simple 64-entry lookup table.

static constexpr char BASE64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_decode(const char* src, size_t src_len,
                         char* dst, size_t dst_size)
{
    // Build reverse lookup: ASCII → 6-bit value (0xFF = invalid)
    uint8_t lut[256];
    memset(lut, 0xFF, sizeof(lut));
    for (int i = 0; i < 64; i++)
    {
        lut[static_cast<uint8_t>(BASE64_TABLE[i])] = static_cast<uint8_t>(i);
    }

    size_t out = 0;
    uint32_t accumulator = 0;
    int bits = 0;

    for (size_t i = 0; i < src_len && out < dst_size; i++)
    {
        char c = src[i];
        if (c == '=' || c == ' ' || c == '\r' || c == '\n')
        {
            break;
        }

        uint8_t val = lut[static_cast<uint8_t>(c)];
        if (val == 0xFF)
        {
            // Invalid character — abort decode
            return -1;
        }

        accumulator = (accumulator << 6) | val;
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            dst[out++] = static_cast<char>((accumulator >> bits) & 0xFF);
        }
    }

    if (out < dst_size)
    {
        dst[out] = '\0';
    }
    return static_cast<int>(out);
}

bool check_basic_auth(const char* auth_header, const char* password)
{
    // No password configured — allow all requests
    if (password == nullptr || password[0] == '\0')
    {
        return true;
    }

    // Expect "Basic " prefix
    if (auth_header == nullptr)
    {
        return false;
    }

    static constexpr const char* BASIC_PREFIX = "Basic ";
    static constexpr size_t PREFIX_LEN = 6;

    if (strncmp(auth_header, BASIC_PREFIX, PREFIX_LEN) != 0)
    {
        return false;
    }

    const char* encoded = auth_header + PREFIX_LEN;
    size_t encoded_len = strlen(encoded);

    if (encoded_len == 0)
    {
        return false;
    }

    // Decode into a stack buffer (Base64 expands to at most 3/4 of input)
    // Max reasonable Auth header is ~256 bytes, so 192 decoded bytes is ample.
    char decoded[192];
    int decoded_len = base64_decode(encoded, encoded_len,
                                    decoded, sizeof(decoded) - 1);
    if (decoded_len <= 0)
    {
        return false;
    }
    decoded[decoded_len] = '\0';

    // Find ':' separator between username and password
    const char* colon = strchr(decoded, ':');
    if (colon == nullptr)
    {
        return false;
    }

    // Compare the password portion (everything after the colon)
    const char* decoded_password = colon + 1;
    return strcmp(decoded_password, password) == 0;
}
