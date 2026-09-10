#pragma once

#include <cstdint>

#include "books_view.hpp"
#include "ui_action.hpp"

bool books_hit_test(
    const books_view_state& state,
    std::int16_t x,
    std::int16_t y,
    ui_control_type& control,
    std::uint8_t& index);

bool books_control_contains(
    ui_control_type control,
    std::uint8_t index,
    std::int16_t x,
    std::int16_t y);
