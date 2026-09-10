#pragma once

#include "app_base.hpp"
#include "launcher_view.hpp"

class launcher_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;

private:
    launcher_view_state view_ = {};
};
