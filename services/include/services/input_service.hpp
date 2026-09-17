#pragma once

#include <esp_err.h>

#include "input_event.hpp"
#include "navigation_event.hpp"

#define INPUT_EVENT_QUEUE_LENGTH 16U
#define INPUT_TASK_STACK_SIZE 4096U
#define INPUT_TASK_PRIORITY 6U
#define INPUT_DEBOUNCE_TIME_MS 20U
#define INPUT_LONG_PRESS_TIME_MS 800U
#define INPUT_LONG_PRESS_REPEAT_MS 250U
#define BUTTON_EVENT_QUEUE_LENGTH 8U
#define BUTTON_DEBOUNCE_TIME_MS 20U
#define BUTTON_LONG_PRESS_TIME_MS 800U

esp_err_t input_service_init();
bool input_service_try_get_event(input_event& event);
bool input_service_try_get_navigation_event(navigation_event& event);
