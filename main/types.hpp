#pragma once

#include <cstdint>
#include <type_traits>

#define PROJECT_NAME "SeeSeeBook"
#define PROJECT_VERSION "V0.1"

#define SYSTEM_TICK_PERIOD_MS 1U
#define TOUCH_DEBOUNCE_TIME_MS 20U
#define TOUCH_LONG_PRESS_TIME_MS 800U
#define LONG_PRESS_REFRESH_INTERVAL_MS 250U

#define EPD_FASTEST_REFRESH_LIMIT 10U
#define EPD_FAST_REFRESH_LIMIT 5U

#define PAPER_MONO_DISPLAY_ROTATION 0U
#define PAPER_MONO_PORTRAIT_WIDTH 480U
#define PAPER_MONO_PORTRAIT_HEIGHT 800U
#define PAPER_MONO_TOUCH_DEVICE_WIDTH 480U
#define PAPER_MONO_TOUCH_DEVICE_HEIGHT 800U
#define FRONT_LIGHT_BAR_HEIGHT 72U
#define FRONT_LIGHT_LEVEL_COUNT 5U
#define FRONT_LIGHT_DEFAULT_LEVEL_INDEX 2U
#define FRONT_LIGHT_LEVEL_OFF 0U
#define FRONT_LIGHT_LEVEL_25 64U
#define FRONT_LIGHT_LEVEL_50 128U
#define FRONT_LIGHT_LEVEL_75 192U
#define FRONT_LIGHT_LEVEL_100 255U

#define TOUCH_EVENT_QUEUE_LENGTH 16U
#define DISPLAY_REQUEST_QUEUE_LENGTH 1U
#define FRONT_LIGHT_REQUEST_QUEUE_LENGTH 8U
#define TOUCH_TASK_STACK_SIZE 4096U
#define DISPLAY_TASK_STACK_SIZE 6144U
#define TOUCH_TASK_PRIORITY 6U
#define DISPLAY_TASK_PRIORITY 5U

enum class refresh_mode : std::uint8_t {
    fastest,
    fast,
    quality,
};

enum class touch_event_type : std::uint8_t {
    press,
    click,
    long_press_start,
    long_press_repeat,
    long_press_end,
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

struct touch_event {
    touch_event_type type;
    std::int16_t start_x;
    std::int16_t start_y;
    std::int16_t end_x;
    std::int16_t end_y;
    std::uint32_t duration_ms;
    std::uint32_t timestamp_ms;
};

struct display_request {
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

struct front_light_request {
    std::uint8_t button_index;
    bool pressed;
    bool apply_level;
};

static_assert(std::is_trivially_copyable_v<touch_event>);
static_assert(std::is_trivially_copyable_v<display_request>);
static_assert(std::is_trivially_copyable_v<front_light_request>);
