#include "hal/time_hal.h"
#include "hal/camera_hal_impl.h"
#include "hal/serial_log_backend.h"
#include "hal/sd_card_hal_impl.h"
#include "camera_controller.h"
#include "time_service.h"
#include "logging_service.h"
#include "peripheral_manager.h"
#include "config_service.h"

using hal::millis_t;

namespace constants
{
  constexpr millis_t LED_ON_TIME_MS = 1000;
  constexpr millis_t DEFAULT_CAMERA_CAPTURE_INTERVAL_MS = 20000;
  constexpr millis_t PERIPHERAL_MANAGER_TICK_TIME_MS = 5;
}

millis_t last_capture_time_ms = 0;         // Last image time
uint32_t image_count = 1;               // File counter

RealTimeService real_time;
hal::SerialLogBackend log_backend;
LoggingService logger{log_backend, real_time};
ConfigService config_service{logger};
hal::SdCardHalImpl sd_hal{SD_CS};
PeripheralManager peripheral_manager{real_time, sd_hal};
hal::CameraHalImpl camera_hal;
CameraController camera_ctrl{camera_hal};
millis_t last_peripheral_tick_time_ms = 0;
millis_t camera_capture_interval_ms = constants::DEFAULT_CAMERA_CAPTURE_INTERVAL_MS;

void take_photo(uint32_t image_count)
{
  CameraCaptureResult result = camera_ctrl.capture_frame();
  if (result != CameraCaptureResult::SUCCESS)
  {
    logger.error("CAMERA", "Failed to capture frame");
    return;
  }

  hal::CameraFrame frame = camera_ctrl.get_frame();

  char file_name[32];
  snprintf(file_name, sizeof(file_name), "/image%u.jpg", image_count);
  peripheral_manager.write_file(file_name, frame.data, frame.len, 1000u);
  camera_ctrl.release_frame();

  logger.info("CAMERA", "Saved picture: %s", file_name);
}

void setup()
{
  // add delay for PlatformIO terminal to connect
  real_time.delay_ms(5000);

  logger.info("MAIN", "Starting initialization");

  peripheral_manager.init();
  peripheral_manager.blink(250, 250);
  {
    auto now = real_time.now();

    while (real_time.now() < (now + 3000))
    {
      peripheral_manager.tick();
      real_time.delay_ms(constants::PERIPHERAL_MANAGER_TICK_TIME_MS);
    }
  }
  peripheral_manager.turn_led_off();

  hal::CameraInitResult cam_result = camera_ctrl.init();
  if (cam_result != hal::CameraInitResult::SUCCESS)
  {
    logger.error("MAIN", "Camera initialization failed");
    return;
  }
  logger.info("MAIN", "Camera initialized");

  if (peripheral_manager.init_sd_card() != hal::SdInitResult::SUCCESS)
  {
    logger.error("MAIN", "SD initialization failed");
    return;
  }
  logger.info("MAIN", "SD initialized");

  // Load config from SD card
  uint8_t config_buf[ConfigService::CONFIG_BUFFER_SIZE];
  size_t bytes_read = 0;
  peripheral_manager.read_file(ConfigService::DEFAULT_CONFIG_PATH,
                               config_buf, sizeof(config_buf),
                               &bytes_read, ConfigService::READ_TIMEOUT_MS);
  logger.info("MAIN", "Config read: %zu / %zu bytes",
              bytes_read, ConfigService::CONFIG_BUFFER_SIZE);
  config_service.load(config_buf, bytes_read);

  // Apply config values
  if (config_service.config().camera.capture_interval_ms > 0)
  {
    camera_capture_interval_ms = config_service.config().camera.capture_interval_ms;
    logger.info("MAIN", "Capture interval: %u ms", camera_capture_interval_ms);
  }
}

void loop()
{
  if (camera_ctrl.is_initialized() && peripheral_manager.is_sd_card_initialized())
  {
    auto now = real_time.now();

    if (TimeService::elapsed_ms(last_peripheral_tick_time_ms, now) >= constants::PERIPHERAL_MANAGER_TICK_TIME_MS)
    {
      peripheral_manager.tick();
      last_peripheral_tick_time_ms = now;
    }

    if (TimeService::elapsed_ms(last_capture_time_ms, now) >= camera_capture_interval_ms)
    {
      peripheral_manager.pulse_led(constants::LED_ON_TIME_MS);
      last_capture_time_ms = now;
      logger.info("MAIN", "Capturing image...");
      take_photo(image_count);
      image_count++;
    }
    else if (now % 5000 == 0)
    {
      logger.info("MAIN", "Looping...");
      real_time.delay_ms(2);
    }
  }
}
