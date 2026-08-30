#pragma once

#include <esp_err.h>

#include "rtc_datetime.hpp"
#include "service_event.hpp"

esp_err_t rtc_service_init();
bool rtc_service_submit_read(std::uint32_t request_id);
bool rtc_service_submit_write(
    const rtc_datetime& datetime,
    std::uint32_t request_id);
bool rtc_service_try_get_event(rtc_service_event& event);
bool rtc_service_get_cached_datetime(rtc_datetime& datetime);
