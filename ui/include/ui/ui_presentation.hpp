#pragma once

#include <cstdint>

#include "file_view.hpp"
#include "menu_view.hpp"
#include "ui_action.hpp"

class ui_presentation_read_guard final {
public:
    explicit ui_presentation_read_guard(ui_view_id view);
    ~ui_presentation_read_guard();

    ui_presentation_read_guard(const ui_presentation_read_guard&) = delete;
    ui_presentation_read_guard& operator=(const ui_presentation_read_guard&) = delete;
    ui_presentation_read_guard(ui_presentation_read_guard&&) = delete;
    ui_presentation_read_guard& operator=(ui_presentation_read_guard&&) = delete;

    bool valid() const;
    const menu_view_state* menu_view() const;
    const file_view_state* file_view() const;

private:
    std::uint32_t generation_ = 0U;
    std::uint8_t index_ = 0xFFU;
    ui_view_id view_ = ui_view_id::menu;
    const void* state_ = nullptr;
};

struct ui_frame_handle;

// Starts a page transition and invalidates input until that generation is
// physically committed by the renderer.
std::uint32_t ui_presentation_select_view(ui_view_id view);

// Publishes the next expected frame generation. A view-opening frame reuses
// the generation created by ui_presentation_select_view().
std::uint32_t ui_presentation_prepare_frame(
    ui_view_id view,
    bool view_opened);

// Commits hit-test state only after the corresponding physical refresh ends.
bool ui_presentation_commit_frame(
    const ui_frame_handle& handle,
    bool rtc_controls_enabled);

bool ui_presentation_input_ready(ui_view_id view);
bool ui_presentation_rtc_controls_enabled();
