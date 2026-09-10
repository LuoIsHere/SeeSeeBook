#pragma once

#include <cstdint>

#include "app_base.hpp"
#include "books_model.hpp"
#include "books_view.hpp"
#include "ui_renderer.hpp"

class books_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_close() override;

private:
    books_settings_model settings_;
    std::uint16_t page_index_ = 0U;
    std::uint16_t page_count_ = 1U;

    void handle_action(const ui_action_event& action);
    books_view_state build_view() const;
    void submit_frame(
        ui_update_reason reason,
        ui_control_type changed_control = ui_control_type::none);
    void update_status_bar() const;
};
