#!/bin/zsh
source ~/.platformio/penv/bin/activate
pio test -e seeed_xiao_esp32s3_test -f hardware/test_timer_hal_impl --without-uploading --without-testing