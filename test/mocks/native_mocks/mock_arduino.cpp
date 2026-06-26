#include "Arduino.h"

HardwareSerial Serial;
static uint32_t mock_millis = 0;

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
