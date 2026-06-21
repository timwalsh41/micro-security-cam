# PeripheralManager Refactoring Design

## Problem Statement

`PeripheralManager` is a monolithic class mixing three concerns:
1. **LED state machine** (on/off/pulse/blink)
2. **SD card write controller** with CS timing protocol
3. **Hardware pin manipulation** (`digitalWrite`, `pinMode`, SD library calls)

This makes unit testing on native impossible and violates single-responsibility.

## Goals

- Decompose into small, single-responsibility classes
- Enable **unit tests on native** for all pure logic
- Keep hardware-specific code in a thin HAL layer (ESP32 only)

---

## New Class Structure

```
include/
  led_state_machine.h      ← Pure LED state machine (testable on native)
  sd_write_controller.h    ← SD write lifecycle + CS timing protocol (testable on native)
  pin_arbitrator.h         ← Resolves shared-pin conflicts (testable on native)
  time_service.h           ← Abstraction over millis() + elapsed-time math (testable on native)
  hal/                     ← Hardware abstraction layer folder
    gpio_hal.h             ← GPIO read/write/pinmode (ESP32 only)
    sd_card_hal.h          ← SD init/open/write/close interface + impl (ESP32 only)
    time_hal.h             ← millis() wrapper (both ESP32 and native)

src/
  led_state_machine.cpp    ← LED state machine implementation (pure logic)
  sd_write_controller.cpp  ← SD write controller implementation (delegates to HAL for I/O)
  pin_arbitrator.cpp       ← Pin arbitration logic (pure)
  time_service.cpp         ← Time service implementation
  hal/                     ← HAL implementations (ESP32 only)
    gpio_hal.cpp           → digitalWrite, pinMode wrappers
    sd_card_hal.cpp        → SD.begin(), file ops wrapper (uses SD.h)
    time_hal.cpp           → millis() forwarder

test/                     ← All native unit tests (Unity)
  test_time_service.cpp         ← elapsed_ms, rollover handling
  test_led_state_machine.cpp    ← all modes: OFF, ON, PULSE_ON, BLINK
  test_sd_write_controller.cpp  ← state transitions + timing protocol (mocked HAL)
  test_pin_arbitrator.cpp       ← shared-pin resolution logic

src/main.cpp              ← Updated to use new composition
```

---

## Class Specifications

### 1. `TimeService` (`include/time_service.h`)

**Responsibility**: Abstract time acquisition and elapsed-time calculations.

```cpp
class TimeService {
public:
    virtual ~TimeService() = default;

    // Returns current time in milliseconds (from HAL)
    virtual uint32_t now() = 0;

    // Calculates elapsed time, handling rollover correctly (pure math)
    static uint32_t elapsed_ms(uint32_t since, uint32_t now);
};

// Concrete: delegates to hal::millis()
class RealTimeService : public TimeService { ... };

// Mock: returns configurable value for testing
class MockTimeService : public TimeService { ... };
```

### 2. `LedStateMachine` (`include/led_state_machine.h`)

**Responsibility**: Pure LED state machine logic. Produces desired output based on mode and timing.

```cpp
enum class LedMode : uint8_t { OFF, ON, PULSE_ON, BLINK };

class LedStateMachine {
public:
    explicit LedStateMachine(TimeService &time);

    void set_mode(LedMode mode, uint32_t on_ms = 0, uint32_t off_ms = 0);

    // Returns desired LED state (true=on, false=off). No side effects.
    bool tick();

private:
    LedMode m_mode{LedMode::OFF};
    uint32_t m_on_duration_ms;
    uint32_t m_off_duration_ms;
    // internal blink/pulse phase tracking...
};
```

### 3. `SdWriteController` (`include/sd_write_controller.h`)

**Responsibility**: Manages SD write lifecycle with proper CS timing protocol. Owns the state machine:
`IDLE → START_WRITE → PRE_DEACTIVATE_CS → ACTIVATE_CS → WRITING → STOP_WRITE → POST_DEACTIVATE_CS → IDLE`

```cpp
// Abstract interface for SD hardware operations (mockable)
class SdCardHal {
public:
    virtual ~SdCardHal() = default;
    virtual SdInitResult init() = 0;

    // File ops — contract: must only be called when CS is asserted by the
    // SdWriteController.  This is a runtime protocol enforced by the
    // controller's state machine, not a compile-time constraint.
    virtual bool open(const char *path) = 0;
    virtual bool write(const uint8_t *data, size_t len) = 0;
    virtual void close() = 0;
};

class SdWriteController {
public:
    explicit SdWriteController(TimeService &time, SdCardHal &sd_hal);

    // Returns desired CS pin state (true=asserted/LOW, false=de-asserted/HIGH)
    bool tick();

    void start_write(const char *path, uint8_t *data, size_t len);
    SdWriteResult write_synchronous(const char *path, uint8_t *data, size_t len,
                                     uint32_t timeout_ms);

    SdInitResult init_card();

private:
    // State machine for SD write protocol...
};
```

### 4. `PinArbitrator` (`include/pin_arbitrator.h`)

**Responsibility**: Resolves conflicts when LED and SD-CS share the same physical pin. On this board, both LED-on and CS-active map to `LOW`. The CS can be idle, in which case the LED request is granted, or CS can force the pin active or inactive, overriding the LED request.

```cpp

enum class SdSignal : uint8_t {
    IDLE,           // SD doesn't care; LED wins
    CS_ACTIVE,      // SD needs CS LOW (during activation/writing)
    CS_INACTIVE     // SD needs CS HIGH (during deactivation timing)
};

class PinArbitrator {
public:
    // Returns true if pin should be driven LOW.
    static bool resolve(bool led_wants_on, SdSignal sd_signal);

private:
    // Both active states are LOW → OR logic suffices.
};
```

### 5. HAL Layer (`include/hal/`, `src/hal/`) — ESP32 Only

Thin wrappers around Arduino/ESP32 hardware APIs. No logic, just forwarding.

| File | Responsibility |
|------|----------------|
| `gpio_hal.h/cpp` | `pinMode()`, `digitalWrite()` wrappers |
| `sd_card_hal.h/cpp` | Concrete `SdCardHalImpl` using SD library (`SD.begin()`, file ops) |
| `time_hal.h/cpp` | Forwards to Arduino `millis()` (ESP32) or mock (native) |

### 6. Refactored `PeripheralManager` (`include/peripherals.h`)

**Responsibility**: Composition root / facade. Wires everything together for the ESP32 environment. No logic of its own — all behavior delegated to sub-components.

```cpp
class PeripheralManager {
public:
    void init();          // initializes HAL, LED state machine to OFF

    SdInitResult init_sd_card();
    void tick();          // ticks both state machines, arbitrates pin

    // LED convenience methods
    void turn_led_on();
    void turn_led_off();
    void pulse_led(uint32_t ms);
    void blink(uint32_t on_ms, uint32_t off_ms);

    // SD convenience methods
    SdWriteResult write_file(const char *path, uint8_t *data, size_t len,
                              uint32_t timeout_ms);

private:
    RealTimeService m_time;
    LedStateMachine  m_led{m_time};
    SdCardHalImpl    m_sd_hal;
    SdWriteController m_sd{m_time, m_sd_hal};
};
```

---

## Unit Test Plan (All Native — Unity)

### `test/test_time_service.cpp`
| Test | Description |
|------|-------------|
| `elapsed_ms_normal_subtraction` | now > since, simple subtraction |
| `elapsed_ms_zero_elapsed` | now == since returns 0 |
| `elapsed_ms_rollover_near_max` | since near UINT32_MAX, now small positive |
| `elapsed_ms_rollover_exact_wrap` | since = UINT32_MAX, now = 0 → elapsed = 1 |

### `test/test_led_state_machine.cpp`
| Test | Description |
|------|-------------|
| `mode_off_returns_false_always` | OFF mode always returns false |
| `mode_on_returns_true_always` | ON mode always returns true |
| `pulse_turns_off_after_duration` | PULSE_ON transitions to OFF after on_ms elapses (mock time) |
| `pulse_stays_on_before_duration` | PULSE_ON stays true before on_ms elapses |
| `blink_cycles_on_and_off` | BLINK alternates true/false at correct intervals (advance mock time) |
| `blink_custom_durations` | BLINK with asymmetric on/off durations produces correct pattern |

### `test/test_sd_write_controller.cpp`
| Test | Description |
|------|-------------|
| `idle_returns_cs_inactive` | IDLE state returns false (CS de-asserted) |
| `start_write_transitions_to_pre_deactivate` | start_write() moves to PRE_DEACTIVATE_CS, returns false |
| `pre_deactivate_to_activate_after_delay` | After DEACTIVATE_TIME_MS, transitions to ACTIVATE_CS (returns true) |
| `activate_to_writing_after_delay` | After ACTIVATE_TIME_MS, transitions to WRITING (returns true) |
| `writing_performs_io_and_moves_to_stop` | WRITING calls HAL write, then transitions to STOP_WRITE (returns true) |
| `stop_write_transitions_to_post_deactivate` | STOP_WRITE moves to POST_DEACTIVATE_CS (returns false) |
| `post_deactivate_to_idle_after_delay` | After DEACTIVATE_TIME_MS, returns to IDLE (returns false) |
| `full_write_cycle_state_sequence` | Full tick sequence produces correct state progression and CS values |
| `synchronous_write_returns_success` | write_synchronous() completes with SUCCESS on mock HAL success |
| `synchronous_write_returns_timeout` | write_synchronous() returns TIMEOUT when tick loop exceeds timeout_ms |

### `test/test_pin_arbitrator.cpp`
| Test | Description |
|------|-------------|
| `both_off_idle_returns_false` | LED off, SD idle → HIGH (false) |
| `led_on_idle_returns_true` | LED on, SD idle → LOW (true) |
| `cs_active_overrides_led_off` | LED off, SD CS_ACTIVE → LOW (true, SD overrides) |
| `cs_active_overrides_led_on` | LED on, SD CS_ACTIVE → LOW (true, SD overrides) |
| `cs_inactive_overrides_led_off` | LED off, SD CS_INACTIVE → HIGH (false, SD overrides) |
| `both_active_returns_true` | Both want pin active → LOW (true, no conflict) |

---

## Build Configuration Changes (`platformio.ini`)

```ini
[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
lib_deps = espressif/esp32-camera@^2.0.4
monitor_speed = 115200

[env:native]
platform = native
build_flags =
    -D UNIT_TESTING
    -I include
    -I test
build_src_filter = -<*> +<utility.cpp> +<time_service.cpp> +<led_state_machine.cpp>
    +<sd_write_controller.cpp> +<pin_arbitrator.cpp>
test_build_src = yes

[common_test_flags]
build_flags = -I include -I test
```

---

## Implementation Order (Recommended)

1. **`TimeService`** — foundation; no dependencies
2. **`PinArbitrator`** — trivial pure logic, quick win for test coverage
3. **`LedStateMachine`** — moderate complexity; depends on `TimeService`
4. **`SdCardHal` interface + mock** — define contract before implementation
5. **`SdWriteController`** — most complex; depends on `TimeService`, `SdCardHal`
6. **HAL implementations** — thin wrappers, ESP32-only; no native tests needed
7. **Refactored `PeripheralManager`** — composition root, wires everything together
8. **Update `main.cpp`** — adapt to new API surface

---

## Key Design Decisions & Rationale

| Decision | Rationale |
|----------|-----------|
| `TimeService` injected by reference, not owned | Enables mock injection for testing; real impl owns HAL call |
| `elapsed_ms` is static on `TimeService`, not instance method | Pure math with no HAL dependency — maximally testable anywhere |
| `SdCardHal` file ops are public (not protected) | CS assertion is a runtime protocol enforced by `SdWriteController`'s state machine, not a compile-time constraint. `protected` + `friend` would couple the interface to its consumer without solving the real problem |
| `PinArbitrator` is static/pure, not a class with state | No internal state needed; OR logic over two booleans is trivially pure |
| HAL layer in separate `hal/` subfolder | Clear visual and namespace boundary between logic and hardware code |
| Keep `PeripheralManager` as public facade | Main app still has a single entry point; internal structure is opaque |
