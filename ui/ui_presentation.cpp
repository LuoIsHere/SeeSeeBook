#include "ui_presentation.hpp"

#include <freertos/FreeRTOS.h>

#include "ui_frame_pool.hpp"

namespace {

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
    portENTER_CRITICAL(&presentation_lock);
    const bool available = presented_view == view &&
                           presented_generation != 0U &&
                           ui_frame_handle_is_valid(presented_handle);
    if (available) {
        handle = presented_handle;
    }
    portEXIT_CRITICAL(&presentation_lock);
    if (!available || !ui_frame_pool_retain(handle)) {
        return false;
    }

    portENTER_CRITICAL(&presentation_lock);
    const bool current = presented_view == view &&
                         presented_generation != 0U &&
                         ui_frame_handles_equal(presented_handle, handle);
    portEXIT_CRITICAL(&presentation_lock);
    if (!current || !ui_frame_pool_resolve(handle, frame)) {
        ui_frame_pool_release(handle);
        frame = nullptr;
        return false;
    }
    return true;
}

}  // namespace

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
        ui_frame_pool_release(previous_handle);
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

bool ui_presentation_get_menu_view(menu_view_state& state)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    const display_request* frame = nullptr;
    if (!acquire_presented_frame(ui_view_id::menu, handle, frame) || frame == nullptr) {
        return false;
    }
    state = frame->payload.menu;
    ui_frame_pool_release(handle);
    return true;
}

bool ui_presentation_get_file_view(file_view_state& state)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    const display_request* frame = nullptr;
    if (!acquire_presented_frame(ui_view_id::file, handle, frame) || frame == nullptr) {
        return false;
    }
    state = frame->payload.file;
    ui_frame_pool_release(handle);
    return true;
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
