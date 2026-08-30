#pragma once

#include <cstdint>

#define FILE_VIEW_PATH_LENGTH 96U
#define FILE_VIEW_NAME_LENGTH 64U
#define FILE_VIEW_ROW_COUNT 8U

enum class file_view_status : std::uint8_t {
    no_card,
    mounting,
    loading,
    ready,
    error,
    directory_error,
    directory_too_large,
    path_too_long,
};

struct file_row_view_state {
    char name[FILE_VIEW_NAME_LENGTH];
    bool directory;
    bool parent;
    bool enabled;
    bool name_truncated;
};

struct file_view_state {
    char path[FILE_VIEW_PATH_LENGTH];
    file_row_view_state rows[FILE_VIEW_ROW_COUNT];
    std::uint16_t page_index;
    std::uint16_t page_count;
    std::uint8_t row_count;
    file_view_status status;
    bool popup_visible;
};
