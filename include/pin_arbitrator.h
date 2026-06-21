#ifndef PIN_ARBITRATOR_H
#define PIN_ARBITRATOR_H

#include <cstdint>

/**
 * @file pin_arbitrator.h
 * @brief Resolves shared-pin conflicts between LED and SD-CS control.
 *
 * On this board, both LED-on and CS-active map to LOW logic level.
 * The PinArbitrator determines whether the shared pin should be driven
 * LOW based on the combined requests from the LED and SD card subsystems.
 */
namespace peripherals
{
    /**
     * @brief Describes the SD card's current request for the shared CS pin.
     *
     * The SD write controller drives this signal to indicate what the
     * chip-select pin should do. When IDLE, the LED's request prevails.
     */
    enum class SdSignal : uint8_t
    {
        IDLE,           ///< SD doesn't care; LED's request wins
        CS_ACTIVE,      ///< SD needs CS driven LOW (during activation/writing)
        CS_INACTIVE     ///< SD needs CS driven HIGH (during deactivation timing)
    };

    /**
     * @class PinArbitrator
     * @brief Static utility class for resolving shared-pin conflicts.
     *
     * Resolves whether the shared LED/SD-CS pin should be driven LOW
     * based on the LED's desired state and the SD card's signal request.
     *
     * Arbitration rules:
     *  - SdSignal::IDLE        → LED's request is granted.
     *  - SdSignal::CS_ACTIVE   → Pin is forced LOW (CS active).
     *  - SdSignal::CS_INACTIVE → Pin is forced HIGH (CS inactive).
     */
    class PinArbitrator
    {
        public:
            /**
             * @brief Determines if the shared pin should be driven LOW.
             *
             * @param led_wants_on True if the LED wants to be ON (active LOW).
             * @param sd_signal The SD card's current CS signal request.
             * @return true  Pin should be driven LOW (active).
             * @return false Pin should be driven HIGH (inactive).
             */
            static bool resolve(bool led_wants_on, SdSignal sd_signal);

        private:
            PinArbitrator() = delete;
    };
}

#endif // PIN_ARBITRATOR_H