#include "display.hpp"

#include <algorithm>
#include <atomic>

#include <M5Unified.h>
#include <esp_log.h>

#include "display/epd_otp_driver.hpp"
#include "internal_i2c.hpp"
#include "paper_mono_config.hpp"

namespace {

constexpr char log_tag[] = "hal_display";

display_surface surface;
M5Canvas frame_canvas;
gray4_framebuffer_view frame_buffer;
bool display_initialized = false;
std::atomic_uint8_t desired_front_light{128U};
std::atomic_bool front_light_suspended{false};

bool apply_front_light(std::uint8_t brightness)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_FRONT_LIGHT_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        ESP_LOGW(log_tag, "front light update skipped; internal I2C bus busy");
        return false;
    }
    M5.Display.setBrightness(brightness);
    return true;
}

void suspend_front_light_for_full_refresh()
{
    front_light_suspended.store(true);
    if (!apply_front_light(0U)) {
        ESP_LOGW(log_tag, "front light could not be disabled before full refresh");
    }
}

void restore_front_light_after_full_refresh()
{
    front_light_suspended.store(false);
    const std::uint8_t brightness = desired_front_light.load();
    if (!apply_front_light(brightness)) {
        ESP_LOGW(log_tag, "front light restore failed after full refresh");
    }
}

std::uint32_t native_color(display_color color)
{
    return color == display_color::black ? TFT_BLACK : TFT_WHITE;
}

std::uint32_t native_color(display_gray4 color)
{
    switch (color) {
        case display_gray4::black: return TFT_BLACK;
        case display_gray4::dark_gray: return 0x555555U;
        case display_gray4::light_gray: return 0xaaaaaaU;
        case display_gray4::white: return TFT_WHITE;
    }
    return TFT_WHITE;
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

bool logical_to_native(std::int16_t logical_x, std::int16_t logical_y,
                       std::uint16_t& native_x, std::uint16_t& native_y)
{
    if (logical_x < 0 || logical_y < 0 ||
        logical_x >= PAPER_MONO_DISPLAY_WIDTH ||
        logical_y >= PAPER_MONO_DISPLAY_HEIGHT) {
        return false;
    }
#if PAPER_MONO_EPD_CANVAS_ROTATION == 5U
    native_x = static_cast<std::uint16_t>(logical_y);
    native_y = static_cast<std::uint16_t>(logical_x);
#elif PAPER_MONO_EPD_CANVAS_ROTATION == 3U
    native_x = static_cast<std::uint16_t>(logical_y);
    native_y = static_cast<std::uint16_t>(
        PAPER_MONO_EPD_NATIVE_HEIGHT - 1 - logical_x);
#else
#error "PaperMono coordinate conversion needs the configured canvas rotation"
#endif
    return true;
}

bool draw_gray4_image(const std::uint8_t* data, std::size_t length,
                      book_cover_encoding encoding, const display_rect& rect,
                      display_image_mode mode)
{
    M5Canvas grayscale_canvas;
    grayscale_canvas.setPsram(true);
    grayscale_canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    if (grayscale_canvas.createSprite(rect.width, rect.height) == nullptr) {
        ESP_LOGW(log_tag, "allocate grayscale image canvas failed bytes=%lu",
                 static_cast<unsigned long>(std::size_t(rect.width) * rect.height));
        return false;
    }
    grayscale_canvas.fillScreen(TFT_WHITE);
    const bool decoded = encoding == book_cover_encoding::jpeg
        ? grayscale_canvas.drawJpg(data, static_cast<std::uint32_t>(length),
            0, 0, rect.width, rect.height, 0, 0, 0.0f, 0.0f,
            datum_t::middle_center)
        : encoding == book_cover_encoding::png
        ? grayscale_canvas.drawPng(data, static_cast<std::uint32_t>(length),
            0, 0, rect.width, rect.height, 0, 0, 0.0f, 0.0f,
            datum_t::middle_center)
        : false;
    const std::size_t expected = std::size_t(rect.width) * rect.height;
    if (!decoded || grayscale_canvas.bufferLength() != expected) {
        return false;
    }

    const auto* grayscale =
        static_cast<const std::uint8_t*>(grayscale_canvas.getBuffer());
    for (std::int16_t y = 0; y < rect.height; ++y) {
        for (std::int16_t x = 0; x < rect.width; ++x) {
            std::uint16_t native_x = 0U;
            std::uint16_t native_y = 0U;
            if (!logical_to_native(rect.left + x, rect.top + y,
                                   native_x, native_y) ||
                !frame_buffer.set_pixel(
                    native_x, native_y,
                    mode == display_image_mode::mono_dither
                        ? gray4_mono_dither(
                              grayscale[std::size_t(y) * rect.width + x],
                              static_cast<std::uint16_t>(rect.left + x),
                              static_cast<std::uint16_t>(rect.top + y))
                        : gray4_quantize(
                              grayscale[std::size_t(y) * rect.width + x]))) {
                return false;
            }
        }
    }
    return true;
}

paper_mono::otp_refresh_rect native_refresh_rect(const display_rect& rect)
{
    std::uint16_t native_left = 0U;
    std::uint16_t native_top = 0U;
    std::uint16_t native_width = 0U;
    std::uint16_t native_height = 0U;

#if PAPER_MONO_EPD_CANVAS_ROTATION == 5U
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

    const std::uint16_t aligned_left = native_left & ~0x07U;
    std::uint16_t aligned_right = static_cast<std::uint16_t>(
        (native_left + native_width + 7U) & ~0x07U);
    if (aligned_right > PAPER_MONO_EPD_NATIVE_WIDTH) {
        aligned_right = PAPER_MONO_EPD_NATIVE_WIDTH;
    }
    return {aligned_left, native_top,
            static_cast<std::uint16_t>(aligned_right - aligned_left),
            native_height};
}

paper_mono::otp_refresh_kind native_refresh_kind(refresh_mode mode)
{
    if (frame_buffer.has_intermediate_gray()) {
        return paper_mono::otp_refresh_kind::full_gray4;
    }
    return mode == refresh_mode::quality
               ? paper_mono::otp_refresh_kind::full_mono
               : paper_mono::otp_refresh_kind::partial;
}

refresh_mode logical_refresh_mode(paper_mono::otp_refresh_kind kind,
                                  refresh_mode requested_mode)
{
    return kind == paper_mono::otp_refresh_kind::partial
               ? requested_mode
               : refresh_mode::quality;
}

display_refresh_error logical_refresh_error(paper_mono::otp_refresh_error error)
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

const char* physical_refresh_name(paper_mono::otp_refresh_kind kind)
{
    switch (kind) {
        case paper_mono::otp_refresh_kind::partial:
            return "partial_mono_otp";
        case paper_mono::otp_refresh_kind::full_mono:
            return "full_mono_otp";
        case paper_mono::otp_refresh_kind::full_gray4:
            return "full_gray4_otp_d7";
    }
    return "unknown";
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

void display_surface::fill_screen(display_gray4 color)
{
    frame_buffer.fill(color);
}

void display_surface::fill_rect(const display_rect& rect, display_color color)
{
    fill_rect(rect.left, rect.top, rect.width, rect.height, color);
}

bool display_surface::fill_rect(const display_rect& rect, display_gray4 color)
{
    if (!valid_refresh_rect(rect)) {
        return false;
    }
#if PAPER_MONO_EPD_CANVAS_ROTATION == 5U
    return frame_buffer.fill_rect(
        static_cast<std::uint16_t>(rect.top),
        static_cast<std::uint16_t>(rect.left),
        static_cast<std::uint16_t>(rect.height),
        static_cast<std::uint16_t>(rect.width),
        color);
#elif PAPER_MONO_EPD_CANVAS_ROTATION == 3U
    return frame_buffer.fill_rect(
        static_cast<std::uint16_t>(rect.top),
        static_cast<std::uint16_t>(
            PAPER_MONO_EPD_NATIVE_HEIGHT - rect.left - rect.width),
        static_cast<std::uint16_t>(rect.height),
        static_cast<std::uint16_t>(rect.width),
        color);
#endif
}

void display_surface::fill_rect(std::int16_t x, std::int16_t y,
                                std::int16_t width_value,
                                std::int16_t height_value,
                                display_color color)
{
    frame_canvas.fillRect(x, y, width_value, height_value, native_color(color));
}

void display_surface::draw_rect(const display_rect& rect, display_color color)
{
    draw_rect(rect.left, rect.top, rect.width, rect.height, color);
}

void display_surface::draw_rect(std::int16_t x, std::int16_t y,
                                std::int16_t width_value,
                                std::int16_t height_value,
                                display_color color)
{
    frame_canvas.drawRect(x, y, width_value, height_value, native_color(color));
}

void display_surface::fill_round_rect(
    const display_rect& rect,
    std::int16_t radius,
    display_color color)
{
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const std::int16_t clamped_radius = std::max<std::int16_t>(
        0,
        std::min<std::int16_t>(
            radius,
            std::min<std::int16_t>(rect.width / 2, rect.height / 2)));
    frame_canvas.fillRoundRect(
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        clamped_radius,
        native_color(color));
}

void display_surface::draw_round_rect(
    const display_rect& rect,
    std::int16_t radius,
    display_color color)
{
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const std::int16_t clamped_radius = std::max<std::int16_t>(
        0,
        std::min<std::int16_t>(
            radius,
            std::min<std::int16_t>(rect.width / 2, rect.height / 2)));
    frame_canvas.drawRoundRect(
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        clamped_radius,
        native_color(color));
}

void display_surface::draw_horizontal_line(std::int16_t x, std::int16_t y,
                                           std::int16_t width_value,
                                           display_color color)
{
    frame_canvas.drawFastHLine(x, y, width_value, native_color(color));
}

void display_surface::draw_line(std::int16_t x0, std::int16_t y0,
                                std::int16_t x1, std::int16_t y1,
                                display_color color)
{
    frame_canvas.drawLine(x0, y0, x1, y1, native_color(color));
}

void display_surface::fill_triangle(std::int16_t x0, std::int16_t y0,
                                    std::int16_t x1, std::int16_t y1,
                                    std::int16_t x2, std::int16_t y2,
                                    display_color color)
{
    frame_canvas.fillTriangle(x0, y0, x1, y1, x2, y2, native_color(color));
}

void display_surface::set_font(display_font font)
{
    if (font == display_font::cjk_24) {
        frame_canvas.setFont(&fonts::efontCN_24);
    } else {
        frame_canvas.setFont(&fonts::Font0);
    }
}

void display_surface::set_text_color(display_color foreground,
                                     display_color background)
{
    frame_canvas.setTextColor(native_color(foreground), native_color(background));
}

void display_surface::set_text_color(display_gray4 foreground,
                                     display_gray4 background)
{
    frame_canvas.setTextColor(native_color(foreground), native_color(background));
}

void display_surface::set_text_alignment(display_text_alignment alignment)
{
    frame_canvas.setTextDatum(native_alignment(alignment));
}

void display_surface::set_text_size(std::uint8_t size)
{
    frame_canvas.setTextSize(size);
}

void display_surface::draw_text(const char* text, std::int16_t x, std::int16_t y)
{
    frame_canvas.drawString(text, x, y);
}

std::int32_t display_surface::text_width(const char* text) const
{
    return frame_canvas.textWidth(text);
}

bool display_surface::draw_image(const std::uint8_t* data, std::size_t length,
                                 book_cover_encoding encoding,
                                 const display_rect& rect,
                                 display_image_mode mode)
{
    if (data == nullptr || length == 0U || length > UINT32_MAX ||
        !valid_refresh_rect(rect)) {
        return false;
    }
    return draw_gray4_image(data, length, encoding, rect, mode);
}

bool display_surface::set_pixel(std::int16_t x, std::int16_t y,
                                display_gray4 color)
{
    std::uint16_t native_x = 0U;
    std::uint16_t native_y = 0U;
    return logical_to_native(x, y, native_x, native_y) &&
           frame_buffer.set_pixel(native_x, native_y, color);
}

bool display_surface::has_intermediate_gray() const
{
    return frame_buffer.has_intermediate_gray();
}

bool hal_display_init()
{
    if (display_initialized) {
        return true;
    }

    frame_canvas.setPsram(true);
    frame_canvas.setColorDepth(lgfx::color_depth_t::grayscale_2bit);
    if (frame_canvas.createSprite(PAPER_MONO_EPD_NATIVE_WIDTH,
                                  PAPER_MONO_EPD_NATIVE_HEIGHT) == nullptr) {
        ESP_LOGE(log_tag, "allocate 2bpp display canvas failed");
        return false;
    }
    frame_canvas.setRotation(PAPER_MONO_EPD_CANVAS_ROTATION);
    frame_canvas.setPaletteColor(0U, TFT_BLACK);
    frame_canvas.setPaletteColor(1U, 0x555555U);
    frame_canvas.setPaletteColor(2U, 0xaaaaaaU);
    frame_canvas.setPaletteColor(3U, TFT_WHITE);

    frame_buffer = gray4_framebuffer_view(
        static_cast<std::uint8_t*>(frame_canvas.getBuffer()),
        frame_canvas.bufferLength(), PAPER_MONO_EPD_NATIVE_WIDTH,
        PAPER_MONO_EPD_NATIVE_HEIGHT);
    if (frame_canvas.width() != PAPER_MONO_DISPLAY_WIDTH ||
        frame_canvas.height() != PAPER_MONO_DISPLAY_HEIGHT ||
        frame_canvas.bufferLength() != PAPER_MONO_GRAY4_FRAME_SIZE ||
        !frame_buffer.valid()) {
        ESP_LOGE(log_tag, "unexpected Gray4 canvas width=%d height=%d bytes=%lu",
                 frame_canvas.width(), frame_canvas.height(),
                 static_cast<unsigned long>(frame_canvas.bufferLength()));
        frame_canvas.deleteSprite();
        frame_buffer = {};
        return false;
    }
    frame_buffer.fill(display_gray4::white);

    if (!paper_mono::epd_otp_driver_init(
            static_cast<const std::uint8_t*>(frame_canvas.getBuffer()),
            frame_canvas.bufferLength())) {
        frame_canvas.deleteSprite();
        frame_buffer = {};
        return false;
    }

    display_initialized = true;
    if (!hal_display_set_front_light(128U)) {
        ESP_LOGW(log_tag, "initial front light update failed");
    }
    ESP_LOGI(log_tag,
             "OTP display backend initialized logical=%dx%d native=%ux%u bpp=2 bytes=%lu",
             frame_canvas.width(), frame_canvas.height(),
             PAPER_MONO_EPD_NATIVE_WIDTH, PAPER_MONO_EPD_NATIVE_HEIGHT,
             static_cast<unsigned long>(frame_canvas.bufferLength()));
    return true;
}

display_surface& hal_display_surface()
{
    return surface;
}

display_refresh_result hal_display_refresh(const display_rect& rect,
                                           refresh_mode mode)
{
    display_refresh_result result = {
        false, mode, 0U, display_refresh_error::invalid_request};
    if (!display_initialized || !valid_refresh_rect(rect)) {
        ESP_LOGE(log_tag, "invalid refresh initialized=%d rect=%d,%d %dx%d",
                 display_initialized, rect.left, rect.top, rect.width, rect.height);
        return result;
    }

    const paper_mono::otp_refresh_kind requested_kind = native_refresh_kind(mode);
    const paper_mono::otp_refresh_rect requested_rect =
        requested_kind == paper_mono::otp_refresh_kind::full_gray4
            ? paper_mono::otp_refresh_rect{0U, 0U, PAPER_MONO_EPD_NATIVE_WIDTH,
                                           PAPER_MONO_EPD_NATIVE_HEIGHT}
            : native_refresh_rect(rect);
    bool front_light_muted =
        requested_kind != paper_mono::otp_refresh_kind::partial;
    if (front_light_muted) {
        suspend_front_light_for_full_refresh();
    }
    paper_mono::otp_refresh_result driver_result =
        paper_mono::epd_otp_driver_refresh(
            static_cast<const std::uint8_t*>(frame_canvas.getBuffer()),
            frame_canvas.bufferLength(), requested_rect, requested_kind);
    std::uint32_t driver_duration_ms = driver_result.duration_ms;
    bool recovery_attempted = false;
    bool recovered = false;
    if (!driver_result.success &&
        driver_result.error ==
            paper_mono::otp_refresh_error::internal_i2c_unavailable) {
        recovery_attempted = true;
        recovered = hal_internal_i2c_recover();
        if (recovered) {
            const paper_mono::otp_refresh_kind recovery_kind =
                requested_kind == paper_mono::otp_refresh_kind::full_gray4
                    ? paper_mono::otp_refresh_kind::full_gray4
                    : paper_mono::otp_refresh_kind::full_mono;
            if (!front_light_muted) {
                suspend_front_light_for_full_refresh();
                front_light_muted = true;
            }
            ESP_LOGW(log_tag, "internal I2C recovered; retrying %s",
                     physical_refresh_name(recovery_kind));
            driver_result = paper_mono::epd_otp_driver_refresh(
                static_cast<const std::uint8_t*>(frame_canvas.getBuffer()),
                frame_canvas.bufferLength(), requested_rect, recovery_kind);
            driver_duration_ms += driver_result.duration_ms;
        }
    }
    if (front_light_muted) {
        restore_front_light_after_full_refresh();
    }
    result.success = driver_result.success;
    result.actual_mode = logical_refresh_mode(driver_result.actual_kind, mode);
    result.duration_ms = driver_duration_ms;
    result.error = logical_refresh_error(driver_result.error);

    ESP_LOGI(
        log_tag,
        "logical=%d,%d %dx%d native=%u,%u %ux%u requested=%u middle_gray=%u physical=%s actual=%u success=%d error=%u recovery=%u recovered=%u",
        rect.left, rect.top, rect.width, rect.height,
        static_cast<unsigned>(requested_rect.left),
        static_cast<unsigned>(requested_rect.top),
        static_cast<unsigned>(requested_rect.width),
        static_cast<unsigned>(requested_rect.height),
        static_cast<unsigned>(mode),
        frame_buffer.has_intermediate_gray() ? 1U : 0U,
        physical_refresh_name(driver_result.actual_kind),
        static_cast<unsigned>(result.actual_mode), result.success,
        static_cast<unsigned>(result.error), recovery_attempted ? 1U : 0U,
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
    desired_front_light.store(brightness);
    if (front_light_suspended.load()) {
        return true;
    }
    const bool applied = apply_front_light(brightness);
    if (front_light_suspended.load()) {
        return apply_front_light(0U) && applied;
    }
    return applied;
}
