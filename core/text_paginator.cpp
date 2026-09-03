#include "text_paginator.hpp"

#include <algorithm>
#include <cstring>

#include "utf8.hpp"

void reader_paginator::reset(std::uint64_t start, const text_layout_profile& layout)
{
    page_ = {};
    page_.current_page_start_offset = start;
    page_.next_page_start_offset = start;
    page_.line_count = 1U;
    page_.empty = true;
    layout_ = layout;
    layout_.line_count = std::min<std::uint16_t>(
        layout.line_count, READER_PAGE_LINE_CAPACITY);
    input_offset_ = start;
    line_width_ = 0U;
    pending_size_ = 0U;
    full_ = false;
    swallow_lf_ = false;
    status_ = layout_.line_width == 0U || layout_.line_count == 0U ||
                      layout_.glyph_width == nullptr
                  ? reader_parse_status::invalid_utf8 : reader_parse_status::need_data;
}

bool reader_paginator::next_line()
{
    if (page_.line_count >= layout_.line_count) {
        full_ = true;
        return false;
    }
    page_.lines[page_.line_count++].offset = page_.text_length;
    line_width_ = 0U;
    return true;
}

bool reader_paginator::append(
    std::uint32_t codepoint, std::uint64_t start, std::uint64_t end)
{
    if (swallow_lf_) {
        swallow_lf_ = false;
        if (codepoint == '\n') {
            page_.next_page_start_offset = end;
            return true;
        }
    }
    if (full_) {
        page_.next_page_start_offset = start;
        return false;
    }
    if (start == 0U && codepoint == 0xfeffU) {
        page_.next_page_start_offset = end;
        return true;
    }
    page_.empty = false;
    if (codepoint == '\r' || codepoint == '\n') {
        next_line();
        swallow_lf_ = codepoint == '\r';
        page_.next_page_start_offset = end;
        return true;
    }
    // Basic TXT: one space per tab, and a visible replacement for controls.
    if (codepoint == '\t') {
        codepoint = ' ';
    } else if (codepoint < 0x20U || codepoint == 0x7fU) {
        codepoint = '?';
    }
    const std::uint16_t width = layout_.glyph_width(codepoint);
    if (static_cast<std::uint32_t>(line_width_) + width > layout_.line_width &&
        page_.lines[page_.line_count - 1U].length != 0U && !next_line()) {
        page_.next_page_start_offset = start;
        return false;
    }
    char encoded[5] = {};
    const std::size_t length = utf8_encode(codepoint, encoded);
    if (page_.text_length + length >= sizeof(page_.text)) {
        page_.next_page_start_offset = start;
        return false;
    }
    std::memcpy(page_.text + page_.text_length, encoded, length);
    page_.text_length += length;
    page_.text[page_.text_length] = '\0';
    page_.lines[page_.line_count - 1U].length += length;
    line_width_ += width;
    page_.next_page_start_offset = end;
    return true;
}

reader_parse_status reader_paginator::feed(
    const char* data, std::size_t size, bool end_of_file)
{
    if (status_ != reader_parse_status::need_data) {
        return status_;
    }
    for (std::size_t index = 0U; index < size; ++index) {
        pending_[pending_size_++] = data[index];
        ++input_offset_;
        std::uint32_t codepoint = 0U;
        std::size_t length = 0U;
        const auto decoded = utf8_decode(pending_, pending_size_, codepoint, length);
        if (decoded == utf8_decode_result::invalid) {
            status_ = reader_parse_status::invalid_utf8;
            return status_;
        }
        if (decoded == utf8_decode_result::incomplete) {
            continue;
        }
        const std::uint64_t start = input_offset_ - pending_size_;
        pending_size_ = 0U;
        if (!append(codepoint, start, input_offset_)) {
            status_ = reader_parse_status::page_ready;
            return status_;
        }
    }
    if (end_of_file) {
        if (pending_size_ != 0U) {
            status_ = reader_parse_status::invalid_utf8;
        } else {
            page_.end_of_file = true;
            page_.next_page_start_offset = input_offset_;
            status_ = reader_parse_status::page_ready;
        }
    }
    return status_;
}

void reader_page_history::push(std::uint64_t offset)
{
    if (size_ != 0U && offsets_[size_ - 1U] == offset) {
        return;
    }
    if (size_ == READER_HISTORY_CAPACITY) {
        std::move(offsets_ + 1U, offsets_ + size_, offsets_);
        --size_;
    }
    offsets_[size_++] = offset;
}

bool reader_page_history::previous(std::uint64_t& offset) const
{
    if (size_ == 0U) {
        return false;
    }
    offset = offsets_[size_ - 1U];
    return true;
}
