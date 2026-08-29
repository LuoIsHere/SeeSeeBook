#pragma once

#include <cstdint>
#include <type_traits>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#define EPD_FASTEST_REFRESH_LIMIT 10U
#define EPD_FAST_REFRESH_LIMIT 5U

#define PAPER_MONO_DISPLAY_ROTATION 0U
#define PAPER_MONO_PORTRAIT_WIDTH 480U
#define PAPER_MONO_PORTRAIT_HEIGHT 800U

#define FRONT_LIGHT_BAR_HEIGHT 72U
#define FRONT_LIGHT_LEVEL_COUNT 5U
#define FRONT_LIGHT_DEFAULT_LEVEL_INDEX 2U
#define FRONT_LIGHT_LEVEL_OFF 0U
#define FRONT_LIGHT_LEVEL_25 64U
#define FRONT_LIGHT_LEVEL_50 128U
#define FRONT_LIGHT_LEVEL_75 192U
#define FRONT_LIGHT_LEVEL_100 255U

#define DISPLAY_REQUEST_QUEUE_LENGTH 1U
#define DISPLAY_CONTROL_QUEUE_LENGTH 16U
#define DISPLAY_TASK_STACK_SIZE 6144U
#define DISPLAY_TASK_PRIORITY 5U

enum class refresh_mode : std::uint8_t {
    fastest,
    fast,
    quality,
};

enum class display_view : std::uint8_t {
    menu,
    test,
};

enum class ui_text_state : std::uint8_t {
    hi_xi,
    hello_world,
};

enum class touch_display_type : std::uint8_t {
    none,
    click,
    long_press,
};

enum class display_control_type : std::uint8_t {
    front_light,
    menu_entry,
    back_button,
};

struct display_request {
    display_view view;
    ui_text_state text_state;
    touch_display_type touch_type;
    std::int16_t start_x;
    std::int16_t start_y;
    std::int16_t end_x;
    std::int16_t end_y;
    std::uint32_t duration_ms;
    std::uint32_t timestamp_ms;
    std::uint32_t minimum_refresh_interval_ms;
    bool force_quality;
};

struct display_control_request {
    display_control_type type;
    std::uint8_t button_index;
    bool pressed;
    bool apply_level;
};

static_assert(std::is_trivially_copyable_v<display_request>);
static_assert(std::is_trivially_copyable_v<display_control_request>);

// Starts the display worker that owns all M5Unified display refresh operations.
bool hal_display_start(
    QueueHandle_t request_queue,
    QueueHandle_t control_queue,
    TaskHandle_t& task_handle);

// Replaces a pending full frame with the latest application state without blocking.
bool hal_submit_display_request(const display_request& request);

// Queues a visual control transition without blocking the Mooncake scheduler.
bool hal_submit_display_control_request(const display_control_request& request);
