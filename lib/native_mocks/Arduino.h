#ifndef ARDUINO_H
#define ARDUINO_H

#include <iostream>
#include <string>
#include <stdint.h>

// Basic Arduino types
typedef uint32_t uint32_t; 
typedef uint8_t uint8_t;

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
        void print(const String& s) { std::cout << s; }
        void print(uint32_t n) { std::cout << n; }
        void println(const String& s) { std::cout << s << std::endl; }
        void println(uint32_t n) { std::cout << n << std::endl; }
};

extern HardwareSerial Serial;

// Mock millis() function
uint32_t millis();
void set_mock_millis(uint32_t ms);

#endif // ARDUINO_H
