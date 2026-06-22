#include "esp_camera.h"

#include <Arduino.h>
#include "SPI.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#include "camera_pins.h"
#include "peripheral_manager.h"
#include "utility.h"

namespace constants
{
  constexpr utility::time_t LED_ON_TIME_MS = 1000;
  constexpr utility::time_t CAMERA_CAPTURE_INTERVAL_MS = 20000;
  constexpr utility::time_t PERIPHERAL_MANAGER_TICK_TIME_MS = 5;
}

using utility::print_with_timestamp;

unsigned long lastCaptureTime = 0;    // Last shooting time
int imageCount = 1;                   // File Counter
bool camera_available = false;        // Check camera status
bool sd_available = false;            // Check sd status

PeripheralManager peripheral_manager;
utility::time_t last_peripheral_tick_time_ms = 0;

// Save pictures to SD card
void photo_save(const char * fileName) {
  // Take a photo
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    print_with_timestamp("Failed to get camera frame buffer");
    return;
  }

  // Save photo to file
  peripheral_manager.write_file(fileName, fb->buf, fb->len, 1000u);
  
  // Release image buffer
  esp_camera_fb_return(fb);

  print_with_timestamp("Photo saved to file");
}

void camera_pin_setup(camera_config_t &config) {
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;
}

void setup() {
  Serial.begin(115200);
  while(!Serial); // When the serial monitor is turned on, the program starts to execute

  // add delay for PlatformIO terminal to connect
  delay(5000);

  print_with_timestamp("Starting initialization");

  peripheral_manager.init();

  camera_config_t config;
  camera_pin_setup(config);
  
  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if(config.pixel_format == PIXFORMAT_JPEG){
    if(psramFound()){
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  print_with_timestamp("Camera initialized");
  camera_available = true; // Camera initialization check passes

  sd_available = (peripheral_manager.init_sd_card() == hal::SdInitResult::SUCCESS);

  if (sd_available)
    // SD initialization check passes
    print_with_timestamp("SD initialization succeeded");
  else
    print_with_timestamp("SD initialization failed");

  peripheral_manager.blink(250, 750);
}

void loop() {
  if(camera_available && sd_available)
  {
    unsigned long now = millis();

    utility::time_t elapsed_ms = utility::time_elapsed_ms(last_peripheral_tick_time_ms);
    if (elapsed_ms >= constants::PERIPHERAL_MANAGER_TICK_TIME_MS)
    {
      peripheral_manager.tick();
      last_peripheral_tick_time_ms = now;
    }
  
    /*
    if ((now - lastCaptureTime) >= constants::CAMERA_CAPTURE_INTERVAL_MS) {
      peripheral_manager.pulse_led(constants::LED_ON_TIME_MS);
      lastCaptureTime = now;

      char filename[32];
      sprintf(filename, "/image%d.jpg", imageCount);
      photo_save(filename);
      
      char logstring[128];
      sprintf(logstring, "Saved picture: %s\r\n", filename);
      print_with_timestamp(logstring);
      
      imageCount++;
    }
    else if (now % 5000 == 0) {
      print_with_timestamp("Looping...");
      delay(1);
    }
    */
  }
}
