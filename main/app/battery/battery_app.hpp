#pragma once

#include "app_back_button.hpp"
#include "app_base.hpp"
#include "hal_display.hpp"

class battery_app final : public app_base {
public:
    battery_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_close() override;

private:
    app_back_button back_button_;
    battery_view_state view_ = {};
    bool has_snapshot_ = false;

    void handle_battery_event(const battery_event& event);
    void submit_frame(refresh_mode mode, display_update_region update_region);
};
