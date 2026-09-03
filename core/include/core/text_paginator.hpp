#pragma once

#include <cstddef>
#include <cstdint>

#include "reader_page.hpp"
#include "text_layout.hpp"

enum class reader_parse_status : std::uint8_t { need_data, page_ready, invalid_utf8 };

class reader_paginator {
public:
    void reset(std::uint64_t start, const text_layout_profile& layout);
    reader_parse_status feed(const char* data, std::size_t size, bool end_of_file);
    const reader_page& page() const { return page_; }
    std::uint64_t read_offset() const { return input_offset_; }

private:
    reader_page page_ = {};
    text_layout_profile layout_ = {};
    std::uint64_t input_offset_ = 0U;
    std::uint16_t line_width_ = 0U;
    char pending_[4] = {};
    std::uint8_t pending_size_ = 0U;
    bool full_ = false;
    bool swallow_lf_ = false;
    reader_parse_status status_ = reader_parse_status::need_data;

    bool append(std::uint32_t codepoint, std::uint64_t start, std::uint64_t end);
    bool next_line();
};

constexpr std::size_t READER_HISTORY_CAPACITY = 64U;

class reader_page_history {
public:
    void clear() { size_ = 0U; }
    void push(std::uint64_t offset);
    bool previous(std::uint64_t& offset) const;
    void pop() { if (size_ != 0U) { --size_; } }
    std::size_t size() const { return size_; }

private:
    std::uint64_t offsets_[READER_HISTORY_CAPACITY] = {};
    std::size_t size_ = 0U;
};
