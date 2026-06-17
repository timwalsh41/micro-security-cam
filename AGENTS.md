# Overview

- This repo is a still photo camera based on an off the shelf Seeed XIAO ESP32S3 Sense with included camera and SD card
- The objective of the project is to create a simple demo showing off these capabilities, while creating some reusable software modules that can be leveraged for more complex systems base on this board
- We use the Arduino framework and libraries for development

# Environment

- We use PlatformIO for environment management (BSP, libraries, build, test).
    - Refer to `platformio.ini` for details on the environment setup and included libraries
- We define two PlatformIO environments:
    - `seeed_xiao_esp32s3` for the embedded target; this is used both for deploying the application code, as well as running certain unit tests
    - `native` for the host dev machine; this is used for running certain unit tests
    - New unit tests should be segregated in the appropriate subfolder under `test/` so that they run in the appropriate environment given the `test_filter` setting for each environment in PlatformIO
- Unit testing
    - Leverage the unity framework

# Development guidelines

- C++11 standard
- Include unit test coverage for all new code
    - Logical/control level components should be tested in the 
- Use namespaces liberally to group like functionality
- Follow the single responsibility principle when designing classes
- Use interface classes where appropriate to make dependencies injectable for testing purposes
- Create Javadoc style docstrings for classes and functions

# Style

- Class/struct names use `PascalCase`
- Enum type definitions use `PascalCase`, and entries use `CAPITAL_SNAKE_CASE`
- Function names use `snake_case`
- Variable names use `snake_case`
    - Member variables are prefixed with `m_`
- Constants use `CAPITAL_SNAKE_CASE`
