# ESP32-S3 Debugging Guide

## Debug environment configuration

The `[env:seeed_xiao_esp32s3_debug]` environment in `platformio.ini` extends the base build with:

- **`debug_tool = esp-builtin`** — uses the ESP32-S3's built-in USB-JTAG (no external probe)
- **`debug_build_flags = -O0 -g3`** — disables optimization and emits full debug symbols
- **`build_flags` additions** — `ARDUINO_USB_MODE=1` and `ARDUINO_USB_CDC_ON_BOOT=1` for USB composite mode (CDC serial + JTAG on the USB-C port)

Build with: `pio run -e seeed_xiao_esp32s3_debug`

## Architectural limitations

### Hardware breakpoints: 2 per CPU only

- The Xtensa LX7 cores in ESP32-S3 support only **2 hardware breakpoints per CPU** (plus 1 reserved for single-stepping by the debugger itself)
- **Software breakpoints (`break` / `bkpt` instruction) do not work in flash** — Xtensa cannot write to flash at runtime to insert the breakpoint instruction. Software breakpoints only work in RAM (stack, heap, `.data`/`.bss`)
- Since our application code executes from flash, **every `break` or `tbreak` consumes one of the 2 hardware breakpoint slots on the current target CPU**

### Workaround: infinite loop as a software "breakpoint"

When hardware breakpoint slots are exhausted or you need more than 2 simultaneous stop points, insert a volatile infinite loop at the desired location in code:

```cpp
void setup()
{
  Serial.begin(115200);
  while(!Serial);
  delay(5000);

  // DEBUG TRAP: volatile infinite loop acts as a breakpoint
  volatile int trap = 1;
  while (trap) { }
```

When execution hits the `while (trap) { }` loop, the target stops incrementing the PC. In GDB, you can then:

1. Use `Ctrl+C` if GDB hasn't already detected the halt (it usually will since the target is stuck)
2. Inspect variables, step, etc.
3. Release the trap with: `set trap = 0` then `continue`

The trap variable lives in RAM (it's `volatile`), so GDB can modify it. This technique:
- Uses **zero hardware breakpoints**
- Works regardless of which CPU the task is pinned to
- Can be placed at multiple points (as many as you want, just rename the variable)
- Is removed by simply deleting the trap lines after debugging (or guarding with `#ifdef DEBUG_TRAP`)

## Command-line debug workflow

Prerequisite: firmware must be built and already flashed to the board (e.g. via `pio run -e seeed_xiao_esp32s3_debug --target upload`).

### 1. Start OpenOCD

```bash
~/.platformio/packages/tool-openocd-esp32/bin/openocd \
  -s ~/.platformio/packages/tool-openocd-esp32/share/openocd/scripts \
  -f interface/esp_usb_jtag.cfg \
  -c "adapter speed 20000" \
  -f target/esp32s3.cfg
```

OpenOCD listens on TCP port 3333 (GDB) and 4444 (telnet).

### 2. Start GDB and connect

In a separate terminal:

```bash
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gdb \
  .pio/build/seeed_xiao_esp32s3_debug/firmware.elf
```

### 3. GDB commands for a debug session

```
(gdb) target remote :3333
(gdb) monitor targets esp32s3.cpu1
(gdb) monitor halt
(gdb) break setup
(gdb) break loop
(gdb) continue
```

**Explanation of each step:**

| Command | Purpose |
|---------|---------|
| `target remote :3333` | Connect to OpenOCD's GDB server |
| `monitor targets esp32s3.cpu1` | **Switch to CPU1** before anything else — `setup()`/`loop()` run on CPU1 |
| `monitor halt` | Halt CPU1 cleanly (avoids the dual-core race condition from `reset halt`) |
| `break setup` / `break loop` | Set hardware breakpoints on CPU1 where your code actually runs |
| `continue` | Let the target run — breakpoints will fire when hit |

### 4. When a breakpoint hits

You'll see in the OpenOCD terminal: `[esp32s3.cpu1] Target halted, PC=0x4200XXXX, debug_reason=00000001`

In GDB you can then:

- `print variable_name` — inspect a variable
- `info locals` — all local variables
- `info args` — function arguments
- `step` — step into function call
- `next` — step over function call
- `continue` — resume execution
- `set var trap = 0` — modify a variable (useful for the infinite-loop trap)

### 5. Automated GDB script

Save as `debug_cpu1.gdb`:

```
target remote :3333
monitor targets esp32s3.cpu1
monitor halt
break setup
break loop
continue
```

Run with: `xtensa-esp32s3-elf-gdb -x debug_cpu1.gdb .pio/build/seeed_xiao_esp32s3_debug/firmware.elf`

### 6. Quick checks via telnet

```bash
# Reset and halt both cores
echo -e "reset halt\nexit" | nc -w 5 localhost 4444

# Check which CPU targets are available and their current state
echo -e "targets\nexit" | nc -w 5 localhost 4444
```

## Debugging via VS Code / PlatformIO

**Before your first debug session, switch PlatformIO to the `seeed_xiao_esp32s3_debug` environment** - PlatformIO updates launch configurations (in `launch.json`) accordingly.

### Launch configurations

| Name | Builds? | Uploads? | Use when |
|------|---------|----------|----------|
| **PIO Debug** | ✅ | ✅ | First session or after code changes |
| **PIO Debug (skip Pre-Debug)** | ❌ | ❌ | Quick reconnect — code hasn't changed |
| **PIO Debug (without uploading)** | ✅ | ❌ (manual) | You want to decide when to upload |

**Switch to the `DEBUG CONSOLE` view** in the bottom pane to observe the startup process, which takes 10-20 seconds.

### What VS Code handles automatically

- **OpenOCD launch, firmware upload, and GDB connection** — clicking the debug button (`F5`) orchestrates all three. No manual terminal juggling needed
- **Breakpoint UI** — click the editor gutter to set/clear breakpoints
- **Variable watch** — hover over variables or add them to the Watch panel
- **Call stack** — the Run & Debug sidebar shows the stack trace
- **Thread switching** — the Threads panel lists all FreeRTOS threads; click to switch

### CPU targeting is now handled automatically

The `debug_extra_cmds` in `platformio.ini` automatically switches GDB to CPU1 and sets breakpoints on `setup()` and `loop()` before the target resumes. This means you should be able to set breakpoints in the editor gutter and have them fire on the first try.

If breakpoints still don't fire (possible if PlatformIO's internal OpenOCD initialization sequence differs from the manual workflow):

1. Open the **Threads** panel in the Debug sidebar
2. Select `loopTask` or `setupTask` — this manually switches GDB to CPU1
3. The breakpoints will now apply to the correct core

### If you see "target not halted" errors

The dual-core race condition can still occur via the PlatformIO path. If you hit this:
- Stop the debug session, start it again with **PIO Debug (skip Pre-Debug)** — a retry often works
- Or fall back to the [command-line workflow](#command-line-debug-workflow) which gives you full control over the halt/resume sequence
