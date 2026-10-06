#include "hal/hardware_peripheral_manager_impl.h"
#include "hal/gpio_hal.h"
#include "hal/time_hal.h"
#include <Arduino.h>

/**
 * @file hardware_peripheral_manager_impl.cpp
 * @brief ESP32 implementation of HardwarePeripheralManager.
 *
 * Implements shared GPIO pin arbitration using FreeRTOS semaphores,
 * and provides ESP32 core temperature reading.
 */

HardwarePeripheralManagerImpl::HardwarePeripheralManagerImpl()
    : m_sd_active_mutex(xSemaphoreCreateMutex())
    , m_temp_mutex(xSemaphoreCreateMutex())
{
}

void HardwarePeripheralManagerImpl::cs_signal(peripherals::SdSignal signal)
{
    if (!m_sd_active_mutex)
    {
        return;
    }

    if (signal == peripherals::SdSignal::CS_ACTIVE)
    {
        xSemaphoreTake(m_sd_active_mutex, portMAX_DELAY);
        hal::digital_write(hal::LED_PIN, hal::PIN_LOW);
    }
    else
    {
        hal::digital_write(hal::LED_PIN, hal::PIN_HIGH);
        xSemaphoreGive(m_sd_active_mutex);
    }
}

void HardwarePeripheralManagerImpl::update_led(bool led_on)
{
    if (!m_sd_active_mutex)
    {
        return;
    }

    if (xSemaphoreTake(m_sd_active_mutex, 0) != pdTRUE)
    {
        return;
    }

    hal::digital_write(hal::LED_PIN, led_on ? hal::PIN_LOW : hal::PIN_HIGH);
    xSemaphoreGive(m_sd_active_mutex);
}

float HardwarePeripheralManagerImpl::read_temperature()
{
    if (!m_temp_mutex)
    {
        return temperatureRead();
    }
    xSemaphoreTake(m_temp_mutex, portMAX_DELAY);
    float temperature = temperatureRead();
    xSemaphoreGive(m_temp_mutex);
    return temperature;
}

void HardwarePeripheralManagerImpl::setup_shared_pin()
{
    hal::set_pin_as_output(hal::LED_PIN);
}