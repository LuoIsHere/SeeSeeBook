#pragma once

#include <cstddef>
#include <cstdint>

#include "book_types.hpp"

struct books_cover_lease {
    const std::uint8_t* data;
    std::size_t size;
    std::uint32_t generation;
    book_cover_encoding encoding;
    std::uint8_t slot;
};

bool ui_books_cover_begin(std::uint8_t slot, std::size_t size, book_cover_encoding encoding);
bool ui_books_cover_append(std::uint8_t slot, std::size_t offset,
                           const std::uint8_t* data, std::size_t length);
std::uint32_t ui_books_cover_commit(std::uint8_t slot);
void ui_books_cover_cancel(std::uint8_t slot);
void ui_books_cover_clear();
bool ui_books_cover_acquire(std::uint8_t slot, std::uint32_t generation,
                            books_cover_lease& lease);
void ui_books_cover_release(books_cover_lease& lease);
