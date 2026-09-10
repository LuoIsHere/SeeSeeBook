#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "book_file_format.hpp"

inline constexpr std::size_t books_view_item_capacity = 6U;
inline constexpr std::size_t books_file_name_line_count = 2U;
inline constexpr std::size_t books_file_name_line_capacity = 40U;

struct books_file_name_view_state {
    char lines[books_file_name_line_count][books_file_name_line_capacity];
    std::uint8_t line_count;
};

struct books_item_view_state {
    books_file_name_view_state file_name;
    book_file_format format;
    bool occupied;
    bool enabled;
};

struct books_settings_view_state {
    bool auto_scan_txt;
    bool auto_scan_epub;
};

struct books_view_state {
    books_item_view_state items[books_view_item_capacity];
    books_settings_view_state pending_settings;
    std::uint16_t page_index;
    std::uint16_t page_count;
    std::uint8_t item_count;
    bool settings_visible;
};

static_assert(std::is_trivially_copyable_v<books_view_state>);
