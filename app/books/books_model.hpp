#pragma once

#include <cstddef>

#include "books_view.hpp"

struct books_scan_settings {
    bool auto_scan_txt;
    bool auto_scan_epub;
};

constexpr bool books_scan_settings_equal(
    const books_scan_settings& left,
    const books_scan_settings& right)
{
    return left.auto_scan_txt == right.auto_scan_txt &&
           left.auto_scan_epub == right.auto_scan_epub;
}

constexpr std::size_t books_page_count(std::size_t item_count)
{
    return item_count == 0U
               ? 1U
               : (item_count + books_view_item_capacity - 1U) /
                     books_view_item_capacity;
}

constexpr std::size_t books_page_first_item(std::size_t page_index)
{
    return page_index * books_view_item_capacity;
}

constexpr std::size_t books_page_item_count(
    std::size_t item_count,
    std::size_t page_index)
{
    const std::size_t first = books_page_first_item(page_index);
    if (first >= item_count) {
        return 0U;
    }
    const std::size_t remaining = item_count - first;
    return remaining < books_view_item_capacity
               ? remaining
               : books_view_item_capacity;
}

class books_settings_model {
public:
    const books_scan_settings& saved() const { return saved_; }
    const books_scan_settings& pending() const { return pending_; }
    bool visible() const { return visible_; }

    // Persistence adapters may restore the committed value while the modal is closed.
    bool load_saved(const books_scan_settings& value);
    void open();
    void toggle_txt();
    void toggle_epub();
    void confirm();
    void cancel();

private:
    books_scan_settings saved_ = {true, false};
    books_scan_settings pending_ = saved_;
    bool visible_ = false;
};
