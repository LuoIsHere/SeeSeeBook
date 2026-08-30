#pragma once

#include <esp_err.h>

#include "input_event.hpp"

#define INPUT_EVENT_QUEUE_LENGTH 16U
#define INPUT_TASK_STACK_SIZE 4096U
#define INPUT_TASK_PRIORITY 6U
#define INPUT_DEBOUNCE_TIME_MS 20U
#define INPUT_LONG_PRESS_TIME_MS 800U
#define INPUT_LONG_PRESS_REPEAT_MS 250U

esp_err_t input_service_init();
bool input_service_try_get_event(input_event& event);
