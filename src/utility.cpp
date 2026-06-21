#include <Arduino.h>
#include "utility.h"

namespace utility
{
time_t time_elapsed_ms(time_t since_ms)
{
    time_t now_ms = millis();
    time_t elapsed_ms = 0;

    if (now_ms >= since_ms)
    {
        elapsed_ms = now_ms - since_ms;
    }
    else
    {
        elapsed_ms = TIME_MAX - since_ms + now_ms;
    }

    return elapsed_ms;
}

void print_with_timestamp(const String& s)
{
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] ");
    Serial.println(s);
}
}