#include "display.hpp"

#include <M5Unified.h>
#include <esp_log.h>

#include "display/epd_otp_driver.hpp"
#include "internal_i2c.hpp"
#include "paper_mono_config.hpp"

namespace {

constexpr char log_tag[] = "hal_display";

display_surface surface;
M5Canvas frame_canvas;
bool display_initialized = false;

std::uint32_t native_color(display_color color)
{
    return color == display_color::black ? TFT_BLACK : TFT_WHITE;
}

textdatum_t native_alignment(display_text_alignment alignment)
{
    switch (alignment) {
        case display_text_alignment::middle_left:
            return textdatum_t::middle_left;
        case display_text_alignment::middle_center:
            return textdatum_t::middle_center;
        case display_text_alignment::middle_right:
            return textdatum_t::middle_right;
    }
    return textdatum_t::middle_left;
}

bool valid_refresh_rect(const display_rect& rect)
{
    if (rect.left < 0 || rect.top < 0 || rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    return rect.left + rect.width <= PAPER_MONO_DISPLAY_WIDTH &&
           rect.top + rect.height <= PAPER_MONO_DISPLAY_HEIGHT;
}

paper_mono::otp_refresh_rect native_refresh_rect(const display_rect& rect)
{
    std::uint16_t native_left = 0U;
    std::uint16_t native_top = 0U;
    std::uint16_t native_width = 0U;
    std::uint16_t native_height = 0U;

#if PAPER_MONO_EPD_CANVAS_ROTATION == 5U
    // Rotation 5 transposes logical coordinates into the native framebuffer.
    native_left = static_cast<std::uint16_t>(rect.top);
    native_top = static_cast<std::uint16_t>(rect.left);
    native_width = static_cast<std::uint16_t>(rect.height);
    native_height = static_cast<std::uint16_t>(rect.width);
#elif PAPER_MONO_EPD_CANVAS_ROTATION == 3U
    native_left = static_cast<std::uint16_t>(rect.top);
    native_top = static_cast<std::uint16_t>(
        PAPER_MONO_EPD_NATIVE_HEIGHT - rect.left - rect.width);
    native_width = static_cast<std::uint16_t>(rect.height);
    native_height = static_cast<std::uint16_t>(rect.width);
#else
#error "PaperMono regional refresh conversion needs the configured canvas rotation"
#endif

    // SSD1677 RAM is written MSB-first, so the native X interval must contain
    // complete bytes even when the logical request begins inside one byte.
    const std::uint16_t aligned_left = native_left & ~0x07U;
    std::uint16_t aligned_right = static_cast<std::uint16_t>(
        (native_left + native_width + 7U) & ~0x07U);
    if (aligned_right > PAPER_MONO_EPD_NATIVE_WIDTH) {
        aligned_right = PAPER_MONO_EPD_NATIVE_WIDTH;
    }
    return {
        aligned_left,
        native_top,
        static_cast<std::uint16_t>(aligned_right - aligned_left),
        native_height,
    };
}

paper_mono::otp_refresh_kind native_refresh_kind(refresh_mode mode)
{
    return mode == refresh_mode::quality
               ? paper_mono::otp_refresh_kind::full_mono
               : paper_mono::otp_refresh_kind::partial;
}

refresh_mode logical_refresh_mode(
    paper_mono::otp_refresh_kind kind,
    refresh_mode requested_mode)
{
    return kind == paper_mono::otp_refresh_kind::full_mono
               ? refresh_mode::quality
               : requested_mode;
}

display_refresh_error logical_refresh_error(
    paper_mono::otp_refresh_error error)
{
    switch (error) {
        case paper_mono::otp_refresh_error::none:
            return display_refresh_error::none;
        case paper_mono::otp_refresh_error::invalid_request:
            return display_refresh_error::invalid_request;
        case paper_mono::otp_refresh_error::internal_i2c_unavailable:
            return display_refresh_error::hardware_control_unavailable;
        case paper_mono::otp_refresh_error::transport_failure:
            return display_refresh_error::transport_failure;
    }
    return display_refresh_error::transport_failure;
}

}  // namespace

std::int16_t display_surface::width() const
{
    return static_cast<std::int16_t>(frame_canvas.width());
}

std::int16_t display_surface::height() const
{
    return static_cast<std::int16_t>(frame_canvas.height());
}

void display_surface::fill_screen(display_color color)
{
    frame_canvas.fillScreen(native_color(color));
}

void display_surface::fill_rect(const display_rect& rect, display_color color)
{
    fill_rect(rect.left, rect.top, rect.width, rect.height, color);
}

void display_surface::fill_rect(
    std::int16_t x,
    std::int16_t y,
    std::int16_t width,
    std::int16_t height,
    display_color color)
{
    frame_canvas.fillRect(x, y, width, height, native_color(color));
}

void display_surface::draw_rect(const display_rect& rect, display_color color)
{
    draw_rect(rect.left, rect.top, rect.width, rect.height, color);
}

void display_surface::draw_rect(
    std::int16_t x,
    std::int16_t y,
    std::int16_t width,
    std::int16_t height,
    display_color color)
{
    frame_canvas.drawRect(x, y, width, height, native_color(color));
}

void display_surface::draw_horizontal_line(
    std::int16_t x,
    std::int16_t y,
    std::int16_t width,
    display_color color)
{
    frame_canvas.drawFastHLine(x, y, width, native_color(color));
}

void display_surface::draw_line(
    std::int16_t x0,
    std::int16_t y0,
    std::int16_t x1,
    std::int16_t y1,
    display_color color)
{
    frame_canvas.drawLine(x0, y0, x1, y1, native_color(color));
}

void display_surface::fill_triangle(
    std::int16_t x0,
    std::int16_t y0,
    std::int16_t x1,
    std::int16_t y1,
    std::int16_t x2,
    std::int16_t y2,
    display_color color)
{
    frame_canvas.fillTriangle(
        x0,
        y0,
        x1,
        y1,
        x2,
        y2,
        native_color(color));
}

void display_surface::set_font(display_font font)
{
    if (font == display_font::cjk_24) {
        frame_canvas.setFont(&fonts::efontCN_24);
    } else {
        frame_canvas.setFont(&fonts::Font0);
    }
}

void display_surface::set_text_color(
    display_color foreground,
    display_color background)
{
    frame_canvas.setTextColor(
        native_color(foreground),
        native_color(background));
}

void display_surface::set_text_alignment(display_text_alignment alignment)
{
    frame_canvas.setTextDatum(native_alignment(alignment));
}

void display_surface::set_text_size(std::uint8_t size)
{
    frame_canvas.setTextSize(size);
}

void display_surface::draw_text(
    const char* text,
    std::int16_t x,
    std::int16_t y)
{
    frame_canvas.drawString(text, x, y);
}

std::int32_t display_surface::text_width(const char* text) const
{
    return frame_canvas.textWidth(text);
}

bool hal_display_init()
{
    if (display_initialized) {
        return true;
    }

    // Use M5GFX only as a 1-bit off-screen rasterizer. The raw sprite layout
    // matches the SSD1677 native 800x480, MSB-first framebuffer.
    frame_canvas.setPsram(true);
    frame_canvas.setColorDepth(1U);
    if (frame_canvas.createSprite(
            PAPER_MONO_EPD_NATIVE_WIDTH,
            PAPER_MONO_EPD_NATIVE_HEIGHT) == nullptr) {
        ESP_LOGE(log_tag, "allocate 1-bit display canvas failed");
        return false;
    }
    frame_canvas.setRotation(PAPER_MONO_EPD_CANVAS_ROTATION);
    frame_canvas.setPaletteColor(0U, TFT_BLACK);
    frame_canvas.setPaletteColor(1U, TFT_WHITE);
    frame_canvas.fillScreen(TFT_WHITE);

    if (frame_canvas.width() != PAPER_MONO_DISPLAY_WIDTH ||
        frame_canvas.height() != PAPER_MONO_DISPLAY_HEIGHT ||
        frame_canvas.bufferLength() != PAPER_MONO_EPD_FRAME_SIZE) {
        ESP_LOGE(
            log_tag,
            "unexpected canvas width=%d height=%d bytes=%lu",
            frame_canvas.width(),
            frame_canvas.height(),
            static_cast<unsigned long>(frame_canvas.bufferLength()));
        frame_canvas.deleteSprite();
        return false;
    }

    if (!paper_mono::epd_otp_driver_init(
            static_cast<const std::uint8_t*>(frame_canvas.getBuffer()),
            frame_canvas.bufferLength())) {
        frame_canvas.deleteSprite();
        return false;
    }

    display_initialized = true;
    if (!hal_display_set_front_light(128U)) {
        ESP_LOGW(log_tag, "initial front light update failed");
    }
    ESP_LOGI(
        log_tag,
        "OTP display backend initialized logical=%dx%d native=%ux%u",
        frame_canvas.width(),
        frame_canvas.height(),
        PAPER_MONO_EPD_NATIVE_WIDTH,
        PAPER_MONO_EPD_NATIVE_HEIGHT);
    return true;
}

display_surface& hal_display_surface()
{
    return surface;
}

display_refresh_result hal_display_refresh(
    const display_rect& rect,
    refresh_mode mode)
{
    display_refresh_result result = {
        false,
        mode,
        0U,
        display_refresh_error::invalid_request,
    };
    if (!display_initialized || !valid_refresh_rect(rect)) {
        ESP_LOGE(
            log_tag,
            "invalid refresh initialized=%d rect=%d,%d %dx%d",
            display_initialized,
            rect.left,
            rect.top,
            rect.width,
            rect.height);
        return result;
    }

    const paper_mono::otp_refresh_rect native_rect = native_refresh_rect(rect);
    const paper_mono::otp_refresh_kind requested_kind = native_refresh_kind(mode);
    paper_mono::otp_refresh_result driver_result =
        paper_mono::epd_otp_driver_refresh(
            static_cast<const std::uint8_t*>(frame_canvas.getBuffer()),
            frame_canvas.bufferLength(),
            native_rect,
            requested_kind);
    std::uint32_t driver_duration_ms = driver_result.duration_ms;
    bool recovery_attempted = false;
    bool recovered = false;
    if (!driver_result.success &&
        driver_result.error ==
            paper_mono::otp_refresh_error::internal_i2c_unavailable) {
        recovery_attempted = true;
        recovered = hal_internal_i2c_recover();
        if (recovered) {
            ESP_LOGW(
                log_tag,
                "internal I2C recovered; retrying one quality refresh");
            driver_result = paper_mono::epd_otp_driver_refresh(
                static_cast<const std::uint8_t*>(frame_canvas.getBuffer()),
                frame_canvas.bufferLength(),
                native_rect,
                paper_mono::otp_refresh_kind::full_mono);
            driver_duration_ms += driver_result.duration_ms;
        }
    }
    result.success = driver_result.success;
    result.actual_mode = logical_refresh_mode(
        driver_result.actual_kind,
        mode);
    result.duration_ms = driver_duration_ms;
    result.error = logical_refresh_error(driver_result.error);

    ESP_LOGI(
        log_tag,
        "logical=%d,%d %dx%d native=%u,%u %ux%u requested=%u physical=%s actual=%u success=%d error=%u recovery=%u recovered=%u",
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        static_cast<unsigned>(native_rect.left),
        static_cast<unsigned>(native_rect.top),
        static_cast<unsigned>(native_rect.width),
        static_cast<unsigned>(native_rect.height),
        static_cast<unsigned>(mode),
        driver_result.actual_kind == paper_mono::otp_refresh_kind::full_mono
            ? "quality"
            : "partial_otp",
        static_cast<unsigned>(result.actual_mode),
        result.success,
        static_cast<unsigned>(result.error),
        recovery_attempted ? 1U : 0U,
        recovered ? 1U : 0U);
    return result;
}

bool hal_display_sleep()
{
    if (!display_initialized) {
        return false;
    }
    return paper_mono::epd_otp_driver_sleep();
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
