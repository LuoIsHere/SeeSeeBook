#include "file_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include <esp_log.h>

#include "app.hpp"
#include "hal.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "app_file";

void copy_utf8_prefix(
    const std::string& source,
    char* destination,
    std::size_t destination_size)
{
    if (destination_size == 0U) {
        return;
    }
    std::size_t copy_length = std::min(source.size(), destination_size - 1U);
    while (copy_length > 0U && copy_length < source.size() &&
           (static_cast<unsigned char>(source[copy_length]) & 0xc0U) == 0x80U) {
        --copy_length;
    }
    std::memcpy(destination, source.data(), copy_length);
    destination[copy_length] = '\0';
}

void make_path_view(
    const std::string& source,
    char* destination,
    std::size_t destination_size)
{
    if (source.size() < destination_size) {
        copy_utf8_prefix(source, destination, destination_size);
        return;
    }
    constexpr char prefix[] = "/.../";
    const std::size_t suffix_capacity = destination_size - sizeof(prefix);
    std::size_t suffix_start = source.size() - suffix_capacity;
    while (suffix_start < source.size() &&
           (static_cast<unsigned char>(source[suffix_start]) & 0xc0U) == 0x80U) {
        ++suffix_start;
    }
    std::snprintf(destination, destination_size, "%s%s", prefix, source.c_str() + suffix_start);
}

bool point_in_page_button(
    std::int16_t x,
    std::int16_t y,
    bool next)
{
    return ui_point_in_rect(
        x,
        y,
        next ? file_next_page_rect() : file_previous_page_rect());
}

}  // namespace

file_app::file_app()
    : back_button_({file_back_button_rect()})
{
    // setAppInfo is part of Mooncake's external API.
    setAppInfo().name = "FileApp";
}

void file_app::handle_app_event(const app_event& event)
{
    switch (event.type) {
        case app_event_type::touch:
            handle_touch(event.touch);
            break;
        case app_event_type::storage_status:
            handle_storage_status(event.storage_status);
            break;
        case app_event_type::storage_result:
            handle_directory_result(event.storage_result);
            break;
        case app_event_type::rtc:
        case app_event_type::battery:
            break;
    }
}

void file_app::on_open()
{
    ++session_id_;
    current_path_ = "/";
    entries_.clear();
    active_control_ = active_control::none;
    page_index_ = 0U;
    popup_visible_ = false;
    back_button_.reset();

    switch (hal_get_storage_state()) {
        case sd_state::no_card:
            status_ = file_view_status::no_card;
            break;
        case sd_state::mounting:
            status_ = file_view_status::mounting;
            break;
        case sd_state::ready:
            status_ = file_view_status::loading;
            request_directory(current_path_);
            break;
        case sd_state::error:
            status_ = file_view_status::error;
            break;
    }
    submit_frame(refresh_mode::quality, display_update_region::full);
    ESP_LOGI(log_tag, "FileApp opened session=%lu", static_cast<unsigned long>(session_id_));
}

void file_app::on_running()
{
    if (popup_visible_ && hal_get_tick_ms() - popup_started_ms_ >= FILE_POPUP_DURATION_MS) {
        popup_visible_ = false;
        submit_frame(refresh_mode::fast, display_update_region::file_content);
    }
}

void file_app::on_close()
{
    ++session_id_;
    entries_.clear();
    current_path_ = "/";
    active_control_ = active_control::none;
    page_index_ = 0U;
    popup_visible_ = false;
    back_button_.reset();
    ESP_LOGI(log_tag, "FileApp closed");
}

void file_app::handle_touch(const touch_event& event)
{
    const app_back_button_result back_result = back_button_.handle_touch(event);
    if (back_result == app_back_button_result::clicked) {
        app_request_back();
        return;
    }
    if (back_result == app_back_button_result::handled) {
        return;
    }

    if (event.type == touch_event_type::press) {
        if (status_ == file_view_status::ready) {
            for (std::uint8_t row = 0U; row < FILE_ROW_COUNT; ++row) {
                if (!ui_point_in_rect(event.start_x, event.start_y, file_row_rect(row))) {
                    continue;
                }
                const std::size_t item_index =
                    static_cast<std::size_t>(page_index_) * FILE_ROW_COUNT + row;
                if (item_index >= entries_.size() + 1U) {
                    return;
                }
                if (item_index == 0U && current_path_ == "/") {
                    return;
                }
                active_control_ = active_control::row;
                active_index_ = row;
                submit_control_feedback(
                    display_control_type::file_row,
                    row,
                    file_row_rect(row),
                    true);
                return;
            }
        }
        if (page_index_ > 0U &&
            point_in_page_button(event.start_x, event.start_y, false)) {
            active_control_ = active_control::previous_page;
            submit_control_feedback(
                display_control_type::file_page,
                0U,
                file_previous_page_rect(),
                true);
        } else if (page_index_ + 1U < page_count() &&
                   point_in_page_button(event.start_x, event.start_y, true)) {
            active_control_ = active_control::next_page;
            submit_control_feedback(
                display_control_type::file_page,
                1U,
                file_next_page_rect(),
                true);
        }
        return;
    }

    if (active_control_ == active_control::none) {
        return;
    }
    if (event.type == touch_event_type::click) {
        const active_control captured_control = active_control_;
        const std::uint8_t captured_index = active_index_;
        active_control_ = active_control::none;
        if (captured_control == active_control::row) {
            const bool released_inside = ui_point_in_rect(
                event.end_x,
                event.end_y,
                file_row_rect(captured_index));
            submit_control_feedback(
                display_control_type::file_row,
                captured_index,
                file_row_rect(captured_index),
                false);
            if (released_inside) {
                activate_row(captured_index);
            }
            return;
        }

        const bool next = captured_control == active_control::next_page;
        const ui_rect rect = next ? file_next_page_rect() : file_previous_page_rect();
        const bool released_inside = ui_point_in_rect(event.end_x, event.end_y, rect);
        submit_control_feedback(
            display_control_type::file_page,
            next ? 1U : 0U,
            rect,
            false);
        if (released_inside) {
            if (next && page_index_ + 1U < page_count()) {
                ++page_index_;
            } else if (!next && page_index_ > 0U) {
                --page_index_;
            }
            submit_frame(refresh_mode::fast, display_update_region::file_content);
        }
        return;
    }

    if (event.type == touch_event_type::long_press_end) {
        if (active_control_ == active_control::row) {
            submit_control_feedback(
                display_control_type::file_row,
                active_index_,
                file_row_rect(active_index_),
                false);
        } else {
            const bool next = active_control_ == active_control::next_page;
            submit_control_feedback(
                display_control_type::file_page,
                next ? 1U : 0U,
                next ? file_next_page_rect() : file_previous_page_rect(),
                false);
        }
        active_control_ = active_control::none;
    }
}

void file_app::handle_storage_status(const sd_status_event& event)
{
    requested_generation_ = event.media_generation;
    active_control_ = active_control::none;
    popup_visible_ = false;
    entries_.clear();
    page_index_ = 0U;
    switch (event.state) {
        case sd_state::no_card:
            current_path_ = "/";
            status_ = file_view_status::no_card;
            break;
        case sd_state::mounting:
            current_path_ = "/";
            status_ = file_view_status::mounting;
            break;
        case sd_state::ready:
            current_path_ = "/";
            status_ = file_view_status::loading;
            request_directory(current_path_);
            break;
        case sd_state::error:
            current_path_ = "/";
            status_ = file_view_status::error;
            break;
    }
    submit_frame(refresh_mode::fast, display_update_region::file_content);
}

void file_app::handle_directory_result(const storage_event& event)
{
    if (event.type != storage_event_type::directory_result ||
        event.directory_result == nullptr) {
        return;
    }
    storage_directory_result& result = *event.directory_result;
    if (result.session_id != session_id_ || result.request_id != request_id_ ||
        result.media_generation != requested_generation_) {
        return;
    }

    switch (result.code) {
        case storage_result_code::ok:
            current_path_ = result.path;
            entries_ = std::move(result.entries);
            status_ = file_view_status::ready;
            break;
        case storage_result_code::too_many_entries:
            entries_.clear();
            status_ = file_view_status::directory_too_large;
            break;
        case storage_result_code::invalid_path:
            entries_.clear();
            status_ = file_view_status::path_too_long;
            break;
        case storage_result_code::no_card:
        case storage_result_code::cancelled:
            return;
        case storage_result_code::io_error:
        case storage_result_code::no_memory:
            entries_.clear();
            status_ = file_view_status::directory_error;
            break;
    }
    page_index_ = 0U;
    submit_frame(refresh_mode::fast, display_update_region::file_content);
}

void file_app::request_directory(const std::string& path)
{
    ++request_id_;
    requested_generation_ = hal_get_storage_media_generation();
    status_ = file_view_status::loading;
    entries_.clear();
    page_index_ = 0U;
    popup_visible_ = false;
    if (!storage_service_list_directory(path.c_str(), request_id_, session_id_)) {
        status_ = hal_get_storage_state() == sd_state::ready
                      ? file_view_status::directory_error
                      : file_view_status::no_card;
    }
}

void file_app::activate_row(std::uint8_t row_index)
{
    const std::size_t item_index =
        static_cast<std::size_t>(page_index_) * FILE_ROW_COUNT + row_index;
    if (item_index == 0U) {
        if (current_path_ == "/") {
            return;
        }
        const std::size_t separator = current_path_.find_last_of('/');
        const std::string parent = separator == 0U ? "/" : current_path_.substr(0U, separator);
        request_directory(parent);
        submit_frame(refresh_mode::fast, display_update_region::file_content);
        return;
    }

    const std::size_t entry_index = item_index - 1U;
    if (entry_index >= entries_.size()) {
        return;
    }
    const file_entry& entry = entries_[entry_index];
    if (entry.type == file_entry_type::file) {
        popup_visible_ = true;
        popup_started_ms_ = hal_get_tick_ms();
        submit_frame(refresh_mode::fast, display_update_region::file_content);
        return;
    }

    std::string child_path = current_path_;
    if (child_path != "/") {
        child_path += '/';
    }
    child_path += entry.name;
    if (child_path.size() > STORAGE_MAX_PATH_LENGTH) {
        status_ = file_view_status::path_too_long;
        submit_frame(refresh_mode::fast, display_update_region::file_content);
        return;
    }
    request_directory(child_path);
    submit_frame(refresh_mode::fast, display_update_region::file_content);
}

void file_app::submit_frame(
    refresh_mode mode,
    display_update_region update_region)
{
    display_request request = {};
    request.view = display_view::file;
    request.mode = mode;
    request.update_region = update_region;
    build_view(request.file);
    request.allow_quality_cleanup = true;
    if (!hal_submit_display_request(request)) {
        ESP_LOGW(log_tag, "display request queue unavailable");
    }
}

void file_app::submit_control_feedback(
    display_control_type type,
    std::uint8_t button_index,
    const ui_rect& rect,
    bool pressed)
{
    display_control_request request = {};
    request.type = type;
    request.mode = refresh_mode::fastest;
    request.update_region = display_update_region::control;
    request.button_index = button_index;
    request.rect = rect;
    request.pressed = pressed;
    request.allow_quality_cleanup = !pressed;
    if (!hal_submit_display_control_request(request)) {
        ESP_LOGW(log_tag, "control feedback queue unavailable");
    }
}

void file_app::build_view(file_view_state& view) const
{
    view = {};
    make_path_view(current_path_, view.path, sizeof(view.path));
    view.status = status_;
    view.page_index = page_index_;
    view.page_count = page_count();
    view.popup_visible = popup_visible_;
    if (status_ != file_view_status::ready) {
        return;
    }

    const std::size_t first_item = static_cast<std::size_t>(page_index_) * FILE_ROW_COUNT;
    const std::size_t total_items = entries_.size() + 1U;
    for (std::size_t item_index = first_item;
         item_index < total_items && view.row_count < FILE_VIEW_ROW_COUNT;
         ++item_index) {
        file_row_view_state& row = view.rows[view.row_count++];
        if (item_index == 0U) {
            std::snprintf(row.name, sizeof(row.name), "%s", FILE_PARENT_ENTRY_NAME);
            row.directory = true;
            row.parent = true;
            row.enabled = current_path_ != "/";
            continue;
        }
        const file_entry& entry = entries_[item_index - 1U];
        copy_utf8_prefix(entry.name, row.name, sizeof(row.name));
        row.directory = entry.type == file_entry_type::directory;
        row.enabled = true;
        row.name_truncated = entry.name.size() >= sizeof(row.name);
    }
}

std::uint16_t file_app::page_count() const
{
    const std::size_t total_items = entries_.size() + 1U;
    return static_cast<std::uint16_t>(
        std::max<std::size_t>(1U, (total_items + FILE_ROW_COUNT - 1U) / FILE_ROW_COUNT));
}
