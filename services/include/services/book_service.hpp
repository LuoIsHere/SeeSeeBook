#pragma once

#include <esp_err.h>

#include "book_types.hpp"
#include "result_handle.hpp"
#include "text_layout.hpp"

constexpr std::size_t BOOK_CONTENT_CHUNK_CAPACITY = 2048U;

enum class book_content_kind : std::uint8_t { text, cover };

struct book_content_result {
    std::uint32_t request_id;
    std::uint32_t session_id;
    std::uint32_t media_generation;
    esp_err_t error;
    book_content_kind kind;
    book_cover_encoding cover_encoding;
    std::uint64_t offset;
    std::uint64_t file_size;
    std::uint16_t length;
    bool end_of_file;
    std::uint8_t data[BOOK_CONTENT_CHUNK_CAPACITY];
};

static_assert(std::is_trivially_copyable_v<book_content_result>);

esp_err_t book_service_init();
bool book_service_open(const char* path, const text_layout_profile& layout,
                       std::uint32_t session, std::uint32_t media_generation,
                       book_file_format format);
bool book_service_query(std::uint32_t session, std::uint32_t media_generation,
                        std::uint32_t request, bool by_page, std::uint32_t page, std::uint64_t offset);
bool book_service_close(std::uint32_t session, std::uint32_t media_generation, std::uint64_t offset);
bool book_service_read(std::uint32_t session, std::uint32_t media_generation,
                       std::uint32_t request, book_content_kind kind,
                       std::uint64_t offset);
bool book_service_resolve_result(const result_handle& handle,
                                 const book_content_result*& result);
bool book_service_release_result(result_handle& handle);
