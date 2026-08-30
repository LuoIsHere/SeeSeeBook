#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool system_tick_service_init();
bool system_tick_service_register_task(
    TaskHandle_t task_handle,
    std::uint32_t period_ms);
bool system_tick_service_start();
std::uint32_t system_tick_now_ms();
