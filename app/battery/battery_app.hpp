#pragma once

#include "app_base.hpp"
#include "battery_view.hpp"

class battery_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_close() override;

private:
    battery_view_state view_ = {};
    battery_snapshot snapshot_ = {};
    bool has_snapshot_ = false;

    void handle_battery_event(const app_battery_event& event);
    void update_view();
};
