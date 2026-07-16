#ifndef ARDUINO_H
#define ARDUINO_H

#include <iostream>
#include <string>
#include <stdint.h>
#include <vector>
#include <utility>

// Basic Arduino types
typedef uint32_t uint32_t; 
typedef uint8_t uint8_t;

// Arduino constants
#ifndef LED_BUILTIN
#define LED_BUILTIN 21
#endif
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0

// Mock String class to satisfy utility.cpp
class String : public std::string
{
    public:
        String() : std::string() {}
        String(const char* s) : std::string(s) {}
        String(const std::string& s) : std::string(s) {}
};

// Mock Serial object
class HardwareSerial
{
    public:
        void begin(uint32_t) {}
        void print(const String& s) { std::cout << s; }
        void print(uint32_t n) { std::cout << n; }
        void println(const String& s) { std::cout << s << std::endl; }
        void println(uint32_t n) { std::cout << n << std::endl; }
        void write(const char* buffer, const size_t size) { std::cout << buffer << std::endl; }
        void flush() { } 
};

extern HardwareSerial Serial;

// Mock millis() function
uint32_t millis();
void set_mock_millis(uint32_t ms);

// Mock delay functions
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

// Mock GPIO functions
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);

// GPIO call tracking
std::vector<std::pair<uint8_t, uint8_t>>& get_gpio_log();
void clear_gpio_log();

#endif // ARDUINO_H
