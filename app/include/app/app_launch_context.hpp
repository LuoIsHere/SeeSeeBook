#pragma once

#include <cstdint>

#include "book_file_format.hpp"
#include "storage_service.hpp"

struct app_launch_context {
    char file_path[STORAGE_MAX_PATH_LENGTH + 1U];
    std::uint32_t media_generation;
    book_file_format format;
};
