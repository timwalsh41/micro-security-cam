#include "pin_arbitrator.h"

namespace peripherals
{
    bool PinArbitrator::resolve(bool led_wants_on, SdSignal sd_signal)
    {
        switch (sd_signal)
        {
            case SdSignal::CS_ACTIVE:
                // SD card requires CS LOW — override LED request
                return true;

            case SdSignal::CS_INACTIVE:
                // SD card requires CS HIGH — override LED request
                return false;

            case SdSignal::IDLE:
            default:
                // SD is idle; LED's request determines the pin state
                return led_wants_on;
        }
    }
}