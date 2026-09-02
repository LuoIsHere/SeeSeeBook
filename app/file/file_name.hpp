#pragma once

#include <cstddef>
#include <string_view>

#include "text_layout.hpp"

// Produces the complete display name, including any ellipsis, in App-owned data.
void format_file_name(
    std::string_view name, bool directory, char* output, std::size_t capacity,
    const text_layout_profile& layout);

bool file_name_is_txt(std::string_view name);
