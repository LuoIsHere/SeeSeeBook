#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "reader_page.hpp"

enum class reader_view_status : std::uint8_t {
    loading, ready, empty_file, invalid_utf8, invalid_epub, unsupported_epub,
    file_not_found, storage_error, no_card,
};

struct reader_view_state {
    reader_page page;
    std::uint64_t file_size;
    reader_view_status status;
    bool previous_enabled;
    bool next_enabled;
    bool progress_persistent;
    bool menu_visible;
    bool showing_cover;
    std::uint32_t cover_generation;
};

static_assert(std::is_trivially_copyable_v<reader_view_state>);
static_assert(sizeof(reader_view_state) <= 2240U);

// Menu-only refreshes are safe only when the previously committed body matches.
inline bool reader_body_matches(const reader_view_state& left, const reader_view_state& right)
{
    return left.status == right.status && left.file_size == right.file_size &&
           left.progress_persistent == right.progress_persistent &&
           left.showing_cover == right.showing_cover &&
           left.cover_generation == right.cover_generation &&
           std::memcmp(&left.page, &right.page, sizeof(reader_page)) == 0;
}
