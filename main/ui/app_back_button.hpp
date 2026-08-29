#pragma once

#include "hal_display.hpp"
#include "hal_touch.hpp"

enum class app_back_button_result : std::uint8_t {
    ignored,
    handled,
    clicked,
};

struct app_back_button_config {
    ui_rect rect;
};

class app_back_button {
public:
    explicit app_back_button(const app_back_button_config& config);

    // Handles capture, visual feedback, and click validation for one back button.
    app_back_button_result handle_touch(const touch_event& event);

    // Clears transient capture state before a lifecycle full-screen refresh.
    void reset();

private:
    app_back_button_config config_;
    bool active_ = false;

    void submit_feedback(bool pressed);
};
