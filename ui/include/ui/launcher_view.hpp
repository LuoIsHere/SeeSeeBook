#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

inline constexpr std::size_t launcher_view_entry_capacity = 3U;
inline constexpr std::size_t launcher_view_label_capacity = 16U;
inline constexpr std::uint8_t launcher_no_selection = UINT8_MAX;

struct launcher_entry_view_state {
    char label[launcher_view_label_capacity];
};

struct launcher_view_state {
    launcher_entry_view_state entries[launcher_view_entry_capacity];
    std::uint8_t entry_count;
    std::uint8_t selected_index = launcher_no_selection;
};

static_assert(std::is_trivially_copyable_v<launcher_view_state>);
