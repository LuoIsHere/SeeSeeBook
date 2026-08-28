#include "hal.h"

#include <cstdio>

#include <M5Unified.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

constexpr char log_tag[] = "hal_display";
constexpr std::int16_t no_pressed_button = -1;
constexpr std::uint8_t front_light_values[FRONT_LIGHT_LEVEL_COUNT] = {
    FRONT_LIGHT_LEVEL_OFF,
    FRONT_LIGHT_LEVEL_25,
    FRONT_LIGHT_LEVEL_50,
    FRONT_LIGHT_LEVEL_75,
    FRONT_LIGHT_LEVEL_100,
};
constexpr const char* front_light_labels[FRONT_LIGHT_LEVEL_COUNT] = {
    "OFF",
    "25%",
    "50%",
    "75%",
    "100%",
};

QueueHandle_t request_queue = nullptr;
QueueHandle_t front_light_queue = nullptr;

struct refresh_policy {
    std::uint8_t fastest_count = 0;
    std::uint8_t fast_count = 0;
};

const char* main_text(ui_text_state text_state)
{
    return text_state == ui_text_state::hi_xi ? "HI XI" : "Hello world";
}

const char* refresh_mode_name(refresh_mode mode)
{
    switch (mode) {
        case refresh_mode::fastest:
            return "fastest";
        case refresh_mode::fast:
            return "fast";
        case refresh_mode::quality:
            return "quality";
    }
    return "unknown";
}

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

refresh_mode select_refresh_mode(const refresh_policy& policy)
{
    if (policy.fast_count >= EPD_FAST_REFRESH_LIMIT) {
        return refresh_mode::quality;
    }
    if (policy.fastest_count >= EPD_FASTEST_REFRESH_LIMIT) {
        return refresh_mode::fast;
    }
    return refresh_mode::fastest;
}

void record_completed_refresh(refresh_policy& policy, refresh_mode completed_mode)
{
    switch (completed_mode) {
        case refresh_mode::fastest:
            ++policy.fastest_count;
            break;
        case refresh_mode::fast:
            policy.fastest_count = 0;
            ++policy.fast_count;
            break;
        case refresh_mode::quality:
            policy.fastest_count = 0;
            policy.fast_count = 0;
            break;
    }
}

void draw_centered_line(const char* text, std::int32_t y, std::uint8_t text_size)
{
    M5.Display.setTextSize(text_size);
    M5.Display.drawString(text, M5.Display.width() / 2, y);
}

void draw_front_light_bar(
    std::uint8_t selected_button,
    std::int16_t pressed_button)
{
    const std::int32_t display_width = M5.Display.width();
    M5.Display.fillRect(0, 0, display_width, FRONT_LIGHT_BAR_HEIGHT, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(2);

    for (std::uint8_t index = 0; index < FRONT_LIGHT_LEVEL_COUNT; ++index) {
        const std::int32_t left = display_width * index / FRONT_LIGHT_LEVEL_COUNT;
        const std::int32_t right = display_width * (index + 1U) / FRONT_LIGHT_LEVEL_COUNT;
        const std::int32_t button_width = right - left;
        const bool is_pressed = pressed_button == static_cast<std::int16_t>(index);

        if (is_pressed) {
            M5.Display.fillRect(left, 0, button_width, FRONT_LIGHT_BAR_HEIGHT, TFT_BLACK);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        } else {
            M5.Display.fillRect(left, 0, button_width, FRONT_LIGHT_BAR_HEIGHT, TFT_WHITE);
            M5.Display.drawRect(left, 0, button_width, FRONT_LIGHT_BAR_HEIGHT, TFT_BLACK);
            if (index == selected_button) {
                // A double border marks the active level while keeping the button white.
                M5.Display.drawRect(
                    left + 3,
                    3,
                    button_width - 6,
                    FRONT_LIGHT_BAR_HEIGHT - 6,
                    TFT_BLACK);
            }
            M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
        }

        M5.Display.drawString(
            front_light_labels[index],
            left + button_width / 2,
            FRONT_LIGHT_BAR_HEIGHT / 2);
    }
}

void draw_request(
    const display_request& request,
    std::uint8_t selected_button,
    std::int16_t pressed_button)
{
    const auto height = M5.Display.height();
    char line_buffer[64];

    M5.Display.fillScreen(TFT_WHITE);
    draw_front_light_bar(selected_button, pressed_button);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    draw_centered_line(main_text(request.text_state), height / 2, 4);

    if (request.touch_type == touch_display_type::click) {
        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Point: (%d, %d)",
            request.end_x,
            request.end_y);
        draw_centered_line(line_buffer, height * 7 / 10, 2);

        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Time: %lu ms",
            static_cast<unsigned long>(request.duration_ms));
        draw_centered_line(line_buffer, height * 3 / 4, 2);
    } else if (request.touch_type == touch_display_type::long_press) {
        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Start: (%d, %d)",
            request.start_x,
            request.start_y);
        draw_centered_line(line_buffer, height * 13 / 20, 2);

        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "End: (%d, %d)",
            request.end_x,
            request.end_y);
        draw_centered_line(line_buffer, height * 7 / 10, 2);

        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Time: %lu ms",
            static_cast<unsigned long>(request.duration_ms));
        draw_centered_line(line_buffer, height * 3 / 4, 2);
    }
}

void refresh_full_screen(
    const display_request& request,
    refresh_mode active_mode,
    refresh_policy& policy,
    std::uint8_t selected_button,
    std::int16_t pressed_button,
    bool count_refresh,
    std::uint32_t& last_refresh_tick_ms)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    ESP_LOGI(
        log_tag,
        "full refresh start mode=%s fastest_count=%u fast_count=%u touch_type=%u",
        refresh_mode_name(active_mode),
        static_cast<unsigned>(policy.fastest_count),
        static_cast<unsigned>(policy.fast_count),
        static_cast<unsigned>(request.touch_type));
    draw_request(request, selected_button, pressed_button);
    M5.Display.display();
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();

    if (count_refresh) {
        record_completed_refresh(policy, active_mode);
    }
    ESP_LOGI(log_tag, "full refresh complete mode=%s", refresh_mode_name(active_mode));
}

void refresh_front_light_bar(
    refresh_mode active_mode,
    refresh_policy& policy,
    std::uint8_t selected_button,
    std::int16_t pressed_button,
    std::uint32_t& last_refresh_tick_ms)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    ESP_LOGI(
        log_tag,
        "partial refresh start mode=%s fastest_count=%u fast_count=%u selected=%u pressed=%d",
        refresh_mode_name(active_mode),
        static_cast<unsigned>(policy.fastest_count),
        static_cast<unsigned>(policy.fast_count),
        static_cast<unsigned>(selected_button),
        pressed_button);
    draw_front_light_bar(selected_button, pressed_button);
    M5.Display.display(0, 0, M5.Display.width(), FRONT_LIGHT_BAR_HEIGHT);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(policy, active_mode);
    ESP_LOGI(log_tag, "partial refresh complete mode=%s", refresh_mode_name(active_mode));
}

void process_front_light_request(
    const front_light_request& request,
    const display_request& latest_request,
    refresh_policy& policy,
    std::uint8_t& selected_button,
    std::int16_t& pressed_button,
    std::uint32_t& last_refresh_tick_ms)
{
    if (request.button_index >= FRONT_LIGHT_LEVEL_COUNT) {
        ESP_LOGW(
            log_tag,
            "invalid front light button index=%u",
            static_cast<unsigned>(request.button_index));
        return;
    }

    if (request.pressed) {
        pressed_button = request.button_index;
        // Press feedback prioritizes latency and always uses a partial Fastest refresh.
        refresh_front_light_bar(
            refresh_mode::fastest,
            policy,
            selected_button,
            pressed_button,
            last_refresh_tick_ms);
        return;
    }

    pressed_button = no_pressed_button;
    if (request.apply_level) {
        selected_button = request.button_index;
        hal_set_front_light(front_light_values[selected_button]);
        ESP_LOGI(
            log_tag,
            "front light applied index=%u brightness=%u",
            static_cast<unsigned>(selected_button),
            static_cast<unsigned>(front_light_values[selected_button]));
    }

    const refresh_mode release_mode = select_refresh_mode(policy);
    if (release_mode == refresh_mode::quality) {
        // Restore the button immediately, then clean accumulated EPD ghosting globally.
        refresh_front_light_bar(
            refresh_mode::fastest,
            policy,
            selected_button,
            pressed_button,
            last_refresh_tick_ms);
        refresh_full_screen(
            latest_request,
            refresh_mode::quality,
            policy,
            selected_button,
            pressed_button,
            true,
            last_refresh_tick_ms);
        return;
    }

    refresh_front_light_bar(
        release_mode,
        policy,
        selected_button,
        pressed_button,
        last_refresh_tick_ms);
}

bool display_request_is_due(
    const display_request& request,
    std::uint32_t last_refresh_tick_ms,
    bool has_refreshed)
{
    return !has_refreshed || request.minimum_refresh_interval_ms == 0 ||
           hal_get_tick_ms() - last_refresh_tick_ms >= request.minimum_refresh_interval_ms;
}

TickType_t display_request_wait_ticks(
    const display_request& request,
    std::uint32_t last_refresh_tick_ms,
    bool has_refreshed)
{
    if (display_request_is_due(request, last_refresh_tick_ms, has_refreshed)) {
        return 0;
    }

    const std::uint32_t elapsed_ms = hal_get_tick_ms() - last_refresh_tick_ms;
    return pdMS_TO_TICKS(request.minimum_refresh_interval_ms - elapsed_ms);
}

void display_task(void*)
{
    refresh_policy policy;
    display_request latest_request = {};
    display_request pending_request = {};
    std::uint8_t selected_button = FRONT_LIGHT_DEFAULT_LEVEL_INDEX;
    std::int16_t pressed_button = no_pressed_button;
    std::uint32_t last_refresh_tick_ms = 0;
    bool has_pending_request = false;
    bool has_refreshed = false;

    latest_request.text_state = ui_text_state::hi_xi;
    latest_request.touch_type = touch_display_type::none;

    for (;;) {
        const TickType_t wait_ticks = has_pending_request
                                          ? display_request_wait_ticks(
                                                pending_request,
                                                last_refresh_tick_ms,
                                                has_refreshed)
                                          : portMAX_DELAY;
        ulTaskNotifyTake(pdTRUE, wait_ticks);

        front_light_request light_request = {};
        while (xQueueReceive(front_light_queue, &light_request, 0) == pdTRUE) {
            process_front_light_request(
                light_request,
                latest_request,
                policy,
                selected_button,
                pressed_button,
                last_refresh_tick_ms);
            has_refreshed = true;
        }

        display_request newer_request = {};
        if (xQueueReceive(request_queue, &newer_request, 0) == pdTRUE) {
            pending_request = newer_request;
            has_pending_request = true;
        }

        if (!has_pending_request ||
            !display_request_is_due(pending_request, last_refresh_tick_ms, has_refreshed)) {
            continue;
        }

        latest_request = pending_request;
        const refresh_mode active_mode = pending_request.force_quality
                                             ? refresh_mode::quality
                                             : select_refresh_mode(policy);
        refresh_full_screen(
            pending_request,
            active_mode,
            policy,
            selected_button,
            pressed_button,
            !pending_request.force_quality,
            last_refresh_tick_ms);
        has_pending_request = false;
        has_refreshed = true;
    }
}

}  // namespace

bool hal_display_start(
    QueueHandle_t target_request_queue,
    QueueHandle_t target_front_light_queue,
    TaskHandle_t& task_handle)
{
    request_queue = target_request_queue;
    front_light_queue = target_front_light_queue;
    const bool started = xTaskCreate(
                             display_task,
                             "display_task",
                             DISPLAY_TASK_STACK_SIZE,
                             nullptr,
                             DISPLAY_TASK_PRIORITY,
                             &task_handle) == pdPASS;
    if (started) {
        ESP_LOGI(log_tag, "display task started");
    }
    return started;
}
