#include "file_name.hpp"

#include <cstring>
#include <limits>

#include "utf8.hpp"

namespace {

std::uint32_t measure(std::string_view text, text_glyph_width width)
{
    std::uint32_t total = 0U;
    for (std::size_t offset = 0U; offset < text.size();) {
        std::uint32_t codepoint = 0U;
        std::size_t length = 0U;
        if (utf8_decode(text.data() + offset, text.size() - offset, codepoint, length) !=
            utf8_decode_result::complete) {
            return UINT32_MAX;
        }
        total += width(codepoint);
        offset += length;
    }
    return total;
}

std::size_t prefix_length(
    std::string_view name, std::size_t bytes, std::uint32_t width,
    text_glyph_width glyph_width)
{
    std::size_t offset = 0U;
    while (offset < name.size()) {
        std::uint32_t codepoint = 0U;
        std::size_t length = 0U;
        if (utf8_decode(name.data() + offset, name.size() - offset, codepoint, length) !=
            utf8_decode_result::complete || length > bytes) {
            break;
        }
        const std::uint32_t advance = glyph_width(codepoint);
        if (advance > width) {
            break;
        }
        bytes -= length;
        width -= advance;
        offset += length;
    }
    return offset;
}

void copy(std::string_view name, char* output)
{
    if (!name.empty()) {
        std::memcpy(output, name.data(), name.size());
    }
    output[name.size()] = '\0';
}

}  // namespace

void format_file_name(
    std::string_view name, bool directory, char* output, std::size_t capacity,
    const text_layout_profile& layout)
{
    if (capacity == 0U || output == nullptr) {
        return;
    }
    output[0] = '\0';
    if (layout.glyph_width == nullptr) {
        return;
    }
    const std::size_t bytes = capacity - 1U;
    const std::uint32_t width = layout.line_width;
    const auto glyph_width = layout.glyph_width;
    if (measure(name, glyph_width) == UINT32_MAX) {
        name = "<invalid UTF-8>";
    }
    if (name.size() <= bytes && measure(name, glyph_width) <= width) {
        copy(name, output);
        return;
    }
    constexpr std::string_view marker = "...";
    const std::uint32_t marker_width = measure(marker, glyph_width);
    const std::size_t dot = name.find_last_of('.');
    const bool has_extension = !directory && dot != std::string_view::npos &&
                               dot != 0U && dot + 1U < name.size();
    if (has_extension) {
        const std::string_view extension = name.substr(dot);
        const std::uint32_t extension_width = measure(extension, glyph_width);
        if (extension.size() <= bytes && extension_width <= width) {
            if (bytes - extension.size() >= marker.size() &&
                width - extension_width >= marker_width) {
                const std::size_t prefix = prefix_length(
                    name.substr(0U, dot), bytes - extension.size() - marker.size(),
                    width - extension_width - marker_width, glyph_width);
                copy(name.substr(0U, prefix), output);
                copy(marker, output + prefix);
                copy(extension, output + prefix + marker.size());
            } else {
                copy(extension, output);
            }
            return;
        }
        // An oversized extension keeps a UTF-8 aligned tail, with a marker
        // when possible. No unsigned subtraction occurs before a size check.
        const bool mark = bytes >= marker.size() && width >= marker_width;
        const std::size_t tail_bytes = bytes - (mark ? marker.size() : 0U);
        const std::uint32_t tail_width = width - (mark ? marker_width : 0U);
        std::size_t start = 0U;
        while (start < extension.size() &&
               (extension.size() - start > tail_bytes ||
                measure(extension.substr(start), glyph_width) > tail_width)) {
            std::uint32_t codepoint = 0U;
            std::size_t length = 0U;
            utf8_decode(extension.data() + start, extension.size() - start,
                        codepoint, length);
            start += length;
        }
        if (mark) {
            copy(marker, output);
        }
        copy(extension.substr(start), output + (mark ? marker.size() : 0U));
        return;
    }
    if (bytes < marker.size() || width < marker_width) {
        copy(marker.substr(0U, prefix_length(marker, bytes, width, glyph_width)), output);
        return;
    }
    const std::size_t prefix = prefix_length(
        name, bytes - marker.size(), width - marker_width, glyph_width);
    copy(name.substr(0U, prefix), output);
    copy(marker, output + prefix);
}

bool file_name_is_txt(std::string_view name)
{
    return book_file_format_from_name(name) == book_file_format::txt;
}

book_file_format file_name_book_format(std::string_view name)
{
    return book_file_format_from_name(name);
}
