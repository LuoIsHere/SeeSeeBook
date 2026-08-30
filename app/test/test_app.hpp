#pragma once

#include <cstdint>

#include "app_base.hpp"
#include "test_view.hpp"

class test_app final : public app_base {
public:
    test_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;

private:
    test_view_state view_ = {};
};
