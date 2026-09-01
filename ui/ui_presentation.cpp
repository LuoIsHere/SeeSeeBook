#include "ui_presentation.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "ui_frame_pool.hpp"

namespace {

constexpr char log_tag[] = "ui_presentation";
portMUX_TYPE presentation_lock = portMUX_INITIALIZER_UNLOCKED;
std::uint32_t next_generation = 0U;
ui_view_id expected_view = ui_view_id::menu;
std::uint32_t expected_generation = 0U;
ui_view_id presented_view = ui_view_id::menu;
std::uint32_t presented_generation = 0U;
ui_frame_handle presented_handle = invalid_ui_frame_handle();
bool presented_rtc_controls_enabled = false;

std::uint32_t allocate_generation_locked()
{
    ++next_generation;
    if (next_generation == 0U) {
        ++next_generation;
    }
    return next_generation;
}

bool acquire_presented_frame(
    ui_view_id view,
    ui_frame_handle& handle,
    const display_request*& frame)
{
    handle = invalid_ui_frame_handle();
    frame = nullptr;
    // Lock order is presentation_lock then the short pool metadata lock. No
    // caller may enter presentation state while holding the pool lock.
    portENTER_CRITICAL(&presentation_lock);
    const bool available = presented_view == view &&
                           presented_generation != 0U &&
                           ui_frame_handle_is_valid(presented_handle);
    bool retained = false;
    if (available) {
        handle = presented_handle;
        retained = ui_frame_pool_retain(handle);
    }
    portEXIT_CRITICAL(&presentation_lock);
    if (!available || !retained) {
        return false;
    }

    portENTER_CRITICAL(&presentation_lock);
    const bool current = presented_view == view &&
                         presented_generation != 0U &&
                         ui_frame_handles_equal(presented_handle, handle);
    portEXIT_CRITICAL(&presentation_lock);
    if (!current || !ui_frame_pool_resolve(handle, frame)) {
        if (!ui_frame_pool_release(handle)) {
            ESP_LOGE(log_tag, "rejected frame release failed view=%u",
                     static_cast<unsigned>(view));
        }
        frame = nullptr;
        return false;
    }
    return true;
}

}  // namespace

ui_presentation_read_guard::ui_presentation_read_guard(ui_view_id view)
    : view_(view)
{
    if (view != ui_view_id::menu && view != ui_view_id::file) {
        return;
    }
    ui_frame_handle handle = invalid_ui_frame_handle();
    const display_request* frame = nullptr;
    if (!acquire_presented_frame(view, handle, frame) || frame == nullptr) {
        return;
    }
    if (view == ui_view_id::menu) {
        state_ = &frame->payload.menu;
    } else if (view == ui_view_id::file) {
        state_ = &frame->payload.file;
    }
    generation_ = handle.generation;
    index_ = handle.index;
}

ui_presentation_read_guard::~ui_presentation_read_guard()
{
    if (!valid()) {
        return;
    }
    ui_frame_handle handle = {generation_, index_};
    if (!ui_frame_pool_release(handle)) {
        ESP_LOGE(log_tag, "read guard release failed view=%u",
                 static_cast<unsigned>(view_));
    }
    generation_ = 0U;
    index_ = 0xFFU;
    state_ = nullptr;
}

bool ui_presentation_read_guard::valid() const
{
    return generation_ != 0U && state_ != nullptr;
}

const menu_view_state* ui_presentation_read_guard::menu_view() const
{
    return valid() && view_ == ui_view_id::menu
               ? static_cast<const menu_view_state*>(state_)
               : nullptr;
}

const file_view_state* ui_presentation_read_guard::file_view() const
{
    return valid() && view_ == ui_view_id::file
               ? static_cast<const file_view_state*>(state_)
               : nullptr;
}

std::uint32_t ui_presentation_select_view(ui_view_id view)
{
    portENTER_CRITICAL(&presentation_lock);
    const std::uint32_t generation = allocate_generation_locked();
    expected_view = view;
    expected_generation = generation;
    portEXIT_CRITICAL(&presentation_lock);
    return generation;
}

std::uint32_t ui_presentation_prepare_frame(
    ui_view_id view,
    bool view_opened)
{
    portENTER_CRITICAL(&presentation_lock);
    const std::uint32_t generation =
        view_opened && expected_view == view && expected_generation != 0U
            ? expected_generation
            : allocate_generation_locked();
    expected_view = view;
    expected_generation = generation;
    portEXIT_CRITICAL(&presentation_lock);
    return generation;
}

bool ui_presentation_commit_frame(
    const ui_frame_handle& handle,
    bool rtc_controls_enabled)
{
    const display_request* frame = nullptr;
    if (!ui_frame_pool_resolve(handle, frame) || frame == nullptr ||
        !ui_frame_pool_retain(handle)) {
        return false;
    }

    ui_frame_handle previous_handle = invalid_ui_frame_handle();
    portENTER_CRITICAL(&presentation_lock);
    previous_handle = presented_handle;
    presented_view = frame->view;
    presented_generation = frame->view_generation;
    presented_handle = handle;
    if (frame->view == ui_view_id::rtc_setting) {
        presented_rtc_controls_enabled = rtc_controls_enabled;
    }
    portEXIT_CRITICAL(&presentation_lock);
    if (ui_frame_handle_is_valid(previous_handle)) {
        if (!ui_frame_pool_release(previous_handle)) {
            ESP_LOGE(log_tag, "previous frame release failed view=%u",
                     static_cast<unsigned>(frame->view));
        }
    }
    return true;
}

bool ui_presentation_input_ready(ui_view_id view)
{
    portENTER_CRITICAL(&presentation_lock);
    const bool ready = expected_view == view && presented_view == view &&
                       expected_generation != 0U &&
                       expected_generation == presented_generation;
    portEXIT_CRITICAL(&presentation_lock);
    return ready;
}

bool ui_presentation_rtc_controls_enabled()
{
    portENTER_CRITICAL(&presentation_lock);
    const bool enabled = presented_view == ui_view_id::rtc_setting &&
                         presented_generation != 0U &&
                         presented_rtc_controls_enabled;
    portEXIT_CRITICAL(&presentation_lock);
    return enabled;
}
