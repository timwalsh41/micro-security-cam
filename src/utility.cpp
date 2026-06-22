#include <Arduino.h>
#include "utility.h"

namespace utility
{

void print_with_timestamp(const String& s)
{
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] ");
    Serial.println(s);
}

} // namespace utility