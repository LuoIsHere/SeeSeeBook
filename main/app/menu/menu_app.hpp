#pragma once

#include <cstdint>

#include "app_base.hpp"

class menu_app final : public app_base {
public:
    menu_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;

private:
    std::int8_t active_entry_index_ = -1;

    void submit_frame(bool force_quality);
    void submit_entry_feedback(std::uint8_t entry_index, bool pressed);
};
