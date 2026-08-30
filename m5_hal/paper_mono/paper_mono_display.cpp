#include "display.hpp"

#include <M5Unified.h>
#include <esp_log.h>

#include "internal_i2c.hpp"

namespace {

constexpr char log_tag[] = "hal_display";

void apply_refresh_mode(refresh_mode mode)
{
    switch (mode) {
        case refresh_mode::fastest:
            M5.Display.setEpdMode(epd_mode_t::epd_fastest);
            break;
        case refresh_mode::fast:
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
            break;
        case refresh_mode::quality:
            M5.Display.setEpdMode(epd_mode_t::epd_quality);
            break;
    }
}

}  // namespace

bool hal_display_init()
{
    M5.Display.setRotation(PAPER_MONO_DISPLAY_ROTATION);
    if (M5.Display.width() != PAPER_MONO_PORTRAIT_WIDTH ||
        M5.Display.height() != PAPER_MONO_PORTRAIT_HEIGHT) {
        ESP_LOGE(
            log_tag,
            "unexpected portrait dimensions width=%d height=%d",
            M5.Display.width(),
            M5.Display.height());
        return false;
    }
    M5.Display.setAutoDisplay(false);
    return hal_display_set_front_light(128U);
}

M5GFX& hal_display_canvas()
{
    return M5.Display;
}

bool hal_display_refresh(
    const display_rect& rect,
    refresh_mode mode)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(mode);
    if (rect.left == 0 && rect.top == 0 &&
        rect.width == M5.Display.width() && rect.height == M5.Display.height()) {
        M5.Display.display();
    } else {
        M5.Display.display(rect.left, rect.top, rect.width, rect.height);
    }
    M5.Display.waitDisplay();
    return true;
}

bool hal_display_set_front_light(std::uint8_t brightness)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_FRONT_LIGHT_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        ESP_LOGW(log_tag, "front light update skipped; internal I2C bus busy");
        return false;
    }
    M5.Display.setBrightness(brightness);
    return true;
}
