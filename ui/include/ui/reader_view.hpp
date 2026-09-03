#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "reader_page.hpp"

enum class reader_view_status : std::uint8_t {
    loading, ready, empty_file, invalid_utf8, file_not_found, storage_error, no_card,
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
