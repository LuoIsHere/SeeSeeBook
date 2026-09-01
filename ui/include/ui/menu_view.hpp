#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

inline constexpr std::size_t menu_view_entry_capacity = 4U;
inline constexpr std::size_t menu_view_label_capacity = 24U;

struct menu_entry_view_state {
    char label[menu_view_label_capacity];
};

struct menu_view_state {
    menu_entry_view_state entries[menu_view_entry_capacity];
    std::uint8_t entry_count;
};

static_assert(std::is_trivially_copyable_v<menu_view_state>);
