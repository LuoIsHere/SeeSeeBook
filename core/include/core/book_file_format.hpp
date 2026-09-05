#pragma once

#include <cstdint>
#include <string_view>

enum class book_file_format : std::uint8_t { unknown, txt, epub };

struct epub_position {
    std::uint16_t spine_index;
    std::uint64_t content_offset;
    std::uint64_t linear_offset;
};

constexpr std::uint16_t EPUB_SPINE_ITEM_LIMIT = 128U;

book_file_format book_file_format_from_name(std::string_view name);
