#pragma once

#include "battery_snapshot.hpp"
#include "rtc_datetime.hpp"

void system_status_update_time(const rtc_datetime& datetime, bool valid);
void system_status_update_battery(const battery_snapshot& snapshot);
