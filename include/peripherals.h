#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include <Arduino.h>
#include "SD.h"
#include "utility.h"

#define LED_ON_CS_ACTIVE  LOW
#define LED_OFF_CS_DEACTIVE HIGH

// See project notes - pin 21 is shared between LED and SD chip select
// GPIO21 is the builtin LED according to the documentation
// GPIO3 should be the chip select
#define SD_CS 21

namespace peripherals
{
    using sd_card_type = uint8_t;

    enum class SdState : uint8_t
    {
        IDLE,
        START_WRITE,
        PRE_DEACTIVATE_CS,
        ACTIVATE_CS,
        WRITING,
        STOP_WRITE,
        POST_DEACTIVATE_CS,
        UNKNOWN,
        ERROR
    };

    enum class LedState : uint8_t
    {
        IDLE_LED_OFF,
        IDLE_LED_ON,
        BLINK_LED_OFF,
        BLINK_LED_ON,
        PULSE_LED_ON,
        UNKNOWN,
        ERROR
    };

    enum class SdInitResult : uint8_t
    {
        SUCCESS,
        HW_INIT_FAILURE,
        NO_SD_CARD
    };

    enum class SdWriteResult : uint8_t
    {
        SUCCESS,
        FILE_OPEN_FAILED,
        FILE_WRITE_ERROR,
        TIMEOUT
    };

    class PeripheralManager
    {
        public:
            PeripheralManager() = default;
            ~PeripheralManager() = default;

            void tick();
            void turn_led_on();
            void turn_led_off();
            void pulse_led_on(utility::time_t pulse_time_ms);
            void blink_led(utility::time_t on_time_ms, utility::time_t off_time_ms);
            SdWriteResult write_file_to_sd_synchronous(const char* path, uint8_t *data, size_t len, utility::time_t timeout_ms);
            void init_led();
            SdInitResult init_sd_card();

        private:
            SdState m_sd_state{SdState::IDLE};
            LedState m_led_state{LedState::IDLE_LED_OFF};
      
            utility::time_t m_last_sd_state_change_time_ms{0};
            utility::time_t m_last_led_state_change_time_ms{0};
            utility::time_t m_led_on_duration_ms{0};
            utility::time_t m_led_off_duration_ms{0};

            sd_card_type m_sd_card_type{CARD_NONE};

            // bundle the data fields used for file write
            struct {
                const char *path;
                uint8_t *data;
                size_t len;
            } m_file;

            SdWriteResult m_sd_write_result{SdWriteResult::SUCCESS};
            
            bool run_led_state_machine();
            bool run_sd_state_machine(bool candidate_led_state_on);
            const void apply_led_state(bool led_state_on);
            void start_file_write(const char* path, uint8_t *data, size_t len);

            static constexpr utility::time_t SD_CS_DEACTIVATE_TIME_MS = 10;
            static constexpr utility::time_t SD_CS_ACTIVATE_TIME_MS = 5;
            static constexpr utility::time_t SD_SYNCHRONOUS_WRITE_TICK_TIME_MS = 1;
            static constexpr bool VERBOSE = false;
    };
}

#endif // PERIPHERALS_H