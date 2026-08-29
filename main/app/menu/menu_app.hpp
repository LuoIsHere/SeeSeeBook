#pragma once

#include "app_base.hpp"

class menu_app final : public app_base {
public:
    menu_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;

private:
    bool entry_active_ = false;

    void submit_frame(bool force_quality);
    void submit_entry_feedback(bool pressed);
};
