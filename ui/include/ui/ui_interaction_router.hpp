#pragma once

#include "file_view.hpp"
#include "input_event.hpp"
#include "ui_action.hpp"

void ui_interaction_set_view(ui_view_id view);
void ui_interaction_set_file_view(const file_view_state& state);
void ui_interaction_set_rtc_controls_enabled(bool enabled);

// Maps normalized coordinates to semantic controls and emits at most one action.
bool ui_interaction_process(
    const input_event& input,
    ui_action_event& action);
