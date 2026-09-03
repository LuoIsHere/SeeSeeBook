#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::size_t READER_PAGE_TEXT_CAPACITY = 2048U;
constexpr std::size_t READER_PAGE_LINE_CAPACITY = 32U;

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
