#pragma once

#include <esp_err.h>

#include "book_types.hpp"
#include "text_layout.hpp"

esp_err_t book_service_init();
bool book_service_open(const char* path, const text_layout_profile& layout,
                       std::uint32_t session, std::uint32_t media_generation);
bool book_service_query(std::uint32_t session, std::uint32_t media_generation,
                        std::uint32_t request, bool by_page, std::uint32_t page, std::uint64_t offset);
bool book_service_close(std::uint32_t session, std::uint32_t media_generation, std::uint64_t offset);
