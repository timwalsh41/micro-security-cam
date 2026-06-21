# Overview

- This repo is a still photo camera based on an off the shelf Seeed XIAO ESP32S3 Sense with included camera and SD card
- The objective of the project is to create a simple demo showing off these capabilities, while creating some reusable software modules that can be leveraged for more complex systems base on this board
- We use the Arduino framework and libraries for development

# Environment

- We use PlatformIO for environment management (BSP, libraries, build, test).
    - Refer to `platformio.ini` for details on the environment setup and included libraries
- To use the PlatformIO CLI (`platformio`/`pio` commands), you'll need to source the environment with the command `source ~/.platformio/penv/bin/activate`
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

# Development guidelines

- C++11 standard
- Include unit test coverage for all new code
    - Logical/control level components should be tested in the `native` environment
    - Components that interface directly with hardware should be tested in the `seeed_xiao_esp32s3_test` environment
    - Mocks and other utilities shared across test suites should be placed in the appropriate subfolder in `lib/`
- Use namespaces liberally to group like functionality
- Follow the single responsibility principle when designing classes
- Use interface classes where appropriate to make dependencies injectable for testing purposes
- Use `#ifdef` guards on all headers
- Create Javadoc style docstrings for classes and functions

# Style

- Class/struct names use `PascalCase`
- Enum type definitions use `PascalCase`, and entries use `CAPITAL_SNAKE_CASE`
- Function names use `snake_case`
- Variable names use `snake_case`
    - Member variables are prefixed with `m_`
- Constants use `CAPITAL_SNAKE_CASE`
- File names are in `snake_case.cpp`
- Use Allman-style braces