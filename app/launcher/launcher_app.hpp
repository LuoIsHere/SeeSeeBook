#pragma once

#include "app_base.hpp"
#include "launcher_view.hpp"
#include "selection_controller.hpp"

class launcher_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;

private:
    launcher_view_state view_ = {};
    selection_controller selection_;

    void activate_entry(std::uint8_t index);
    void handle_navigation(navigation_action action);
};
