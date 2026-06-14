#include "peripherals.h"
#include "SD.h"

namespace peripherals
{
    constexpr utility::time_t PeripheralManager::SD_CS_DEACTIVATE_TIME_MS;
    constexpr utility::time_t PeripheralManager::SD_CS_ACTIVATE_TIME_MS;

    void PeripheralManager::tick()
    {
        // run LED state machine first to get it's requested LED state
        bool candidate_led_state_on = run_led_state_machine();
        
        // the final state for the LED/SD CS pin
        // the LED is on when CS is active
        // this will be determined by getting the requested state from the LED state machine, and
        // overriding in the SD state machine if necessary
        bool led_state_on_cs_active = run_sd_state_machine(candidate_led_state_on);

        // set the LED/CS state
        apply_led_state(led_state_on_cs_active);
    }

    bool PeripheralManager::run_sd_state_machine(bool candidate_led_state_on)
    {
        utility::time_t now = millis();
        bool led_state_on_cs_active;

        if (m_sd_state == SdState::IDLE)
        {
            // SD card idle - no impact on LED
            led_state_on_cs_active = candidate_led_state_on;
        }
        else if (m_sd_state == SdState::START_WRITE)
        {
            // transitory state to deassert chip select
            if (VERBOSE)
                utility::print_with_timestamp("SD state -> PRE_DEACTIVATE_CS");
            
            m_sd_state = SdState::PRE_DEACTIVATE_CS;
            m_last_sd_state_change_time_ms = now;

            // override LED state to deassert chip select
            led_state_on_cs_active = false;
        }
        else if (m_sd_state == SdState::PRE_DEACTIVATE_CS)
        {
            if (utility::time_elapsed_ms(m_last_sd_state_change_time_ms) > SD_CS_DEACTIVATE_TIME_MS)
            {
                if (VERBOSE)
                    utility::print_with_timestamp("SD state -> ACTIVATE_CS");
                
                m_sd_state = SdState::ACTIVATE_CS;
                m_last_sd_state_change_time_ms = now;

                // override LED state to assert chip select
                led_state_on_cs_active = true;
            }
            else
                // keep LED off
                led_state_on_cs_active = false;
        }
        else if (m_sd_state == SdState::ACTIVATE_CS)
        {
            // we always keep chip select asserted here, whether lingering in this state or
            // transitioning to WRITING
            led_state_on_cs_active = true;

            if (utility::time_elapsed_ms(m_last_sd_state_change_time_ms) > SD_CS_ACTIVATE_TIME_MS)
            {
                if (VERBOSE)
                    utility::print_with_timestamp("SD state -> WRITING");
                
                m_sd_state = SdState::WRITING;
                m_last_sd_state_change_time_ms = now;                
            }
        }
        else if (m_sd_state == SdState::WRITING)
        {
            // blocking state for synchronous write
            File file = SD.open(m_file.path, FILE_WRITE);
            if(!file)
                m_sd_write_result = SdWriteResult::FILE_OPEN_FAILED;

            if (m_sd_write_result == SdWriteResult::SUCCESS)
            {
                if(file.write(m_file.data, m_file.len) != m_file.len)
                    m_sd_write_result = SdWriteResult::FILE_WRITE_ERROR;
            }

            file.close();
           
            if (VERBOSE)
                utility::print_with_timestamp("SD state -> STOP_WRITE");
            
            m_sd_state = SdState::STOP_WRITE;

            // we always keep chip select asserted here
            led_state_on_cs_active = true;
        }
        else if (m_sd_state == SdState::STOP_WRITE)
        {
            // transitory state to deassert chip select
            if (VERBOSE)
                utility::print_with_timestamp("SD state -> POST_DEACTIVATE_CS");
            
            m_sd_state = SdState::POST_DEACTIVATE_CS;
            m_last_sd_state_change_time_ms = now;

            // override LED state to deassert chip select
            led_state_on_cs_active = false;
        }
        else if (m_sd_state == SdState::POST_DEACTIVATE_CS)
        {
            if (utility::time_elapsed_ms(m_last_sd_state_change_time_ms) > SD_CS_DEACTIVATE_TIME_MS)
            {
                if (VERBOSE)
                    utility::print_with_timestamp("SD state -> IDLE");
                
                m_sd_state = SdState::IDLE;
                m_last_sd_state_change_time_ms = now;

                // adopt candidate LED state
                led_state_on_cs_active = candidate_led_state_on;
            }
            else
                // keep LED off
                led_state_on_cs_active = false;
        }
        else if (m_sd_state == SdState::UNKNOWN)
        {
            utility::print_with_timestamp("SD state machine in unknown state, moving to error");
            
            m_sd_state = SdState::ERROR;
            m_last_sd_state_change_time_ms = now;
        }

        return led_state_on_cs_active;
    }

    bool PeripheralManager::run_led_state_machine()
    {
        utility::time_t now = millis();
        bool candidate_led_on_value = false;

        if (m_led_state == LedState::IDLE_LED_OFF)
        {
            candidate_led_on_value = false;
        }
        else if (m_led_state == LedState::IDLE_LED_ON)
        {
            candidate_led_on_value = true;
        }
        else if (m_led_state == LedState::PULSE_LED_ON)
        {
            if (utility::time_elapsed_ms(m_last_led_state_change_time_ms) > m_led_on_duration_ms)
            {
                if (VERBOSE)
                    utility::print_with_timestamp("LED state -> IDLE_LED_OFF");
                
                m_led_state = LedState::IDLE_LED_OFF;
                candidate_led_on_value = false;
                m_last_led_state_change_time_ms = now;
            }
            else
                candidate_led_on_value = true;
        }
        else if (m_led_state == LedState::BLINK_LED_ON)
        {
            if (utility::time_elapsed_ms(m_last_led_state_change_time_ms) > m_led_on_duration_ms)
            {
                if (VERBOSE)
                    utility::print_with_timestamp("LED state -> LED_BLINK_OFF");
                
                m_led_state = LedState::BLINK_LED_OFF;
                candidate_led_on_value = false;
                m_last_led_state_change_time_ms = now;
            }
            else
                candidate_led_on_value = true;
        }
        else if (m_led_state == LedState::BLINK_LED_OFF)
        {
            if (utility::time_elapsed_ms(m_last_led_state_change_time_ms) > m_led_off_duration_ms)
            {
                if (VERBOSE)
                    utility::print_with_timestamp("LED state -> LED_BLINK_ON");
                
                m_led_state = LedState::BLINK_LED_ON;
                candidate_led_on_value = true;
                m_last_led_state_change_time_ms = now;
            }
            else
                candidate_led_on_value = false;
        }
        else if (m_led_state ==LedState::UNKNOWN)
        {
            utility::print_with_timestamp("LED state machine in unknown state, moving to error");
            m_led_state = LedState::ERROR;
            candidate_led_on_value = false;
            m_last_led_state_change_time_ms = now;
        }

        return candidate_led_on_value;
    }

    SdWriteResult PeripheralManager::write_file_to_sd_synchronous(const char* path, uint8_t *data, size_t len, utility::time_t timeout_ms)
    {
        // initiate the file write and then tick state machine until completion, with timeout
        utility::time_t start_ms = millis();
        start_file_write(path, data, len);

        m_sd_write_result = SdWriteResult::SUCCESS;
        utility::time_t next_tick_time_ms = start_ms;

        while (utility::time_elapsed_ms(start_ms) < timeout_ms && 
            m_sd_state != SdState::IDLE &&
            m_sd_write_result == SdWriteResult::SUCCESS)
        {
            if (millis() >= next_tick_time_ms)
            {
                tick();
                next_tick_time_ms += SD_SYNCHRONOUS_WRITE_TICK_TIME_MS;
            }
        }

        if (utility::time_elapsed_ms(start_ms) >= timeout_ms)
            m_sd_write_result = SdWriteResult::TIMEOUT;

        return m_sd_write_result;
    }

    void PeripheralManager::start_file_write(const char* path, uint8_t *data, size_t len)
    {
        if (VERBOSE)
            utility::print_with_timestamp("SD state -> START_WRITE");
        
        m_sd_state = SdState::START_WRITE;
        m_file.path = path;
        m_file.data = data;
        m_file.len = len;
    }

    void PeripheralManager::turn_led_on()
    {
        if (VERBOSE)
            utility::print_with_timestamp("LED state -> IDLE_LED_ON");
        
        m_led_state = LedState::IDLE_LED_ON;
    }

    void PeripheralManager::turn_led_off()
    {
        if (VERBOSE)
            utility::print_with_timestamp("LED state -> IDLE_LED_OFF");
        
        m_led_state = LedState::IDLE_LED_OFF;
    }

    void PeripheralManager::pulse_led_on(utility::time_t time_ms)
    {
        m_last_led_state_change_time_ms = millis();
        m_led_on_duration_ms = time_ms;

        if (VERBOSE)
            utility::print_with_timestamp("LED state -> PULSE_LED_ON");
        
        m_led_state = LedState::PULSE_LED_ON;
    }

    void PeripheralManager::blink_led(utility::time_t on_time_ms, utility::time_t off_time_ms)
    {
        m_last_led_state_change_time_ms = millis();
        m_led_on_duration_ms = on_time_ms;
        m_led_off_duration_ms = off_time_ms;

        if (VERBOSE)
            utility::print_with_timestamp("LED state -> BLINK_LED_ON");
        
        m_led_state = LedState::BLINK_LED_ON;
    }

    const void PeripheralManager::apply_led_state(bool led_state_on)
    {
        digitalWrite(LED_BUILTIN, led_state_on ? LED_ON_CS_ACTIVE : LED_OFF_CS_DEACTIVE);
    }

    SdInitResult PeripheralManager::init_sd_card()
    {
        // Initialize SD card
        if(!SD.begin(SD_CS))
            return SdInitResult::HW_INIT_FAILURE;
        
        m_sd_card_type = SD.cardType();

        // Determine if the type of SD card is available
        if(m_sd_card_type == CARD_NONE)
            return SdInitResult::NO_SD_CARD;
        
        return SdInitResult::SUCCESS;
    }

    void PeripheralManager::init_led()
    {
        // initialize LED digital pin as an output.
        pinMode(LED_BUILTIN, OUTPUT);

        if (VERBOSE)
            utility::print_with_timestamp("LED state -> IDLE_LED_OFF");
        
        m_led_state = LedState::IDLE_LED_OFF;
        apply_led_state(false);
    }
}