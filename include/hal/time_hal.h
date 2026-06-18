#ifndef TIME_HAL_H
#define TIME_HAL_H

#include <stdint.h>

namespace hal {

/**
 * @brief Type alias for time representation in milliseconds.
 */
using time_t = uint32_t;

/**
 * @brief Returns the number of milliseconds since the program started.
 */
time_t get_millis();

} // namespace hal

#endif // TIME_HAL_H

