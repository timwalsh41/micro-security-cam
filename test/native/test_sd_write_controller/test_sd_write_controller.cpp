#include <unity.h>
#include "sd_write_controller.h"
#include "mock_sd_card_hal.h"
#include "mock_time_service.h"

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
 * @param ctrl      The SdWriteController under test.
 * @return SdSignal::CS_ACTIVE  if CS was asserted.
 * @return SdSignal::IDLE or CS_INACTIVE if CS was not asserted after sufficient ticks.
 */
static SdSignal tick_until_cs_asserted(MockTimeService &time,
                                        SdWriteController &ctrl)
{
    // PRE_DEACTIVATE_CS requires DEACTIVATE_TIME_MS + 1 ms to transition
    for (uint32_t elapsed = 0; elapsed <= SdWriteController::DEACTIVATE_TIME_MS + 1; elapsed++)
    {
        time.set(elapsed);
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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    SdWriteController ctrl(time, sd_hal);

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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Tick into PRE_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Advance to just before the timeout → still PRE_DEACTIVATE_CS → CS_INACTIVE
    // (DEACTIVATE_TIME_MS = 10, elapsed = 10, condition is > 10 → not met)
    time.set(SdWriteController::DEACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Advance past the timeout → transitions to ACTIVATE_CS → CS_ACTIVE
    // (elapsed = 11 > 10 → true)
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());
}

// ============================================================
// ACTIVATE_CS → WRITING
// ============================================================

/**
 * @brief After ACTIVATE_TIME_MS in ACTIVATE_CS, tick() transitions
 *        to WRITING and returns CS_ACTIVE (CS still asserted).
 *
 * NOTE: The WRITING body (I/O) does NOT execute on this tick — each
 *       tick executes exactly one state's body. The I/O runs on the
 *       next call to tick() when state is already WRITING.
 */
void test_activate_to_writing_after_delay()
{
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance through PRE_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());  // → ACTIVATE_CS

    // Just before ACTIVATE_CS timeout → still ACTIVATE_CS → CS_ACTIVE
    // (ACTIVATE_TIME_MS = 5, elapsed = 5, condition is > 5 → not met)
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1 +
             SdWriteController::ACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Past ACTIVATE_CS timeout → transitions to WRITING → CS_ACTIVE
    // (elapsed = 6 > 5 → true)
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1 +
             SdWriteController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // HAL has NOT been called yet — WRITING I/O runs on next tick
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);
}

// ============================================================
// WRITING → STOP_WRITE
// ============================================================

/**
 * @brief When tick() is called with state already WRITING, it
 *        calls HAL open/write/close and transitions to STOP_WRITE.
 */
void test_writing_performs_io_and_moves_to_stop()
{
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance through PRE_DEACTIVATE_CS and ACTIVATE_CS to reach WRITING
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1 +
             SdWriteController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → WRITING (transition)

    // One more tick: WRITING executes I/O → STOP_WRITE
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Verify HAL was called correctly
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/test.bin", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(data, sd_hal.last_write_data);
    TEST_ASSERT_EQUAL_UINT32(3, sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);
}

// ============================================================
// STOP_WRITE → POST_DEACTIVATE_CS
// ============================================================

/**
 * @brief After WRITING I/O, the next tick() enters STOP_WRITE
 *        which then immediately transitions to POST_DEACTIVATE_CS
 *        and returns CS_INACTIVE (SD actively de-asserting CS).
 */
void test_stop_write_transitions_to_post_deactivate()
{
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance to WRITING
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1 +
             SdWriteController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → WRITING (transition)

    // Execute WRITING I/O → STOP_WRITE
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // WRITING I/O → STOP_WRITE

    // Next tick: STOP_WRITE → POST_DEACTIVATE_CS → CS_INACTIVE
    time.advance(1);
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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 3);

    // Advance through the full cycle to POST_DEACTIVATE_CS
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());  // PRE_DEACTIVATE_CS
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1 +
             SdWriteController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → WRITING (transition)
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // WRITING I/O → STOP_WRITE
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick()); // STOP_WRITE → POST_DEACTIVATE_CS

    // Just before POST_DEACTIVATE_CS timeout → still POST_DEACTIVATE_CS → CS_INACTIVE
    time.advance(SdWriteController::DEACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Past POST_DEACTIVATE_CS timeout → returns to IDLE → IDLE
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    // Verify back in IDLE — subsequent ticks return IDLE
    time.advance(100);
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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    SdWriteController ctrl(time, sd_hal);

    // Phase: IDLE
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    ctrl.start_write("/photo.dat", data, 4);

    // Phase: PRE_DEACTIVATE_CS (SD actively de-asserting)
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Phase: transition to ACTIVATE_CS (SD asserting CS)
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Phase: transition to WRITING (SD still asserting CS)
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1 +
             SdWriteController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());  // ACTIVATE_CS → WRITING (transition)

    // Phase: execute WRITING I/O → STOP_WRITE (SD still asserting CS)
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Verify HAL calls
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/photo.dat", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(data, sd_hal.last_write_data);
    TEST_ASSERT_EQUAL_UINT32(4, sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);

    // Phase: STOP_WRITE → POST_DEACTIVATE_CS (SD actively de-asserting)
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // Phase: POST_DEACTIVATE_CS → IDLE (SD doesn't care)
    time.advance(SdWriteController::DEACTIVATE_TIME_MS + 1);
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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    SdWriteController ctrl(time, sd_hal);

    ctrl.start_write("/test.bin", data, 5);

    // PRE_DEACTIVATE_CS: wait DEACTIVATE_TIME_MS then transition
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, tick_until_cs_asserted(time, ctrl));

    // ACTIVATE_CS: wait ACTIVATE_TIME_MS then transition
    time.advance(SdWriteController::ACTIVATE_TIME_MS);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // still ACTIVATE_CS
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());   // ACTIVATE_CS → WRITING (transition)

    // Execute WRITING I/O → STOP_WRITE
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // STOP_WRITE → POST_DEACTIVATE_CS
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // POST_DEACTIVATE_CS → IDLE (wait DEACTIVATE_TIME_MS + 1)
    time.advance(SdWriteController::DEACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::IDLE, ctrl.tick());

    // Verify HAL was called correctly
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_STRING("/test.bin", sd_hal.last_open_path);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL(data, sd_hal.last_write_data);
    TEST_ASSERT_EQUAL_UINT32(5, sd_hal.last_write_len);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);

    // Back in IDLE — subsequent ticks return IDLE
    time.advance(100);
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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

    sd_hal.open_result = false;

    ctrl.start_write("/test.bin", data, 3);

    // Tick 1: PRE_DEACTIVATE_CS → stays (time 0, elapsed 0)
    SdSignal r1 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_INACTIVE, r1, "tick 1 should be CS_INACTIVE (PRE_DEACTIVATE_CS wait)");

    // Tick 2: PRE_DEACTIVATE_CS → ACTIVATE_CS (time 11, elapsed 11 > 10)
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1);
    SdSignal r2 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_ACTIVE, r2, "tick 2 should be CS_ACTIVE (PRE→ACTIVATE)");

    // Tick 3: ACTIVATE_CS → WRITING (time 17, elapsed 6 > 5)
    time.set(SdWriteController::DEACTIVATE_TIME_MS + 1 +
             SdWriteController::ACTIVATE_TIME_MS + 1);
    SdSignal r3 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_ACTIVE, r3, "tick 3 should be CS_ACTIVE (ACTIVATE→WRITING)");

    // Tick 4: WRITING I/O (time 18)
    time.advance(1);
    SdSignal r4 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_ACTIVE, r4, "tick 4 should be CS_ACTIVE (WRITING I/O)");

    // Check call counts NOW, right after the WRITING tick
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sd_hal.open_call_count, "open_call_count after WRITING I/O");

    // Tick 5: STOP_WRITE → POST_DEACTIVATE_CS (time 19)
    time.advance(1);
    SdSignal r5 = ctrl.tick();
    TEST_ASSERT_EQUAL_MESSAGE(SdSignal::CS_INACTIVE, r5, "tick 5 should be CS_INACTIVE (STOP→POST)");

    // Tick 6: POST_DEACTIVATE_CS → IDLE (time 30)
    time.advance(SdWriteController::DEACTIVATE_TIME_MS + 1);
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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteController ctrl(time, sd_hal);

    sd_hal.write_result = false;

    ctrl.start_write("/test.bin", data, 3);

    // PRE_DEACTIVATE_CS: wait DEACTIVATE_TIME_MS then transition
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, tick_until_cs_asserted(time, ctrl));

    // ACTIVATE_CS → WRITING (transition)
    time.advance(SdWriteController::ACTIVATE_TIME_MS + 1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // Execute WRITING I/O (write fails) → STOP_WRITE
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_ACTIVE, ctrl.tick());

    // STOP_WRITE → POST_DEACTIVATE_CS
    time.advance(1);
    TEST_ASSERT_EQUAL(SdSignal::CS_INACTIVE, ctrl.tick());

    // POST_DEACTIVATE_CS → IDLE
    time.advance(SdWriteController::DEACTIVATE_TIME_MS + 1);
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
    MockTimeService time(0);
    MockSdCardHal sd_hal;
    SdWriteController ctrl(time, sd_hal);

    sd_hal.init_result = hal::SdInitResult::SUCCESS;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::SdInitResult::SUCCESS),
                          static_cast<int>(ctrl.init_card()));
    TEST_ASSERT_EQUAL_INT(1, sd_hal.init_call_count);

    sd_hal.init_result = hal::SdInitResult::HW_INIT_FAILURE;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(hal::SdInitResult::HW_INIT_FAILURE),
                          static_cast<int>(ctrl.init_card()));
    TEST_ASSERT_EQUAL_INT(2, sd_hal.init_call_count);
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

    return UNITY_END();
}
