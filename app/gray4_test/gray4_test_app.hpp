#pragma once

#include "app_base.hpp"

class gray4_test_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
};
