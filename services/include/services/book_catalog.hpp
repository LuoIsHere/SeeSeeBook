#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <esp_err.h>

#include "book_types.hpp"

constexpr std::size_t BOOK_CATALOG_ITEM_LIMIT = 256U;
constexpr std::size_t BOOK_CATALOG_PAGE_CAPACITY = 6U;
constexpr std::size_t BOOK_CATALOG_PREVIEW_CAPACITY = 257U;
constexpr std::uint32_t BOOK_CATALOG_SCHEMA_VERSION = 1U;

struct book_catalog_settings {
    bool auto_scan_txt;
    bool auto_scan_epub;
};

constexpr bool book_catalog_settings_equal(
    const book_catalog_settings& left,
    const book_catalog_settings& right)
{
    return left.auto_scan_txt == right.auto_scan_txt &&
           left.auto_scan_epub == right.auto_scan_epub;
}

enum class book_catalog_preview_state : std::uint8_t {
    none,
    ready,
    invalid_utf8,
    unavailable,
};

enum class book_catalog_cover_state : std::uint8_t {
    none,
    pending,
    ready,
    unavailable,
};

enum class book_catalog_state : std::uint8_t {
    unavailable,
    loading_cache,
    ready,
    scanning,
    enriching,
    error,
};

struct book_catalog_item {
    char path[BOOK_PATH_CAPACITY];
    char book_id[65];
    char preview[BOOK_CATALOG_PREVIEW_CAPACITY];
    std::uint64_t source_size;
    std::int64_t modified_time;
    std::uint32_t cover_size;
    book_file_format format;
    book_catalog_preview_state preview_state;
    book_catalog_cover_state cover_state;
    book_cover_encoding cover_encoding;
};

struct book_catalog_snapshot {
    std::uint32_t media_generation;
    std::uint32_t revision;
    std::uint16_t item_count;
    book_catalog_settings settings;
    book_catalog_state state;
    esp_err_t error;
};

struct book_catalog_event {
    std::uint32_t media_generation;
    std::uint32_t revision;
};

static_assert(std::is_trivially_copyable_v<book_catalog_item>);
static_assert(std::is_trivially_copyable_v<book_catalog_snapshot>);
static_assert(std::is_trivially_copyable_v<book_catalog_event>);

esp_err_t book_catalog_service_init();
bool book_catalog_service_activate(std::uint32_t media_generation);
void book_catalog_service_pause();
bool book_catalog_service_idle();
bool book_catalog_service_update_settings(
    std::uint32_t media_generation,
    const book_catalog_settings& settings);
bool book_catalog_service_snapshot(book_catalog_snapshot& snapshot);
bool book_catalog_service_copy_page(
    std::uint32_t media_generation,
    std::size_t first_item,
    book_catalog_item* items,
    std::size_t capacity,
    std::size_t& copied);
bool book_catalog_service_try_get_event(book_catalog_event& event);

// The extracted cover is owned by the existing EPUB cache under this path.
bool book_catalog_cover_path(
    const book_catalog_item& item,
    char* output,
    std::size_t capacity);
