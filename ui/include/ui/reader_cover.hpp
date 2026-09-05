#pragma once

#include <cstddef>
#include <cstdint>

#include "book_types.hpp"

struct reader_cover_lease {
    const std::uint8_t* data;
    std::size_t size;
    std::uint32_t generation;
    book_cover_encoding encoding;
    std::uint8_t slot;
};

bool ui_reader_cover_begin(std::size_t size, book_cover_encoding encoding);
bool ui_reader_cover_append(std::size_t offset, const std::uint8_t* data,
                            std::size_t length);
std::uint32_t ui_reader_cover_commit();
void ui_reader_cover_cancel();
void ui_reader_cover_clear();
bool ui_reader_cover_acquire(std::uint32_t generation, reader_cover_lease& lease);
void ui_reader_cover_release(reader_cover_lease& lease);
