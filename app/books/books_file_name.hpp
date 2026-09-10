#pragma once

#include <string_view>

#include "books_view.hpp"
#include "text_layout.hpp"

// Produces up to two UTF-8-aligned display lines in App-owned view data.
void format_books_file_name(
    std::string_view name,
    books_file_name_view_state& output,
    const text_layout_profile& layout);
