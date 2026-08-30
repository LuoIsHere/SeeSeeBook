#pragma once

#include "rtc_datetime.hpp"

// Reads the PaperMono RTC in device-local time.
bool hal_rtc_read(rtc_datetime& datetime);

// Writes the PaperMono RTC in device-local time.
bool hal_rtc_write(const rtc_datetime& datetime);
