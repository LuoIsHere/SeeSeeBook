#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

constexpr std::size_t READER_PAGE_TEXT_CAPACITY = 2048U;
constexpr std::size_t READER_PAGE_LINE_CAPACITY = 32U;

enum class reader_view_status : std::uint8_t {
    loading, ready, empty_file, invalid_utf8, file_not_found, storage_error, no_card,
};

struct reader_line {
    std::uint16_t offset;
    std::uint16_t length;
};

struct reader_page {
    char text[READER_PAGE_TEXT_CAPACITY];
    reader_line lines[READER_PAGE_LINE_CAPACITY];
    std::uint64_t current_page_start_offset;
    std::uint64_t next_page_start_offset;
    std::uint16_t text_length;
    std::uint16_t line_count;
    bool end_of_file;
    bool empty;
};

struct reader_view_state {
    reader_page page;
    std::uint64_t file_size;
    reader_view_status status;
    bool previous_enabled;
    bool next_enabled;
    bool progress_persistent;
};

static_assert(std::is_trivially_copyable_v<reader_view_state>);
static_assert(sizeof(reader_view_state) <= 2240U);
