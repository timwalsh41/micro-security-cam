#ifndef CAMERA_RUNNER_H
#define CAMERA_RUNNER_H

/**
 * @file camera_runner.h
 * @brief Platform-agnostic singleton that owns all application objects.
 *
 * CameraRunner is a singleton that encapsulates the entire application
 * initialization and loop logic. It owns all service objects, HAL
 * implementations, and the HardwarePeripheralManager as private members.
 *
 * This header contains no FreeRTOS includes and no hardware-specific code;
 * all platform-specific code lives in the implementation file.
 *
 * Usage:
 *   void setup() { CameraRunner::init(); }
 *   void loop()  { CameraRunner::loop(); }
 */

/**
 * @class CameraRunner
 * @brief Singleton that owns all application objects and drives setup/loop.
 *
 * This is a code organization change: all global objects and setup/loop
 * logic that previously lived in main.cpp are now encapsulated here.
 * The class is not testable — it is a pure composition root.
 */
class CameraRunner
{
public:
    /**
     * @brief Initializes the application.
     *
     * Performs all hardware initialization, service wiring, and
     * startup sequence as defined in the original setup() function.
     */
    static void init();

    /**
     * @brief Runs one iteration of the application loop.
     *
     * Polls command transports, ticks the system orchestrator,
     * updates the LED state machine, and logs a periodic heartbeat.
     */
    static void loop();

private:
    CameraRunner() = delete;
    CameraRunner(const CameraRunner&) = delete;
    CameraRunner& operator=(const CameraRunner&) = delete;
};

#endif // CAMERA_RUNNER_H