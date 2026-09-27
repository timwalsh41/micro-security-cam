# Overview

- This repo is a still photo camera based on an off the shelf Seeed XIAO ESP32S3 Sense with included camera and SD card
- The objective of the project is to create a simple demo showing off these capabilities, while creating some reusable software modules that can be leveraged for more complex systems base on this board
- We use the Arduino framework and libraries for development

# Environment

- We use PlatformIO for environment management (BSP, libraries, build, test).
    - Refer to `platformio.ini` for details on the environment setup and included libraries
- To use the PlatformIO CLI (`platformio`/`pio` commands), you'll need to source the environment with the command `source ~/.platformio/penv/bin/activate`
    - To invoke `pio` commands, no need to change directory, just run as a compound command, e.g. `source ~/.platformio/penv/bin/activate && pio test -e native`
    - Do not execute tool calls with `requestAllowNetwork=true` by default - in the environment settings are whitelisted domains that will be required for PlatformIO command calls, if we hit a blocked domain we'll add it to the whitelist; only use `requestAllowNetwork=true` if truly needed
- Use terminal commands to read files outside of the project folder
- We define three PlatformIO environments:
    - `seeed_xiao_esp32s3` for the embedded target; this environment is used to build the application binary
    - `seeed_xiao_esp32s3_test` for the embedded target; this environment is used to build and run certain unit tests
    - `native` for the host dev machine; this is used for running certain unit tests
    - New unit tests should be segregated in the appropriate subfolder under `test/` so that they run in the appropriate environment given the `test_filter` setting for each environment in `platformio.ini`
- Unit testing
    - Leverage the unity framework
    - Unit tests should be segregated into subfolders under the appropriate test folder (`native` or `hardware`), and named with a `test_` prefix
        - Each subfolder represents a single test suite, typically a single implementation file with a set of tests defined and then gathered within one `main()` function
        - If there is reason to do so, the tests could be broken out across multiple implementation files, but still need to be gathered and executed in a single `main()`
    - To run a specific test suite, use the `-f` option and specify the path to the test suite relative to the `test/` folder, e.g. `pio test -e native -f native/test_sd_write_controller`
- Use `uv` for Python package management and script execution

# Development guidelines

- When making technical decisions, don’t give much something interesting that you may weight to development cost
- When doing bug fixes always start by reproducing the bug in an end to end test setting as closely aligned to real user workflow as possible
- **Stop and wait for explicit approval** before implementing changes requested via  plan execution prompts or similar structured workflows. Request approval to proceed and wait for the user's affirmative response.
- C++11 standard
- Include unit test coverage for all new code
    - Logical/control level components should be tested in the `native` environment
    - Components that interface directly with hardware should be tested in the `seeed_xiao_esp32s3_test` environment
    - Mocks and other utilities shared across test suites should be placed in the appropriate subfolder in `lib/`
- Use namespaces liberally to group like functionality
- Follow the single responsibility principle when designing classes
- Use interface classes where appropriate to make dependencies injectable for testing purposes
- Do not isolate platform-specific code from platform-agnostic logical/service/controller code using #ifdef guards (`#ifdef UNIT_TESTING`) to exlude it from native unit tests; rather, any platform specific code should exist in a separate module with an appropriate interface and not included in native test build
- Include what you use - if a header or implementation file uses another header's definitions then include it directly as opposed to relying on indirect transitive inclusion
- Use `#ifdef` guards on all headers
- Prefer 3rd party libraries to building functionality from scratch
- Create Javadoc style docstrings for classes and functions
- Verify `native` environment tests pass and `seeed_xiao_esp32s3` environment build succeeds

# Style

- Class/struct names use `PascalCase`
- Enum type definitions use `PascalCase`, and entries use `CAPITAL_SNAKE_CASE`
- Function names use `snake_case`
- Variable names use `snake_case`
    - Member variables are prefixed with `m_`
- Constants use `CAPITAL_SNAKE_CASE`
- File names are in `snake_case.cpp`
- Use Allman-style braces
- Include a space between keywords and parentheses, e.g. `if (...)` not `if(...)`
- Align initializer list commas with the initial colon on new lines for each entry, e.g.:
  ```c++
  MyClass MyClass(int arg1, double arg2)
  : m_int1(arg1)
  , m_double1(arg2)
  ```
- Add ampersand/asterisk on the type for pointers/references rather than the parameter/variable, e.g. `MyType& var1`, not `MyType &var1`
- In classes and structs, indent the access specifier relative to the outer braces for the class definition, and
  then indent the member variables and methods relative to the access specifier, e.g.
  ```c++
  class MyClass
  {
      public:
          void my_method();

      private:
          int m_my_variable;  
  };
  ```
- Include function briefs for each individual test function in unit test files

# Documentation

- `SYSTEM_DESIGN.md` is our main architectural documentation that describes the software component design and composition; keep this up to date as changes are made impacting the design
    - Be concise but comprehensive when editing this file - review it in its entirety to ensure no redundant information is added
    - This is a record of the design architecture, NOT a record of the implementation history - do not refer to how a feature or implementation has changed, or what plan or project phase features were introduced or modifed, just document the current state
    - Do not call out the number of tests per module, this is a number that may vary over time, does not add useful information to the user understanding at this level of abstraction, and is prone to get out of date.
- Use PlantUML to render UML diagrams (class diagrams, sequence diagrams, etc.)
- In documentation markdowns, enclose all symbols and filenames in `backticks`

# Other

- Use `hal::millis_t` from `hal/time_hal.h` for millisecond time type
- All SD card file I/O has to go through peripheral manager as that governs arbitration/sharing of the common CS/LED pin
- In the comments for a module, minimize comments referencing other modules, that reduces reusability; any cross-module integration should be documented in `SYSTEM_DESIGN.md`
- Similarly, minimize comments that reference specific hardware configuration details that are not relevant at that level of abstraction; for example, the `IniParser` does not need to reference that a config file is read from the SD card
- Do not modify `TODO.md` - that is user only
- Prioritize retrieval-led reasoning over pretrained-knowledge-led reasoning

# End-to-end suite

- `test/e2e/` (pytest, run via `script/test-e2e.sh`) is the real-hardware regression suite. Update it as features are added: coverage must stay complementary to the unit test coverage, not duplicate it.
- The suite shares the camera HTTP transport with the CLI (`script/python/camclient.py`); keep that module the single transport definition when endpoints change.

## Maintaining this file

Keep this file for knowledge useful to almost every future agent session in this project.
Do not repeat what the codebase already shows; point to the authoritative file or command instead.
Prefer rewriting or pruning existing entries over appending new ones.
When updating this file, preserve this bar for all agents and keep entries concise.
