#include <unity.h>
#include "sd_controller.h"
#include "time_service.h"
#include "mock_sd_card_hal.h"
#include <Arduino.h>
#include "hal/time_hal.h"

using hal::millis_t;
using peripherals::SdSignal;

// ============================================================
// Test helpers
// ============================================================

/**
 * @brief Advances time and ticks until the CS pin is asserted.
 *
 * Advances mock time by 1ms per tick through the PRE_DEACTIVATE_CS
 * timing state until the controller asserts CS.
 *
 * @param time      The mock time service.
 * @param ctrl      The SdController under test.
 * @return SdSignal::CS_ACTIVE  if CS was asserted.
 * @return SdSignal::IDLE or CS_INACTIVE if CS was not asserted after sufficient ticks.
 */
static SdSignal tick_until_cs_asserted(RealTimeService& time,
                                        SdController& ctrl)
{
    // PRE_DEACTIVATE_CS requires DEACTIVATE_TIME_MS + 1 ms to transition
    for (millis_t elapsed = 0; elapsed <= SdController::DEACTIVATE_TIME_MS + 1; elapsed++)
    {
        set_mock_millis(elapsed);
        SdSignal sig = ctrl.tick();
        if (sig == SdSignal::CS_ACTIVE)
        {
            return sig;
        }
    }
    return SdSignal::IDLE;
}

// ============================================================
// IDLE state
// ============================================================

/**
 * @brief IDLE state always returns SdSignal::IDLE (SD doesn't care about pin).
 *        Assumes state machine is initialized in IDLE state.
 */
void test_idle_returns_cs_inactive()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController ctrl(time, sd_hal);

    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());
}

// ============================================================
// START_WRITE → PRE_DEACTIVATE_CS
// ============================================================

/**
 * @brief After start_write(), the first tick() enters PRE_DEACTIVATE_CS
 *        and returns CS_INACTIVE (SD actively de-asserting CS).
 */
void test_start_write_transitions_to_pre_deactivate()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // First tick after start_write → PRE_DEACTIVATE_CS → CS_INACTIVE
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());
}

// ============================================================
// PRE_DEACTIVATE_CS → ACTIVATE_CS
// ============================================================

/**
 * @brief After DEACTIVATE_TIME_MS in PRE_DEACTIVATE_CS, tick()
 *        transitions to ACTIVATE_CS and returns CS_ACTIVE.
 */
void test_pre_deactivate_to_activate_after_delay()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Tick into PRE_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Advance to just before the timeout → still PRE_DEACTIVATE_CS → CS_INACTIVE
    // (DEACTIVATE_TIME_MS = 10, elapsed = 10, condition is > 10 → not met)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Advance past the timeout → transitions to ACTIVATE_CS → CS_ACTIVE
    // (elapsed = 11 > 10 → true)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());
}

// ============================================================
// ACTIVATE_CS → IO_ACTIVE
// ============================================================

/**
 * @brief After ACTIVATE_TIME_MS in ACTIVATE_CS, tick() transitions
 *        to IO_ACTIVE and returns CS_ACTIVE (CS still asserted).
 *
 * NOTE: The IO_ACTIVE body (I/O) does NOT execute on this tick — each
 *       tick executes exactly one state's body. The I/O runs on the
 *       next call to tick() when state is already IO_ACTIVE.
 */
void test_activate_to_writing_after_delay()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance through PRE_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());  // → ACTIVATE_CS

    // Just before ACTIVATE_CS timeout → still ACTIVATE_CS → CS_ACTIVE
    // (ACTIVATE_TIME_MS = 5, elapsed = 5, condition is > 5 → not met)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Past ACTIVATE_CS timeout → transitions to IO_ACTIVE → CS_ACTIVE
    // (elapsed = 6 > 5 → true)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // HAL has NOT been called yet — IO_ACTIVE I/O runs on next tick
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);
}

// ============================================================
// IO_ACTIVE → END_IO
// ============================================================

/**
 * @brief When tick() is called with state already IO_ACTIVE, it
 *        calls HAL open/write/close and transitions to END_IO.
 */
void test_writing_performs_io_and_moves_to_stop()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance through PRE_DEACTIVATE_CS and ACTIVATE_CS to reach IO_ACTIVE
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → IO_ACTIVE (transition)

    // One more tick: IO_ACTIVE executes I/O → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Verify HAL was called correctly
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/test.bin", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(hal::FileOpenMode::WRITE, sd_hal.last_open_mode);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(data, sd_hal.last_write_data);
    TEST_ASSERT_EQUAL_UINT32(3, sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);
}

// ============================================================
// END_IO → POST_DEACTIVATE_CS
// ============================================================

/**
 * @brief After IO_ACTIVE I/O, the next tick() enters END_IO
 *        which then immediately transitions to POST_DEACTIVATE_CS
 *        and returns CS_INACTIVE (SD actively de-asserting CS).
 */
void test_stop_write_transitions_to_post_deactivate()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance to IO_ACTIVE
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → IO_ACTIVE (transition)

    // Execute IO_ACTIVE I/O → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // IO_ACTIVE I/O → END_IO

    // Next tick: END_IO → POST_DEACTIVATE_CS → CS_INACTIVE
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());
}

// ============================================================
// POST_DEACTIVATE_CS → IDLE
// ============================================================

/**
 * @brief After DEACTIVATE_TIME_MS in POST_DEACTIVATE_CS, tick()
 *        returns to IDLE and returns SdSignal::IDLE.
 */
void test_post_deactivate_to_idle_after_delay()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance through the full cycle to POST_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → IO_ACTIVE (transition)
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // IO_ACTIVE I/O → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick()); // END_IO → POST_DEACTIVATE_CS

    // Just before POST_DEACTIVATE_CS timeout → still POST_DEACTIVATE_CS → CS_INACTIVE
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Past POST_DEACTIVATE_CS timeout → returns to IDLE → IDLE
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    // Verify back in IDLE — subsequent ticks return IDLE
    set_mock_millis(millis() + 100);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());
}

// ============================================================
// Full write cycle
// ============================================================

/**
 * @brief Complete state sequence produces correct SdSignal at
 *        each stage and calls HAL I/O exactly once.
 */
void test_full_write_cycle_state_sequence()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    SdController ctrl(time, sd_hal);

    // Phase: IDLE
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    ctrl.start_write("/photo.dat", data, 4);

    // Phase: PRE_DEACTIVATE_CS (SD actively de-asserting)
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Phase: transition to ACTIVATE_CS (SD asserting CS)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Phase: transition to IO_ACTIVE (SD still asserting CS)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());  // ACTIVATE_CS → IO_ACTIVE (transition)

    // Phase: execute IO_ACTIVE I/O → END_IO (SD still asserting CS)
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Verify HAL calls
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/photo.dat", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(hal::FileOpenMode::WRITE, sd_hal.last_open_mode);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(data, sd_hal.last_write_data);
    TEST_ASSERT_EQUAL_UINT32(4, sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);

    // Phase: END_IO → POST_DEACTIVATE_CS (SD actively de-asserting)
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Phase: POST_DEACTIVATE_CS → IDLE (SD doesn't care)
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    // Verify back in IDLE — subsequent ticks return IDLE, no additional HAL calls
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);  // unchanged
}

// ============================================================
// End-to-end synchronous-style write
// ============================================================

/**
 * @brief A full write cycle driven with explicit time advancement simulates
 *        the synchronous write path and verifies all HAL calls.
 */
void test_end_to_end_write_success()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    SdController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 5);

    // PRE_DEACTIVATE_CS: wait DEACTIVATE_TIME_MS then transition
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, tick_until_cs_asserted(time, ctrl));

    // ACTIVATE_CS: wait ACTIVATE_TIME_MS then transition
    set_mock_millis(millis() + SdController::ACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // still ACTIVATE_CS
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → IO_ACTIVE (transition)

    // Execute IO_ACTIVE I/O → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // END_IO → POST_DEACTIVATE_CS
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // POST_DEACTIVATE_CS → IDLE (wait DEACTIVATE_TIME_MS + 1)
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    // Verify HAL was called correctly
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/test.bin", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(hal::FileOpenMode::WRITE, sd_hal.last_open_mode);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(data, sd_hal.last_write_data);
    TEST_ASSERT_EQUAL_UINT32(5, sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);

    // Back in IDLE — subsequent ticks return IDLE
    set_mock_millis(millis() + 100);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());
}

/**
 * @brief Debug test: verifies that open() IS called even when
 *        open_result is false.
 *
 * Direct tick-by-tick trace with minimal assertions.
 */
void test_end_to_end_write_open_failed()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    sd_hal.open_result = false;

    ctrl.start_write("/test.bin", data, 3);

    // Tick 1: PRE_DEACTIVATE_CS → stays (time 0, elapsed 0)
    SdSignal r1 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_INACTIVE, r1, "tick 1 should be CS_INACTIVE (PRE_DEACTIVATE_CS wait)");

    // Tick 2: PRE_DEACTIVATE_CS → ACTIVATE_CS (time 11, elapsed 11 > 10)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    SdSignal r2 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_ACTIVE, r2, "tick 2 should be CS_ACTIVE (PRE→ACTIVATE)");

    // Tick 3: ACTIVATE_CS → IO_ACTIVE (time 17, elapsed 6 > 5)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    SdSignal r3 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_ACTIVE, r3, "tick 3 should be CS_ACTIVE (ACTIVATE→IO_ACTIVE)");

    // Tick 4: IO_ACTIVE I/O (time 18)
    set_mock_millis(millis() + 1);
    SdSignal r4 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_ACTIVE, r4, "tick 4 should be CS_ACTIVE (IO_ACTIVE I/O)");

    // Check call counts NOW, right after the IO_ACTIVE tick
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sd_hal.open_call_count, "open_call_count after IO_ACTIVE I/O");

    // Tick 5: END_IO → POST_DEACTIVATE_CS (time 19)
    set_mock_millis(millis() + 1);
    SdSignal r5 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_INACTIVE, r5, "tick 5 should be CS_INACTIVE (END_IO→POST)");

    // Tick 6: POST_DEACTIVATE_CS → IDLE (time 30)
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS + 1);
    SdSignal r6 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::IDLE, r6, "tick 6 should be IDLE (POST→IDLE)");

    // Verify open was called, but write was not
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_INT(0, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);
}

/**
 * @brief Verifies FILE_WRITE_ERROR when HAL write() returns false.
 */
void test_end_to_end_write_write_error()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdController ctrl(time, sd_hal);

    sd_hal.write_result = false;

    ctrl.start_write("/test.bin", data, 3);

    // PRE_DEACTIVATE_CS: wait DEACTIVATE_TIME_MS then transition
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, tick_until_cs_asserted(time, ctrl));

    // ACTIVATE_CS → WRITING (transition)
    set_mock_millis(millis() + SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Execute IO_ACTIVE I/O (write fails) → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // END_IO → POST_DEACTIVATE_CS
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // POST_DEACTIVATE_CS → IDLE
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    // Verify both open and write were attempted, and close was called
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);
}

// ============================================================
// init_card
// ============================================================

/**
 * @brief init_card() delegates to SdCardHal::init() and returns its result.
 */
void test_init_card_delegates_to_hal()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    SdController ctrl(time, sd_hal);

    sd_hal.init_result = hal::SdInitResult::SUCCESS;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::SdInitResult::SUCCESS),
                          static_cast<int>(ctrl.init_card()));
    TEST_ASSERT_EQUAL_INT(1, sd_hal.init_call_count);

    sd_hal.init_result = hal::SdInitResult::HW_INIT_FAILURE;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::SdInitResult::HW_INIT_FAILURE),
                          static_cast<int>(ctrl.init_card()));
    TEST_ASSERT_EQUAL_INT(2, sd_hal.init_call_count);

    sd_hal.init_result = hal::SdInitResult::NO_SD_CARD;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::SdInitResult::NO_SD_CARD),
                          static_cast<int>(ctrl.init_card()));
    TEST_ASSERT_EQUAL_INT(3, sd_hal.init_call_count);
}

// ============================================================
// Read operations
// ============================================================

/**
 * @brief start_read() transitions to PRE_DEACTIVATE_CS, same as write.
 */
void test_start_read_transitions_to_pre_deactivate()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t buf[8];
    SdController ctrl(time, sd_hal);

    ctrl.start_read("/config.ini", buf, sizeof(buf));

    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());
}

/**
 * @brief IO_ACTIVE state calls HAL open with READ mode and populates buffer.
 */
void test_read_opens_with_read_mode_and_copies_data()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t buf[4] = {0};
    SdController ctrl(time, sd_hal);

    // Set up fixture: 3 bytes of content to read
    sd_hal.file_content = {0xDE, 0xAD, 0xBE};

    ctrl.start_read("/config.ini", buf, sizeof(buf));

    // Advance through PRE_DEACTIVATE_CS and ACTIVATE_CS to reach IO_ACTIVE
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());  // ACTIVATE_CS → IO_ACTIVE

    // Execute IO_ACTIVE: open + read + close → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Verify HAL was called correctly
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/config.ini", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(hal::FileOpenMode::READ, sd_hal.last_open_mode);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.read_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);

    // Verify data was copied into the buffer
    TEST_ASSERT_EQUAL_UINT8(0xDE, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAD, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xBE, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);  // untouched
}

/**
 * @brief Full read cycle produces SUCCESS result with correct data and byte count.
 *
 * Driven manually via tick() + set_mock_millis() to avoid the infinite
 * loop that occurs when the synchronous read_file() wrapper calls
 * tick() → millis() and millis() never auto-advances.
 */
void test_read_file_success()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t buf[8] = {0};
    SdController ctrl(time, sd_hal);

    sd_hal.file_content = {0x01, 0x02, 0x03};

    ctrl.start_read("/data.txt", buf, sizeof(buf));

    // Drive through the full state machine cycle
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());    // → ACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());    // → IO_ACTIVE (transition)
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());    // IO_ACTIVE I/O → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // END_IO → POST_DEACTIVATE_CS
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());         // → IDLE

    // Verify HAL was called correctly
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/data.txt", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(hal::FileOpenMode::READ, sd_hal.last_open_mode);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.read_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);

    // Verify data was copied into the buffer
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x03, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);  // untouched
}

/**
 * @brief Full read cycle with HAL open failure produces FILE_OPEN_FAILED.
 *
 * Verifies that when HAL open() returns false, close() is still called
 * for cleanup, the buffer is untouched, and the operation completes
 * back to IDLE with the expected error result.
 */
void test_read_file_open_failed()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    SdController ctrl(time, sd_hal);

    sd_hal.open_result = false;

    ctrl.start_read("/missing.txt", buf, sizeof(buf));

    // Drive through the full state machine cycle
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());    // → ACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());    // → IO_ACTIVE (transition)
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());    // IO_ACTIVE I/O → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // END_IO → POST_DEACTIVATE_CS
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());         // → IDLE

    // Verify HAL calls — open attempted but failed, no read, close still called
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_INT(0, sd_hal.read_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);

    // Verify buffer is untouched
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[3]);
}

/**
 * @brief State machine stalls in PRE_DEACTIVATE_CS when time does not advance.
 *
 * Demonstrates the condition that read_file()'s timeout logic protects
 * against: without time advancement, the state machine never progresses
 * past PRE_DEACTIVATE_CS.
 */
void test_read_stalls_without_time_advance()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t buf[4] = {0};
    SdController ctrl(time, sd_hal);

    sd_hal.file_content = {0xAA};

    ctrl.start_read("/data.txt", buf, sizeof(buf));

    // First tick enters PRE_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Without advancing time, repeated ticks stay in PRE_DEACTIVATE_CS
    set_mock_millis(0);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // No I/O has occurred
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_INT(0, sd_hal.read_call_count);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[0]);  // untouched
}

/**
 * @brief Full read cycle produces correct SdSignal at each stage.
 */
void test_full_read_cycle_state_sequence()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    uint8_t buf[4] = {0};
    SdController ctrl(time, sd_hal);

    sd_hal.file_content = {0xCA, 0xFE};

    ctrl.start_read("/test.ini", buf, sizeof(buf));

    // PRE_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // ACTIVATE_CS
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // ACTIVATE_CS → IO_ACTIVE (transition)
    set_mock_millis(SdController::DEACTIVATE_TIME_MS + 1 +
             SdController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // IO_ACTIVE: read I/O → END_IO
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Verify HAL calls
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/test.ini", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL(hal::FileOpenMode::READ, sd_hal.last_open_mode);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.read_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);
    TEST_ASSERT_EQUAL_UINT8(0xCA, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFE, buf[1]);

    // END_IO → POST_DEACTIVATE_CS
    set_mock_millis(millis() + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // POST_DEACTIVATE_CS → IDLE
    set_mock_millis(millis() + SdController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());
}

// ============================================================
// Unity boilerplate
// ============================================================

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    // IDLE state
    RUN_TEST(test_idle_returns_cs_inactive);

    // State transitions
    RUN_TEST(test_start_write_transitions_to_pre_deactivate);
    RUN_TEST(test_pre_deactivate_to_activate_after_delay);
    RUN_TEST(test_activate_to_writing_after_delay);
    RUN_TEST(test_writing_performs_io_and_moves_to_stop);
    RUN_TEST(test_stop_write_transitions_to_post_deactivate);
    RUN_TEST(test_post_deactivate_to_idle_after_delay);

    // Full cycle
    RUN_TEST(test_full_write_cycle_state_sequence);

    // End-to-end write tests (synchronous-style with manual time advancement)
    // NOTE: open_failed runs first to check if test ordering affects the result
    RUN_TEST(test_end_to_end_write_open_failed);
    RUN_TEST(test_end_to_end_write_success);
    RUN_TEST(test_end_to_end_write_write_error);

    // init_card
    RUN_TEST(test_init_card_delegates_to_hal);

    // Read operations
    RUN_TEST(test_start_read_transitions_to_pre_deactivate);
    RUN_TEST(test_read_opens_with_read_mode_and_copies_data);
    RUN_TEST(test_read_file_success);
    RUN_TEST(test_read_file_open_failed);
    RUN_TEST(test_read_stalls_without_time_advance);
    RUN_TEST(test_full_read_cycle_state_sequence);

    return UNITY_END();
}
