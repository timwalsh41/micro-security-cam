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

} // namespace hal

#endif // TIME_HAL_H

