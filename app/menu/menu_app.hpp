#pragma once

#include "app_base.hpp"
#include "menu_view.hpp"

class menu_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;

private:
    menu_view_state view_ = {};
};
