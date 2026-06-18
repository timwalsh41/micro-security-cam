#ifndef UTILITY_H
#define UTILITY_H

#include <Arduino.h>

namespace utility
{
    constexpr time_t TIME_MAX = UINT32_MAX;

    time_t time_elapsed_ms(time_t since_ms);
    void print_with_timestamp(const String& s);
}


#endif // UTILITY_H