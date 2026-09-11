#include "books_file_name.hpp"

#include <algorithm>
#include <cctype>
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

bool ascii_suffix(std::string_view text, std::string_view suffix)
{
    if (text.size() <= suffix.size()) { return false; }
    const std::size_t offset = text.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        const auto left = static_cast<unsigned char>(text[offset + index]);
        const auto right = static_cast<unsigned char>(suffix[index]);
        if (std::tolower(left) != std::tolower(right)) { return false; }
    }
    return true;
}

std::string_view display_basename(std::string_view path)
{
    const auto slash = path.find_last_of('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1U);
    if (ascii_suffix(name, ".txt")) { name.remove_suffix(4U); }
    else if (ascii_suffix(name, ".epub")) { name.remove_suffix(5U); }
    return name;
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
    name = display_basename(name);
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

void format_books_preview(
    std::string_view text,
    books_item_view_state& output,
    const text_layout_profile& layout)
{
    output.preview_line_count = 0U;
    std::memset(output.preview, 0, sizeof(output.preview));
    if (layout.glyph_width == nullptr || layout.line_width == 0U ||
        layout.line_count == 0U || text.empty() || !valid_utf8(text)) {
        return;
    }
    const std::size_t line_limit = std::min<std::size_t>(
        layout.line_count, books_preview_line_count);
    std::size_t offset = 0U;
    for (std::size_t line = 0U; line < line_limit && offset < text.size(); ++line) {
        while (offset < text.size() && text[offset] == ' ') { ++offset; }
        if (offset >= text.size()) { break; }
        if (line + 1U == line_limit) {
            text_layout_profile final_layout = layout;
            final_layout.line_count = 1U;
            format_file_name(text.substr(offset), false, output.preview[line],
                             sizeof(output.preview[line]), final_layout);
            if (output.preview[line][0] != '\0') {
                output.preview_line_count = static_cast<std::uint8_t>(line + 1U);
            }
            break;
        }
        const std::size_t length = first_line_length(
            text.substr(offset), sizeof(output.preview[line]), layout);
        if (length == 0U) { break; }
        std::memcpy(output.preview[line], text.data() + offset, length);
        output.preview[line][length] = '\0';
        output.preview_line_count = static_cast<std::uint8_t>(line + 1U);
        offset += length;
    }
}
