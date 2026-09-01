#include "ui_renderer.hpp"

#include <algorithm>
#include <cstdio>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "display.hpp"
#include "layout.hpp"
#include "renderer_internal.hpp"
#include "status_bar.hpp"
#include "ui_frame_pool.hpp"
#include "ui_presentation.hpp"
#include "views/renderer_helpers.hpp"
#include "views/view_renderer.hpp"

namespace {

constexpr char log_tag[] = "ui_renderer";
constexpr std::int16_t no_pressed_button = -1;
constexpr std::uint32_t stack_monitor_period_ms = 5000U;
constexpr std::uint32_t stack_warning_bytes = 2048U;
constexpr std::uint32_t stack_log_step_bytes = 256U;

QueueHandle_t request_queue = nullptr;
QueueHandle_t control_queue = nullptr;
TaskHandle_t renderer_task_handle = nullptr;

struct region_ghost_debt {
    std::uint8_t fastest_count = 0U;
    std::uint8_t fast_count = 0U;
    std::uint8_t text_count = 0U;
};

struct ghost_debt {
    region_ghost_debt control;
    region_ghost_debt rtc_editor;
    std::uint16_t status_bar = 0U;
    region_ghost_debt test_content;
    region_ghost_debt battery_content;
    region_ghost_debt file_content;
    bool status_cleanup_pending = false;
};

display_surface& canvas()
{
    return hal_display_surface();
}

std::uint32_t monotonic_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void monitor_renderer_stack()
{
    static std::uint32_t last_check_ms = 0U;
    static UBaseType_t lowest_reported_bytes = UINT32_MAX;
    const std::uint32_t now_ms = monotonic_ms();
    if (now_ms - last_check_ms < stack_monitor_period_ms) {
        return;
    }
    last_check_ms = now_ms;
    const UBaseType_t free_bytes = uxTaskGetStackHighWaterMark(nullptr);
    const bool crossed_warning = free_bytes < stack_warning_bytes &&
                                 lowest_reported_bytes >= stack_warning_bytes;
    const bool meaningful_drop = lowest_reported_bytes == UINT32_MAX ||
                                 free_bytes + stack_log_step_bytes <=
                                     lowest_reported_bytes;
    if (!crossed_warning && !meaningful_drop) {
        return;
    }
    lowest_reported_bytes = free_bytes;
    const ui_frame_pool_stats stats = ui_frame_pool_get_stats();
    if (free_bytes < stack_warning_bytes) {
        ESP_LOGW(
            log_tag,
            "renderer stack low water=%lu bytes frame_pool=%u/%u peak=%u",
            static_cast<unsigned long>(free_bytes),
            stats.active_count,
            UI_FRAME_POOL_CAPACITY,
            stats.peak_active_count);
    } else {
        ESP_LOGI(
            log_tag,
            "renderer stack low water=%lu bytes frame_pool=%u/%u peak=%u",
            static_cast<unsigned long>(free_bytes),
            stats.active_count,
            UI_FRAME_POOL_CAPACITY,
            stats.peak_active_count);
    }
}

const char* refresh_mode_name(refresh_mode mode)
{
    switch (mode) {
        case refresh_mode::fastest:
            return "fastest";
        case refresh_mode::text:
            return "text";
        case refresh_mode::fast:
            return "fast";
        case refresh_mode::quality:
            return "quality";
    }
    return "unknown";
}

region_ghost_debt& debt_for_region(ghost_debt& debt, display_update_region region)
{
    switch (region) {
        case display_update_region::control:
            return debt.control;
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            return debt.rtc_editor;
        case display_update_region::status_bar:
            return debt.control;
        case display_update_region::test_content:
            return debt.test_content;
        case display_update_region::battery_content:
            return debt.battery_content;
        case display_update_region::file_content:
            return debt.file_content;
        case display_update_region::full:
            return debt.control;
    }
    return debt.control;
}

refresh_mode resolve_mode(
    refresh_mode requested,
    display_update_region region,
    bool allow_cleanup,
    ghost_debt& debt)
{
    if (requested == refresh_mode::quality || region == display_update_region::status_bar ||
        !allow_cleanup) {
        return requested;
    }
    const region_ghost_debt& value = debt_for_region(debt, region);
    if (requested == refresh_mode::fastest &&
        value.fastest_count + 1U >= FASTEST_REFRESHES_BEFORE_FAST) {
        return value.fast_count + 1U >= FAST_REFRESHES_BEFORE_QUALITY
                   ? refresh_mode::quality
                   : refresh_mode::fast;
    }
    if (requested == refresh_mode::fast &&
        value.fast_count + 1U >= FAST_REFRESHES_BEFORE_QUALITY) {
        return refresh_mode::quality;
    }
    if (requested == refresh_mode::text &&
        value.text_count + 1U >= FILE_TEXT_REFRESHES_BEFORE_QUALITY) {
        return refresh_mode::quality;
    }
    return requested;
}

void record_refresh(
    ghost_debt& debt,
    refresh_mode mode,
    display_update_region region)
{
    if (mode == refresh_mode::quality) {
        // Every quality request is rendered as a full frame and cleans all regions.
        debt = {};
        return;
    }
    if (region == display_update_region::status_bar) {
        if (debt.status_bar < UINT16_MAX) {
            ++debt.status_bar;
        }
        if (debt.status_bar >= STATUS_BAR_GHOST_DEBT_LIMIT) {
            debt.status_cleanup_pending = true;
        }
        return;
    }
    region_ghost_debt& value = debt_for_region(debt, region);
    if (mode == refresh_mode::fastest) {
        if (value.fastest_count < FASTEST_REFRESHES_BEFORE_FAST) {
            ++value.fastest_count;
        }
    } else if (mode == refresh_mode::fast) {
        value.fastest_count = 0U;
        if (value.fast_count < FAST_REFRESHES_BEFORE_QUALITY) {
            ++value.fast_count;
        }
    } else if (mode == refresh_mode::text && value.text_count < UINT8_MAX) {
        ++value.text_count;
    }
}

display_refresh_result commit_refresh(
    ghost_debt& debt,
    const display_rect& rect,
    refresh_mode requested_mode,
    display_update_region region)
{
    display_refresh_result result = hal_display_refresh(rect, requested_mode);
    if (!result.success) {
        // Retry once with a full monochrome cycle. The HAL marks an uncertain
        // differential baseline invalid after every failed activation.
        ESP_LOGW(
            log_tag,
            "refresh failed mode=%s; attempting one quality recovery",
            refresh_mode_name(requested_mode));
        result = hal_display_refresh(
            {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT},
            refresh_mode::quality);
    }
    if (result.success) {
        record_refresh(debt, result.actual_mode, region);
        if (result.actual_mode != requested_mode) {
            ESP_LOGI(
                log_tag,
                "HAL upgraded refresh requested=%s actual=%s duration=%lu",
                refresh_mode_name(requested_mode),
                refresh_mode_name(result.actual_mode),
                static_cast<unsigned long>(result.duration_ms));
        }
    } else {
        ESP_LOGE(log_tag, "display refresh and bounded recovery both failed");
    }
    return result;
}

display_rect merged_rect(const display_rect& first, const display_rect& second)
{
    const std::int16_t left = std::min(first.left, second.left);
    const std::int16_t top = std::min(first.top, second.top);
    const std::int16_t right = std::max<std::int16_t>(
        first.left + first.width,
        second.left + second.width);
    const std::int16_t bottom = std::max<std::int16_t>(
        first.top + first.height,
        second.top + second.height);
    return {left, top, static_cast<std::int16_t>(right - left),
            static_cast<std::int16_t>(bottom - top)};
}

bool status_states_equal(
    const status_bar_view_state& left,
    const status_bar_view_state& right)
{
    const bool time_equal = left.time_valid == right.time_valid &&
                            (!left.time_valid ||
                             (left.hour == right.hour && left.minute == right.minute));
    const bool battery_equal =
        left.battery.level_valid == right.battery.level_valid &&
        (!left.battery.level_valid || left.battery.percent == right.battery.percent) &&
        left.battery.charging_valid == right.battery.charging_valid &&
        (!left.battery.charging_valid || left.battery.charging == right.battery.charging);
    return time_equal && battery_equal;
}

void draw_status_bar(const status_bar_view_state& state)
{
    const display_rect rect = status_bar_rect();
    canvas().fill_rect(rect, display_color::white);
    canvas().set_text_color(display_color::black, display_color::white);
    canvas().set_text_size(STATUS_BAR_TEXT_SIZE);

    char buffer[12] = {};
    if (state.time_valid) {
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u", state.hour, state.minute);
    } else {
        std::snprintf(buffer, sizeof(buffer), "--:--");
    }
    canvas().set_text_alignment(display_text_alignment::middle_left);
    canvas().draw_text(buffer, STATUS_BAR_LEFT_MARGIN, STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);

    if (state.battery.level_valid) {
        std::snprintf(buffer, sizeof(buffer), "%u%%", state.battery.percent);
    } else {
        std::snprintf(buffer, sizeof(buffer), "--%%");
    }
    const std::int16_t percent_right = UI_DISPLAY_WIDTH - STATUS_BAR_RIGHT_MARGIN;
    canvas().set_text_alignment(display_text_alignment::middle_right);
    canvas().draw_text(buffer, percent_right, STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);
    if (state.battery.charging_valid && state.battery.charging) {
        const std::int16_t left = percent_right - STATUS_BATTERY_PERCENT_MAX_WIDTH - 30;
        const std::int16_t top = STATUS_BAR_TOP + 5;
        canvas().fill_triangle(
            left + 16, top, left + 5, top + 18, left + 14, top + 18,
            display_color::black);
        canvas().fill_triangle(
            left + 13, top + 13, left + 24, top + 13, left + 10, top + 30,
            display_color::black);
    }
}

void draw_full_view(
    const display_request& request,
    std::uint8_t selected_light,
    std::int16_t pressed_light)
{
    canvas().fill_screen(display_color::white);
    canvas().set_text_color(display_color::black, display_color::white);
    switch (request.view) {
        case ui_view_id::menu:
            paper_mono_views::draw_menu_view(canvas(), request.payload.menu);
            break;
        case ui_view_id::test:
            paper_mono_views::draw_test_view(
                canvas(), request.payload.test, selected_light, pressed_light);
            break;
        case ui_view_id::rtc_setting:
            paper_mono_views::draw_rtc_view(canvas(), request.payload.rtc);
            break;
        case ui_view_id::battery:
            paper_mono_views::draw_battery_view(canvas(), request.payload.battery);
            break;
        case ui_view_id::file:
            paper_mono_views::draw_file_view(canvas(), request.payload.file);
            break;
    }
}

display_rect content_rect(display_update_region region)
{
    switch (region) {
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            return {0, RTC_EDITOR_REGION_TOP, UI_DISPLAY_WIDTH, RTC_EDITOR_REGION_HEIGHT};
        case display_update_region::test_content:
            return {0, TEST_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH, TEST_CONTENT_REGION_HEIGHT};
        case display_update_region::battery_content:
            return {0, BATTERY_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH,
                    BATTERY_CONTENT_REGION_HEIGHT};
        case display_update_region::file_content:
            return {0, FILE_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH, FILE_CONTENT_REGION_HEIGHT};
        case display_update_region::status_bar:
            return status_bar_rect();
        case display_update_region::full:
            return {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
        case display_update_region::control:
            return {0, 0, UI_DISPLAY_WIDTH, FRONT_LIGHT_BAR_HEIGHT};
    }
    return {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
}

void draw_partial_request(const display_request& request, display_rect& rect)
{
    rect = content_rect(request.update_region);
    switch (request.update_region) {
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            paper_mono_views::draw_rtc_editor(canvas(), request.payload.rtc);
            for (std::uint8_t index = 0U; index < RTC_KEY_COUNT; ++index) {
                if ((request.released_key_mask & (1U << index)) != 0U) {
                    paper_mono_views::draw_rtc_key(
                        canvas(),
                        index,
                        false,
                        paper_mono_views::rtc_keys_enabled(request.payload.rtc));
                    rect = merged_rect(rect, rtc_key_rect(index));
                }
            }
            break;
        case display_update_region::test_content:
            paper_mono_views::draw_test_content(canvas(), request.payload.test);
            break;
        case display_update_region::battery_content:
            paper_mono_views::draw_battery_content(canvas(), request.payload.battery);
            break;
        case display_update_region::file_content:
            paper_mono_views::draw_file_content(canvas(), request.payload.file);
            break;
        case display_update_region::control:
            if (request.view == ui_view_id::test) {
                paper_mono_views::draw_front_light_bar(
                    canvas(), request.payload.test.front_light_level, no_pressed_button);
            }
            break;
        case display_update_region::full:
        case display_update_region::status_bar:
            break;
    }
}

display_rect draw_control(
    const display_control_request& request,
    const display_request& latest,
    std::uint8_t selected_light,
    std::int16_t pressed_light)
{
    switch (request.control) {
        case ui_control_type::front_light:
            paper_mono_views::draw_front_light_bar(
                canvas(), selected_light, pressed_light);
            return {0, 0, UI_DISPLAY_WIDTH, FRONT_LIGHT_BAR_HEIGHT};
        case ui_control_type::menu_entry:
            paper_mono_views::draw_menu_entry(
                canvas(), latest.payload.menu, request.index, request.pressed);
            return menu_entry_rect(request.index);
        case ui_control_type::navigate_back:
            paper_mono_views::draw_back_button(
                canvas(), latest.view, request.pressed);
            return app_back_button_rect(latest.view);
        case ui_control_type::rtc_key:
            paper_mono_views::draw_rtc_key(
                canvas(),
                request.index,
                request.pressed,
                paper_mono_views::rtc_keys_enabled(latest.payload.rtc));
            return rtc_key_rect(request.index);
        case ui_control_type::file_row:
            paper_mono_views::draw_file_row(
                canvas(), latest.payload.file, request.index, request.pressed);
            return file_row_rect(request.index);
        case ui_control_type::file_previous_page:
            paper_mono_views::draw_file_page_button(
                canvas(), latest.payload.file, false, request.pressed);
            return file_previous_page_rect();
        case ui_control_type::file_next_page:
            paper_mono_views::draw_file_page_button(
                canvas(), latest.payload.file, true, request.pressed);
            return file_next_page_rect();
        case ui_control_type::none:
        case ui_control_type::rtc_field:
        case ui_control_type::test_surface:
            break;
    }
    return {0, 0, 0, 0};
}

bool queued_not_after(std::uint32_t left, std::uint32_t right)
{
    return static_cast<std::int32_t>(right - left) >= 0;
}

bool control_replaced_by_frame(
    const display_control_request& control,
    const display_request& frame,
    refresh_mode frame_mode,
    display_update_region frame_region)
{
    if (!queued_not_after(control.queued_at_ms, frame.queued_at_ms)) {
        return false;
    }
    if (frame_mode == refresh_mode::quality ||
        frame_region == display_update_region::full) {
        return true;
    }
    if (control.control == ui_control_type::rtc_key &&
        frame.view == ui_view_id::rtc_setting &&
        frame_region == display_update_region::rtc_editor_and_key &&
        control.index < RTC_KEY_COUNT &&
        (frame.released_key_mask & (1U << control.index)) != 0U) {
        return true;
    }
    if (control.control == ui_control_type::front_light &&
        frame.view == ui_view_id::test &&
        frame_region == display_update_region::control) {
        return true;
    }
    const bool file_control =
        control.control == ui_control_type::file_row ||
        control.control == ui_control_type::file_previous_page ||
        control.control == ui_control_type::file_next_page;
    return file_control && frame.view == ui_view_id::file &&
           frame_region == display_update_region::file_content;
}

void process_control_request(
    const display_control_request& control,
    const display_request& latest,
    ghost_debt& debt,
    std::uint8_t selected_light,
    std::int16_t& pressed_light,
    status_bar_view_state& displayed_status,
    bool& status_displayed,
    bool has_frame)
{
    if (!has_frame) {
        return;
    }
    if (control.control == ui_control_type::front_light) {
        pressed_light = control.pressed ? control.index : no_pressed_button;
    }
    display_rect rect =
        draw_control(control, latest, selected_light, pressed_light);
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }

    refresh_mode mode = resolve_mode(
        control.mode,
        control.update_region,
        control.allow_quality_cleanup,
        debt);
    if (mode == refresh_mode::quality) {
        draw_full_view(latest, selected_light, pressed_light);
        displayed_status = status_bar_get_state();
        draw_status_bar(displayed_status);
        rect = {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
        status_displayed = true;
    }
    const std::uint32_t start_ms = monotonic_ms();
    commit_refresh(debt, rect, mode, control.update_region);
    ESP_LOGI(
        log_tag,
        "stage=control_refresh control=%u pressed=%d queue_wait_ms=%lu duration_ms=%lu",
        static_cast<unsigned>(control.control),
        control.pressed,
        static_cast<unsigned long>(start_ms - control.queued_at_ms),
        static_cast<unsigned long>(monotonic_ms() - start_ms));
}

bool release_frame(ui_frame_handle& handle, const char* owner)
{
    if (!ui_frame_handle_is_valid(handle)) {
        return true;
    }
    if (ui_frame_pool_release(handle)) {
        return true;
    }
    ESP_LOGE(log_tag, "frame release failed owner=%s", owner);
    return false;
}

bool reclaim_queued_frame(bool& quality_pending)
{
    if (request_queue == nullptr) {
        return false;
    }
    ui_frame_handle discarded = invalid_ui_frame_handle();
    if (xQueueReceive(request_queue, &discarded, 0) != pdTRUE) {
        return false;
    }
    const display_request* discarded_frame = nullptr;
    if (ui_frame_pool_resolve(discarded, discarded_frame) &&
        discarded_frame != nullptr) {
        quality_pending = quality_pending ||
                          discarded_frame->mode == refresh_mode::quality;
    }
    release_frame(discarded, "queue_reclaim");
    return true;
}

bool acquire_request(
    ui_view_id view,
    ui_update_reason reason,
    ui_frame_handle& handle,
    display_request*& request)
{
    bool quality_pending = false;
    if (!ui_frame_pool_acquire(handle, request)) {
        if (!reclaim_queued_frame(quality_pending) ||
            !ui_frame_pool_acquire(handle, request)) {
            const ui_frame_pool_stats stats = ui_frame_pool_get_stats();
            ESP_LOGW(
                log_tag,
                "frame pool exhausted active=%u peak=%u",
                stats.active_count,
                stats.peak_active_count);
            return false;
        }
    }
    request->view_generation = ui_presentation_prepare_frame(
        view,
        reason == ui_update_reason::view_opened);
    request->view = view;
    request->mode = reason == ui_update_reason::view_opened || quality_pending
                        ? refresh_mode::quality
                        : refresh_mode::fastest;
    request->update_region = reason == ui_update_reason::view_opened || quality_pending
                                 ? display_update_region::full
                                 : display_update_region::control;
    request->allow_quality_cleanup = true;
    return true;
}

bool submit_request(
    ui_frame_handle& handle,
    display_request& request)
{
    if (request_queue == nullptr || renderer_task_handle == nullptr) {
        release_frame(handle, "submit_unavailable");
        return false;
    }
    request.queued_at_ms = monotonic_ms();
    if (uxQueueSpacesAvailable(request_queue) == 0U) {
        bool quality_pending = false;
        if (!reclaim_queued_frame(quality_pending)) {
            release_frame(handle, "submit_queue_full");
            return false;
        }
        if (quality_pending) {
            request.mode = refresh_mode::quality;
            request.update_region = display_update_region::full;
        }
    }
    if (!ui_frame_pool_publish(handle)) {
        release_frame(handle, "publish_failure");
        return false;
    }
    if (xQueueSend(request_queue, &handle, 0) != pdTRUE) {
        release_frame(handle, "submit_race_failure");
        return false;
    }
    handle = invalid_ui_frame_handle();
    xTaskNotifyGive(renderer_task_handle);
    return true;
}

void renderer_task(void*)
{
    ghost_debt debt = {};
    ui_frame_handle latest_handle = invalid_ui_frame_handle();
    const display_request* latest = nullptr;
    std::uint8_t selected_light = FRONT_LIGHT_DEFAULT_LEVEL_INDEX;
    std::int16_t pressed_light = no_pressed_button;
    status_bar_view_state displayed_status = {};
    bool has_frame = false;
    bool status_displayed = false;

    for (;;) {
        if (ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(DISPLAY_IDLE_SLEEP_MS)) == 0U) {
            if (!hal_display_sleep()) {
                ESP_LOGW(log_tag, "idle display sleep failed");
            }
            monitor_renderer_stack();
            continue;
        }

        ui_frame_handle next_handle = invalid_ui_frame_handle();
        const display_request* next = nullptr;
        bool has_request = false;
        bool quality_pending = false;
        ui_frame_handle candidate = invalid_ui_frame_handle();
        while (xQueueReceive(request_queue, &candidate, 0) == pdTRUE) {
            const display_request* candidate_frame = nullptr;
            if (!ui_frame_pool_resolve(candidate, candidate_frame) ||
                candidate_frame == nullptr) {
                ESP_LOGE(log_tag, "queued frame handle is invalid");
                candidate = invalid_ui_frame_handle();
                continue;
            }
            quality_pending = quality_pending ||
                              candidate_frame->mode == refresh_mode::quality;
            release_frame(next_handle, "renderer_superseded");
            next_handle = candidate;
            next = candidate_frame;
            candidate = invalid_ui_frame_handle();
            has_request = true;
        }

        display_control_request controls[DISPLAY_CONTROL_QUEUE_LENGTH] = {};
        std::size_t control_count = 0U;
        while (control_count < DISPLAY_CONTROL_QUEUE_LENGTH &&
               xQueueReceive(control_queue, &controls[control_count], 0) == pdTRUE) {
            ++control_count;
        }
        if (has_request && next != nullptr) {
            const refresh_mode queued_mode = quality_pending
                                                 ? refresh_mode::quality
                                                 : next->mode;
            const display_update_region queued_region = quality_pending
                                                            ? display_update_region::full
                                                            : next->update_region;
            for (std::size_t index = 0U; index < control_count; ++index) {
                if (!control_replaced_by_frame(
                        controls[index], *next, queued_mode, queued_region)) {
                    continue;
                }
                if (controls[index].control == ui_control_type::front_light) {
                    pressed_light = no_pressed_button;
                }
                ESP_LOGI(
                    log_tag,
                    "discard transient control=%u pressed=%d before frame view=%u",
                    static_cast<unsigned>(controls[index].control),
                    controls[index].pressed,
                    static_cast<unsigned>(next->view));
                controls[index].control = ui_control_type::none;
            }

            selected_light = next->view == ui_view_id::test
                                 ? next->payload.test.front_light_level
                                 : selected_light;
            const refresh_mode requested_mode =
                debt.status_cleanup_pending ? refresh_mode::quality : queued_mode;
            refresh_mode mode = resolve_mode(
                requested_mode,
                queued_region,
                next->allow_quality_cleanup,
                debt);
            display_rect rect = content_rect(queued_region);
            const std::uint32_t draw_start_ms = monotonic_ms();
            if (!has_frame || mode == refresh_mode::quality ||
                queued_region == display_update_region::full) {
                draw_full_view(*next, selected_light, pressed_light);
                rect = {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
            } else {
                draw_partial_request(*next, rect);
            }
            const status_bar_view_state status = status_bar_get_state();
            if (!status_displayed || !status_states_equal(status, displayed_status) ||
                rect.height == UI_DISPLAY_HEIGHT) {
                draw_status_bar(status);
                displayed_status = status;
                status_displayed = true;
                rect = merged_rect(rect, status_bar_rect());
            }
            const std::uint32_t refresh_start_ms = monotonic_ms();
            const display_refresh_result refresh =
                commit_refresh(debt, rect, mode, queued_region);
            has_frame = refresh.success;
            status_displayed = refresh.success;
            if (refresh.success) {
                const bool presentation_committed = ui_presentation_commit_frame(
                    next_handle,
                    next->view == ui_view_id::rtc_setting &&
                        paper_mono_views::rtc_keys_enabled(next->payload.rtc));
                if (!presentation_committed) {
                    ESP_LOGE(log_tag, "presented frame commit failed");
                }
                release_frame(latest_handle, "renderer_previous_latest");
                latest_handle = next_handle;
                latest = next;
                next_handle = invalid_ui_frame_handle();
            }
            ESP_LOGI(
                log_tag,
                "stage=view_refresh requested=%s actual=%s success=%d view=%u generation=%lu region=%u queue_wait_ms=%lu draw_ms=%lu refresh_ms=%lu",
                refresh_mode_name(mode),
                refresh_mode_name(refresh.actual_mode),
                refresh.success,
                static_cast<unsigned>(next->view),
                static_cast<unsigned long>(next->view_generation),
                static_cast<unsigned>(queued_region),
                static_cast<unsigned long>(draw_start_ms - next->queued_at_ms),
                static_cast<unsigned long>(refresh_start_ms - draw_start_ms),
                static_cast<unsigned long>(monotonic_ms() - refresh_start_ms));
            if (!refresh.success) {
                release_frame(next_handle, "renderer_refresh_failure");
            }
        }

        for (std::size_t index = 0U; index < control_count; ++index) {
            if (controls[index].control == ui_control_type::none) {
                continue;
            }
            if (latest != nullptr) {
                process_control_request(
                    controls[index],
                    *latest,
                    debt,
                    selected_light,
                    pressed_light,
                    displayed_status,
                    status_displayed,
                    has_frame);
            }
        }

        const status_bar_view_state status = status_bar_get_state();
        if (has_frame && (!status_displayed || !status_states_equal(status, displayed_status))) {
            draw_status_bar(status);
            const display_refresh_result refresh = commit_refresh(
                debt,
                status_bar_rect(),
                refresh_mode::fastest,
                display_update_region::status_bar);
            if (refresh.success) {
                displayed_status = status;
                status_displayed = true;
            }
        }
        monitor_renderer_stack();
    }
}

}  // namespace

esp_err_t ui_renderer_init()
{
    request_queue = xQueueCreate(DISPLAY_REQUEST_QUEUE_LENGTH, sizeof(ui_frame_handle));
    control_queue = xQueueCreate(DISPLAY_CONTROL_QUEUE_LENGTH, sizeof(display_control_request));
    if (request_queue == nullptr || control_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(renderer_task, "ui_renderer", DISPLAY_TASK_STACK_SIZE, nullptr,
                    DISPLAY_TASK_PRIORITY, &renderer_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        log_tag,
        "PaperMono renderer started frame_size=%u pool_capacity=%u",
        static_cast<unsigned>(sizeof(display_request)),
        UI_FRAME_POOL_CAPACITY);
    return ESP_OK;
}

bool ui_render_menu(const menu_view_state& state, ui_update_reason reason)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    display_request* request = nullptr;
    if (!acquire_request(ui_view_id::menu, reason, handle, request) || request == nullptr) {
        return false;
    }
    request->payload.menu = state;
    return submit_request(handle, *request);
}

bool ui_render_test(const test_view_state& state, ui_update_reason reason)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    display_request* request = nullptr;
    if (!acquire_request(ui_view_id::test, reason, handle, request) || request == nullptr) {
        return false;
    }
    request->payload.test = state;
    if (reason != ui_update_reason::view_opened &&
        request->mode != refresh_mode::quality) {
        const bool light_selection = reason == ui_update_reason::selection_changed;
        request->mode = light_selection || state.touch_type != test_touch_display_type::click
                            ? refresh_mode::fastest
                            : refresh_mode::fast;
        request->update_region = light_selection ? display_update_region::control
                                                 : display_update_region::test_content;
    }
    return submit_request(handle, *request);
}

bool ui_render_rtc(
    const rtc_view_state& state,
    ui_update_reason reason,
    ui_control_type released_control,
    std::uint8_t released_index)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    display_request* request = nullptr;
    if (!acquire_request(ui_view_id::rtc_setting, reason, handle, request) ||
        request == nullptr) {
        return false;
    }
    request->payload.rtc = state;
    if (reason != ui_update_reason::view_opened &&
        request->mode != refresh_mode::quality) {
        request->update_region = released_control == ui_control_type::rtc_key
                                     ? display_update_region::rtc_editor_and_key
                                     : display_update_region::rtc_editor;
        if (released_control == ui_control_type::rtc_key && released_index < RTC_KEY_COUNT) {
            request->released_key_mask = static_cast<std::uint16_t>(1U << released_index);
        }
    }
    return submit_request(handle, *request);
}

bool ui_render_battery(const battery_view_state& state, ui_update_reason reason)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    display_request* request = nullptr;
    if (!acquire_request(ui_view_id::battery, reason, handle, request) || request == nullptr) {
        return false;
    }
    request->payload.battery = state;
    if (reason != ui_update_reason::view_opened &&
        request->mode != refresh_mode::quality) {
        request->update_region = display_update_region::battery_content;
    }
    return submit_request(handle, *request);
}

bool ui_render_file(const file_view_state& state, ui_update_reason reason)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    display_request* request = nullptr;
    if (!acquire_request(ui_view_id::file, reason, handle, request) || request == nullptr) {
        return false;
    }
    request->payload.file = state;
    if (reason != ui_update_reason::view_opened &&
        request->mode != refresh_mode::quality) {
        request->mode = refresh_mode::text;
        request->update_region = display_update_region::file_content;
    }
    return submit_request(handle, *request);
}

bool ui_render_control(ui_control_type control, std::uint8_t index, bool pressed)
{
    if (control_queue == nullptr || renderer_task_handle == nullptr) {
        return false;
    }
    display_control_request request = {};
    request.queued_at_ms = monotonic_ms();
    request.control = control;
    request.mode = refresh_mode::fastest;
    request.update_region = display_update_region::control;
    request.index = index;
    request.pressed = pressed;
    request.allow_quality_cleanup = !pressed;
    if (xQueueSend(control_queue, &request, 0) != pdTRUE) {
        return false;
    }
    xTaskNotifyGive(renderer_task_handle);
    return true;
}

void ui_renderer_notify_status_bar()
{
    if (renderer_task_handle != nullptr) {
        xTaskNotifyGive(renderer_task_handle);
    }
}
