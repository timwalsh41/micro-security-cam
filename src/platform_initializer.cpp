#include "platform_initializer.h"
#include "esp_log.h"

namespace
{

// No-op handler that swallows all ESP-IDF vprintf output, preventing it
// from reaching UART0 and interleaving with application log writes on
// the dual-core ESP32-S3.
//
// esp_log_level_set("*", ESP_LOG_NONE) alone is unreliable — individual
// components may override per-tag levels during init. Installing a no-op
// vprintf guarantees nothing reaches UART0.
static int no_op_vprintf(const char*, va_list)
{
    return 0;
}

} // anonymous namespace

void PlatformInitializer::init()
{
    suppress_esp_idf_logging();
}

void PlatformInitializer::suppress_esp_idf_logging()
{
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_set_vprintf(no_op_vprintf);
}
