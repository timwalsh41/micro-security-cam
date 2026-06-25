#include <Arduino.h>
#include "hal/time_hal.h"
#include "hal/camera_hal_impl.h"
#include "camera_controller.h"
#include "time_service.h"
#include "peripheral_manager.h"
#include "utility.h"

using hal::millis_t;
using utility::print_with_timestamp;

namespace constants
{
  constexpr millis_t LED_ON_TIME_MS = 1000;
  constexpr millis_t CAMERA_CAPTURE_INTERVAL_MS = 20000;
  constexpr millis_t PERIPHERAL_MANAGER_TICK_TIME_MS = 5;
}

millis_t last_capture_time = 0;         // Last image time
uint32_t image_count = 1;               // File counter

RealTimeService real_time;
PeripheralManager peripheral_manager;
hal::CameraHalImpl camera_hal;
CameraController camera_ctrl{camera_hal};
millis_t last_peripheral_tick_time_ms = 0;

void take_photo(const char * file_name)
{
  CameraCaptureResult result = camera_ctrl.capture_frame();
  if (result != CameraCaptureResult::SUCCESS)
  {
    print_with_timestamp("Failed to capture frame");
    return;
  }

  hal::CameraFrame frame = camera_ctrl.get_frame();
  peripheral_manager.write_file(file_name, frame.data, frame.len, 1000u);
  camera_ctrl.release_frame();

  print_with_timestamp("Photo saved to file");
}

void setup()
{
  Serial.begin(115200);
#ifndef DEBUG
  while(!Serial); // When the serial monitor is turned on, the program starts to execute
#endif

  // add delay for PlatformIO terminal to connect
  delay(5000);

  print_with_timestamp("Starting initialization");

  peripheral_manager.init();

  peripheral_manager.blink(250, 250);
  {
    auto now = real_time.now();
    
    while (real_time.now() < (now + 3000))
    {
      peripheral_manager.tick();
      delay(constants::PERIPHERAL_MANAGER_TICK_TIME_MS);
    }
  }
  peripheral_manager.turn_led_off();

  hal::CameraInitResult cam_result = camera_ctrl.init();
  if (cam_result != hal::CameraInitResult::SUCCESS)
  {
    print_with_timestamp("Camera initialization failed");
    return;
  }
  print_with_timestamp("Camera initialized");

  if (peripheral_manager.init_sd_card() != hal::SdInitResult::SUCCESS)
  {
    print_with_timestamp("SD initialization failed");
    return;
    
  }
  print_with_timestamp("SD initialized");
}

void loop() {
  if(camera_ctrl.is_initialized() && peripheral_manager.is_sd_card_initialized())
  {
    auto now = real_time.now();

    auto elapsed_ms = TimeService::elapsed_ms(last_peripheral_tick_time_ms, now);
    if (elapsed_ms >= constants::PERIPHERAL_MANAGER_TICK_TIME_MS)
    {
      peripheral_manager.tick();
      last_peripheral_tick_time_ms = now;
    }

    if ((now - last_capture_time) >= constants::CAMERA_CAPTURE_INTERVAL_MS)
    {
      peripheral_manager.pulse_led(constants::LED_ON_TIME_MS);
      last_capture_time = now;
      print_with_timestamp("Capturing image...");

      char filename[32];
      sprintf(filename, "/image%d.jpg", image_count);
      take_photo(filename);
      
      char logstring[128];
      sprintf(logstring, "Saved picture: %s\r\n", filename);
      print_with_timestamp(logstring);
      
      image_count++;
    }
    else if (now % 5000 == 0)
    {
      print_with_timestamp("Looping...");
      delay(2);
    }
  }
}
