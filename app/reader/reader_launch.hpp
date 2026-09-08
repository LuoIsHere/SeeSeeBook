#pragma once

#include <cstdint>
#include <cstring>

#include "app_launch_context.hpp"
#include "book_file_format.hpp"
#include "storage_service.hpp"

struct reader_launch_parameters {
    static constexpr std::uint32_t launch_type = 0x52445231U;  // RDR1

    char file_path[STORAGE_MAX_PATH_LENGTH + 1U] = {};
    std::uint32_t media_generation = 0U;
    book_file_format format = book_file_format::unknown;
};

static_assert(std::is_trivially_copyable_v<reader_launch_parameters>);
static_assert(sizeof(reader_launch_parameters) <= APP_LAUNCH_PAYLOAD_CAPACITY);

inline bool reader_make_launch_context(app_launch_context& context, const char* path,
                                       std::uint32_t media_generation,
                                       book_file_format format)
{
    if (path == nullptr || path[0] != '/' || std::strlen(path) > STORAGE_MAX_PATH_LENGTH ||
        (format != book_file_format::txt && format != book_file_format::epub)) {
        context.clear();
        return false;
    }
    reader_launch_parameters parameters = {};
    std::strcpy(parameters.file_path, path);
    parameters.media_generation = media_generation;
    parameters.format = format;
    return context.set(parameters);
}

inline bool reader_read_launch_context(const app_launch_context& context,
                                       reader_launch_parameters& parameters)
{
    if (!context.get(parameters) || parameters.file_path[0] != '/' ||
        std::memchr(parameters.file_path, '\0', sizeof(parameters.file_path)) == nullptr ||
        (parameters.format != book_file_format::txt && parameters.format != book_file_format::epub)) {
        parameters = {};
        return false;
    }
    return true;
}
