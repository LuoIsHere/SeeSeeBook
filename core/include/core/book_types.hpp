#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "book_file_format.hpp"

constexpr std::uint32_t BOOK_METADATA_SCHEMA_VERSION = 1U;
constexpr std::uint32_t BOOK_PAGE_INDEX_FORMAT_VERSION = 1U;
// Increment whenever font metrics, layout or the shared paginator changes.
constexpr std::uint32_t BOOK_PAGINATION_VERSION = 2U;
constexpr std::size_t BOOK_PATH_CAPACITY = 513U;
constexpr std::size_t BOOK_SCAN_BUFFER_SIZE = 4096U;

struct book_fingerprint {
    std::uint32_t head;
    std::uint32_t middle;
    std::uint32_t tail;
};

struct book_file_identity {
    std::uint64_t file_size;
    std::int64_t modified_time;
    book_fingerprint fingerprint;
};

struct book_progress {
    std::uint32_t page;
    std::uint64_t byte_offset;
};

enum class book_event_type : std::uint8_t { opened, ready, position, saved, error };
enum class book_cover_encoding : std::uint8_t { none, jpeg, png };

// Small value event: no index/page pointers cross tasks. Offsets remain on SD.
struct book_service_event {
    book_event_type type;
    std::uint32_t session_id;
    std::uint32_t request_id;
    std::uint32_t media_generation;
    book_progress progress;
    std::uint32_t page_count;
    std::uint64_t file_size;
    std::int64_t modified_time;
    std::int32_t error;
    book_file_format format;
    bool index_valid;
    bool persistent;
    bool content_ready;
    bool cover_available;
};

static_assert(std::is_trivially_copyable_v<book_service_event>);
static_assert(sizeof(book_service_event) <= 80U);
