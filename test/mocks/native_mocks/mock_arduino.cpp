#include "Arduino.h"

HardwareSerial Serial;
static uint32_t mock_millis = 0;
static std::vector<std::pair<uint8_t, uint8_t>> gpio_log;

uint32_t millis()
{
    return mock_millis;
}

void set_mock_millis(uint32_t ms)
{
    mock_millis = ms;
}

void delay(uint32_t ms)
{
    mock_millis += ms;
}

void delayMicroseconds(uint32_t us)
{
    mock_millis += us / 1000;
}

void pinMode(uint8_t, uint8_t)
{
    // No-op in native tests
}

void digitalWrite(uint8_t pin, uint8_t value)
{
    gpio_log.push_back(std::make_pair(pin, value));
}

std::vector<std::pair<uint8_t, uint8_t>> &get_gpio_log()
{
    return gpio_log;
}

void clear_gpio_log()
{
    gpio_log.clear();
}
