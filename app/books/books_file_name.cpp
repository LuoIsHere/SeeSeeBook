#include "books_file_name.hpp"

#include <algorithm>
#include <cstring>

#include "file_name.hpp"
#include "utf8.hpp"

namespace {

bool valid_utf8(std::string_view text)
{
    for (std::size_t offset = 0U; offset < text.size();) {
        std::uint32_t codepoint = 0U;
        std::size_t length = 0U;
        if (utf8_decode(text.data() + offset, text.size() - offset,
                        codepoint, length) != utf8_decode_result::complete) {
            return false;
        }
        offset += length;
    }
    return true;
}

std::size_t first_line_length(
    std::string_view text,
    std::size_t capacity,
    const text_layout_profile& layout)
{
    std::size_t offset = 0U;
    std::uint32_t width = 0U;
    while (offset < text.size()) {
        std::uint32_t codepoint = 0U;
        std::size_t length = 0U;
        if (utf8_decode(text.data() + offset, text.size() - offset,
                        codepoint, length) != utf8_decode_result::complete ||
            offset + length >= capacity) {
            break;
        }
        const std::uint32_t remaining_width =
            width < layout.line_width ? layout.line_width - width : 0U;
        const std::uint32_t advance = layout.glyph_width(codepoint);
        if (advance > remaining_width) {
            break;
        }
        width += advance;
        offset += length;
    }
    return offset;
}

}  // namespace

void format_books_file_name(
    std::string_view name,
    books_file_name_view_state& output,
    const text_layout_profile& layout)
{
    output = {};
    if (layout.glyph_width == nullptr || layout.line_count == 0U || name.empty()) {
        return;
    }
    constexpr std::string_view invalid = "<invalid UTF-8>";
    if (!valid_utf8(name)) {
        name = invalid;
    }
    const std::size_t first = first_line_length(
        name, sizeof(output.lines[0]), layout);
    if (first == 0U) {
        return;
    }
    std::memcpy(output.lines[0], name.data(), first);
    output.lines[0][first] = '\0';
    output.line_count = 1U;
    if (first >= name.size() || layout.line_count < 2U) {
        return;
    }
    text_layout_profile second_layout = layout;
    second_layout.line_count = 1U;
    format_file_name(
        name.substr(first), false, output.lines[1],
        sizeof(output.lines[1]), second_layout);
    if (output.lines[1][0] != '\0') {
        output.line_count = 2U;
    }
}
