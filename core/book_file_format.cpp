#include "book_file_format.hpp"

namespace {

bool ascii_equal_folded(char left, char right)
{
    if (left >= 'A' && left <= 'Z') { left = static_cast<char>(left - 'A' + 'a'); }
    if (right >= 'A' && right <= 'Z') { right = static_cast<char>(right - 'A' + 'a'); }
    return left == right;
}

bool suffix(std::string_view name, std::string_view extension)
{
    if (name.size() <= extension.size()) { return false; }
    const auto start = name.size() - extension.size();
    for (std::size_t index = 0U; index < extension.size(); ++index) {
        if (!ascii_equal_folded(name[start + index], extension[index])) { return false; }
    }
    return true;
}

}  // namespace

book_file_format book_file_format_from_name(std::string_view name)
{
    if (suffix(name, ".txt")) { return book_file_format::txt; }
    if (suffix(name, ".epub")) { return book_file_format::epub; }
    return book_file_format::unknown;
}

