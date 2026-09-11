#pragma once

#include <cstddef>
#include <cstdint>

#include "book_catalog.hpp"

constexpr std::size_t BOOK_CATALOG_TXT_READ_LIMIT = 2048U;

bool book_catalog_path_excluded(const char* logical_path);
book_file_format book_catalog_path_format(const char* logical_path);
bool book_catalog_path_selected(
    const char* logical_path,
    const book_catalog_settings& settings,
    book_file_format& format);
bool book_catalog_join_path(
    const char* directory,
    const char* name,
    char* output,
    std::size_t capacity);
bool book_catalog_make_txt_preview(
    const std::uint8_t* bytes,
    std::size_t length,
    bool end_of_file,
    char* output,
    std::size_t capacity,
    book_catalog_preview_state& state);
void book_catalog_sort_unique(book_catalog_item* items, std::size_t& count);
