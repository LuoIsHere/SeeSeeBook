#pragma once

#include <cstdint>

#include "file_view.hpp"
#include "menu_view.hpp"
#include "ui_action.hpp"

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
bool ui_presentation_get_menu_view(menu_view_state& state);
bool ui_presentation_get_file_view(file_view_state& state);
bool ui_presentation_rtc_controls_enabled();
