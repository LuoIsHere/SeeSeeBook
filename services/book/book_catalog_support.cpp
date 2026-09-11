#include "book_catalog_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "book_format.hpp"
#include "utf8.hpp"

namespace {

bool ascii_equal(char left, char right)
{
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
}

bool extension_is(const char* path, const char* extension)
{
    const std::size_t path_length = std::strlen(path);
    const std::size_t extension_length = std::strlen(extension);
    if (path_length <= extension_length) { return false; }
    const char* value = path + path_length - extension_length;
    for (std::size_t index = 0U; index < extension_length; ++index) {
        if (!ascii_equal(value[index], extension[index])) { return false; }
    }
    const char* slash = std::strrchr(path, '/');
    return slash == nullptr || value > slash + 1;
}

int folded_path_compare(const char* left, const char* right)
{
    while (*left != '\0' && *right != '\0') {
        const auto l = std::tolower(static_cast<unsigned char>(*left));
        const auto r = std::tolower(static_cast<unsigned char>(*right));
        if (l != r) { return l < r ? -1 : 1; }
        ++left;
        ++right;
    }
    return *left == *right ? 0 : (*left == '\0' ? -1 : 1);
}

int path_compare(const char* left, const char* right)
{
    const int folded = folded_path_compare(left, right);
    return folded != 0 ? folded : std::strcmp(left, right);
}

bool append_byte(char byte, char* output, std::size_t capacity, std::size_t& used)
{
    if (used + 1U >= capacity) { return false; }
    output[used++] = byte;
    output[used] = '\0';
    return true;
}

}  // namespace

bool book_catalog_path_excluded(const char* logical_path)
{
    if (logical_path == nullptr || logical_path[0] != '/') { return true; }
    for (const char* part = logical_path + 1; *part != '\0';) {
        const char* end = std::strchr(part, '/');
        const std::size_t length = end == nullptr ? std::strlen(part)
                                                  : static_cast<std::size_t>(end - part);
        if (length != 0U && part[0] == '.') { return true; }
        if (end == nullptr) { break; }
        part = end + 1;
    }
    return false;
}

book_file_format book_catalog_path_format(const char* logical_path)
{
    if (logical_path == nullptr || book_catalog_path_excluded(logical_path)) {
        return book_file_format::unknown;
    }
    if (extension_is(logical_path, ".txt")) { return book_file_format::txt; }
    if (extension_is(logical_path, ".epub")) { return book_file_format::epub; }
    return book_file_format::unknown;
}

bool book_catalog_path_selected(
    const char* logical_path,
    const book_catalog_settings& settings,
    book_file_format& format)
{
    format = book_catalog_path_format(logical_path);
    return (format == book_file_format::txt && settings.auto_scan_txt) ||
           (format == book_file_format::epub && settings.auto_scan_epub);
}

bool book_catalog_join_path(
    const char* directory,
    const char* name,
    char* output,
    std::size_t capacity)
{
    if (directory == nullptr || name == nullptr || output == nullptr ||
        directory[0] != '/' || name[0] == '\0' || std::strchr(name, '/') != nullptr ||
        std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
        return false;
    }
    const int written = std::strcmp(directory, "/") == 0
                            ? std::snprintf(output, capacity, "/%s", name)
                            : std::snprintf(output, capacity, "%s/%s", directory, name);
    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) { return false; }
    char canonical[BOOK_PATH_CAPACITY] = {};
    if (!book_canonical_path(output, canonical, sizeof(canonical))) { return false; }
    std::strcpy(output, canonical);
    return true;
}

bool book_catalog_make_txt_preview(
    const std::uint8_t* bytes,
    std::size_t length,
    bool end_of_file,
    char* output,
    std::size_t capacity,
    book_catalog_preview_state& state)
{
    state = book_catalog_preview_state::unavailable;
    if ((bytes == nullptr && length != 0U) || output == nullptr || capacity < 2U ||
        length > BOOK_CATALOG_TXT_READ_LIMIT) {
        return false;
    }
    output[0] = '\0';
    std::size_t input = length >= 3U && bytes[0] == 0xefU && bytes[1] == 0xbbU && bytes[2] == 0xbfU
                            ? 3U
                            : 0U;
    std::size_t used = 0U;
    bool pending_space = false;
    while (input < length) {
        std::uint32_t codepoint = 0U;
        std::size_t size = 0U;
        const auto decoded = utf8_decode(
            reinterpret_cast<const char*>(bytes + input), length - input,
            codepoint, size);
        if (decoded == utf8_decode_result::incomplete && !end_of_file) { break; }
        if (decoded != utf8_decode_result::complete) {
            std::strcpy(output, "Invalid UTF-8");
            state = book_catalog_preview_state::invalid_utf8;
            return true;
        }
        input += size;
        if (codepoint == '\r') {
            if (input < length && bytes[input] == '\n') { ++input; }
            pending_space = used != 0U;
            continue;
        }
        if (codepoint == '\n' || codepoint == '\t' || codepoint == ' ') {
            pending_space = used != 0U;
            continue;
        }
        if (codepoint < 0x20U || codepoint == 0x7fU) { continue; }
        if (pending_space && !append_byte(' ', output, capacity, used)) { break; }
        pending_space = false;
        if (size >= capacity - used) { break; }
        std::memcpy(output + used, bytes + input - size, size);
        used += size;
        output[used] = '\0';
    }
    state = book_catalog_preview_state::ready;
    return true;
}

void book_catalog_sort_unique(book_catalog_item* items, std::size_t& count)
{
    if (items == nullptr) { count = 0U; return; }
    std::sort(items, items + count, [](const auto& left, const auto& right) {
        return path_compare(left.path, right.path) < 0;
    });
    std::size_t output = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        if (output != 0U &&
            folded_path_compare(items[output - 1U].path, items[index].path) == 0) {
            continue;
        }
        if (output != index) { items[output] = items[index]; }
        ++output;
    }
    count = output;
}
