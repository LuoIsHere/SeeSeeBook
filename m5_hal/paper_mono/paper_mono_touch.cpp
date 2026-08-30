#include "touch.hpp"

#include <M5Unified.h>
#include <esp_log.h>

#include "display.hpp"
#include "internal_i2c.hpp"

namespace {

constexpr char log_tag[] = "hal_touch";
constexpr std::int32_t touch_device_width = 480;
constexpr std::int32_t touch_device_height = 800;

std::int32_t clamp_coordinate(
    std::int32_t value,
    std::int32_t maximum)
{
    if (value < 0) {
        return 0;
    }
    return value > maximum ? maximum : value;
}

}  // namespace

bool hal_touch_sample(touch_sample& sample)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_TOUCH_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    M5.update();
    const auto detail = M5.Touch.getDetail();
    sample.pressed = detail.isPressed();
    if (!sample.pressed) {
        return true;
    }

    static_assert(
        PAPER_MONO_DISPLAY_ROTATION == 0U,
        "update PaperMono touch transform when display rotation changes");
    constexpr std::int32_t touch_x_max = touch_device_width - 1;
    constexpr std::int32_t touch_y_max = touch_device_height - 1;
    constexpr std::int32_t screen_x_max = PAPER_MONO_PORTRAIT_WIDTH - 1U;
    constexpr std::int32_t screen_y_max = PAPER_MONO_PORTRAIT_HEIGHT - 1U;
    const std::int32_t touch_x = clamp_coordinate(detail.x, touch_x_max);
    const std::int32_t touch_y = clamp_coordinate(detail.y, touch_y_max);

    // PaperMono touch axes are rotated 90 degrees relative to the portrait framebuffer.
    sample.x = static_cast<std::int16_t>(
        (touch_y * screen_x_max + touch_y_max / 2) / touch_y_max);
    sample.y = static_cast<std::int16_t>(
        ((touch_x_max - touch_x) * screen_y_max + touch_x_max / 2) /
        touch_x_max);

    ESP_LOGD(
        log_tag,
        "device=(%d,%d) screen=(%d,%d)",
        detail.x,
        detail.y,
        sample.x,
        sample.y);
    return true;
}
