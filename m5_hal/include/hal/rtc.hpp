#pragma once

#include "rtc_datetime.hpp"

// Reads the hardware RTC in device-local time.
bool hal_rtc_read(rtc_datetime& datetime);

// Writes the hardware RTC in device-local time.
bool hal_rtc_write(const rtc_datetime& datetime);
