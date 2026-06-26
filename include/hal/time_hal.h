#ifndef TIME_HAL_H
#define TIME_HAL_H

#include <stdint.h>

namespace hal 
{

/**
 * @brief Type alias for time representation in milliseconds.
 */
using millis_t = uint32_t;

/**
 * @brief Returns the number of milliseconds since the program started.
 */
millis_t get_millis();

/**
 * @brief Blocks execution for the given number of milliseconds.
 */
void delay_ms(uint32_t ms);

/**
 * @brief Blocks execution for the given number of microseconds.
 */
void delay_us(uint32_t us);

} // namespace hal

#endif // TIME_HAL_H

