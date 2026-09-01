#include "file_app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <esp_log.h>

#include "app.hpp"
#include "system_tick_service.hpp"

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
    std::size_t length = std::min(source.size(), destination_size - 1U);
    while (length > 0U && length < source.size() &&
           (static_cast<unsigned char>(source[length]) & 0xc0U) == 0x80U) {
        --length;
    }
    std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
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

file_view_status view_status(storage_state state)
{
    switch (state) {
        case storage_state::no_card:
            return file_view_status::no_card;
        case storage_state::mounting:
            return file_view_status::mounting;
        case storage_state::ready:
            return file_view_status::loading;
        case storage_state::error:
            return file_view_status::error;
    }
    return file_view_status::error;
}

}  // namespace

void file_app::handle_app_event(const app_event& event)
{
    switch (event.type) {
        case app_event_type::ui_action:
            handle_action(event.action);
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
    release_directory_result();
    current_path_ = "/";
    page_index_ = 0U;
    popup_visible_ = false;
    status_ = view_status(storage_service_get_state());
    if (storage_service_get_state() == storage_state::ready) {
        request_directory(current_path_);
    }
    submit_frame(ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "FileApp opened session=%lu", static_cast<unsigned long>(session_id_));
}

void file_app::on_running()
{
    if (popup_visible_ &&
        system_tick_now_ms() - popup_started_ms_ >= FILE_POPUP_DURATION_MS) {
        popup_visible_ = false;
        submit_frame(ui_update_reason::popup_changed);
    }
}

void file_app::on_close()
{
    ++session_id_;
    release_directory_result();
    current_path_ = "/";
    page_index_ = 0U;
    popup_visible_ = false;
    status_ = file_view_status::no_card;
    ESP_LOGI(log_tag, "FileApp session cleared");
}

void file_app::handle_action(const ui_action_event& action)
{
    if (action.input.gesture != input_gesture_type::click) {
        return;
    }
    if (action.control == ui_control_type::navigate_back) {
        app_request_back();
    } else if (action.control == ui_control_type::file_row) {
        activate_row(action.index);
    } else if (action.control == ui_control_type::file_previous_page && page_index_ > 0U) {
        --page_index_;
        submit_frame(ui_update_reason::content_changed);
    } else if (action.control == ui_control_type::file_next_page &&
               page_index_ + 1U < page_count()) {
        ++page_index_;
        submit_frame(ui_update_reason::content_changed);
    }
}

void file_app::handle_storage_status(const app_storage_status_event& event)
{
    requested_generation_ = event.media_generation;
    release_directory_result();
    current_path_ = "/";
    page_index_ = 0U;
    popup_visible_ = false;
    status_ = view_status(event.state);
    if (event.state == storage_state::ready) {
        request_directory(current_path_);
    }
    submit_frame(ui_update_reason::content_changed);
}

void file_app::handle_directory_result(const app_storage_result_event& event)
{
    const storage_directory_result* result = nullptr;
    if (!storage_service_resolve_result(event.handle, result) || result == nullptr ||
        result->session_id != session_id_ || result->request_id != request_id_ ||
        result->media_generation != requested_generation_) {
        return;
    }
    if (result->code == storage_result_code::ok) {
        if (!storage_service_retain_result(event.handle)) {
            status_ = file_view_status::directory_error;
        } else {
            release_directory_result();
            directory_handle_ = event.handle;
            current_path_ = result->path;
            status_ = file_view_status::ready;
        }
    } else {
        release_directory_result();
        switch (result->code) {
            case storage_result_code::too_many_entries:
                status_ = file_view_status::directory_too_large;
                break;
            case storage_result_code::invalid_path:
                status_ = file_view_status::path_too_long;
                break;
            case storage_result_code::no_card:
            case storage_result_code::cancelled:
                return;
            case storage_result_code::io_error:
            case storage_result_code::no_memory:
                status_ = file_view_status::directory_error;
                break;
            case storage_result_code::ok:
                break;
        }
    }
    page_index_ = 0U;
    submit_frame(ui_update_reason::content_changed);
}

void file_app::request_directory(const std::string& path)
{
    ++request_id_;
    requested_generation_ = storage_service_get_media_generation();
    release_directory_result();
    status_ = file_view_status::loading;
    page_index_ = 0U;
    popup_visible_ = false;
    if (!storage_service_list_directory(path.c_str(), request_id_, session_id_)) {
        status_ = storage_service_get_state() == storage_state::ready
                      ? file_view_status::directory_error
                      : file_view_status::no_card;
    }
}

void file_app::activate_row(std::uint8_t row_index)
{
    const storage_directory_result* result = nullptr;
    if (status_ != file_view_status::ready ||
        !storage_service_resolve_result(directory_handle_, result) || result == nullptr) {
        status_ = file_view_status::directory_error;
        submit_frame(ui_update_reason::content_changed);
        return;
    }
    const std::size_t item_index =
        static_cast<std::size_t>(page_index_) * FILE_VIEW_ROW_COUNT + row_index;
    if (item_index == 0U) {
        if (current_path_ == "/") {
            return;
        }
        const std::size_t separator = current_path_.find_last_of('/');
        const std::string parent = separator == 0U ? "/" : current_path_.substr(0U, separator);
        request_directory(parent);
        submit_frame(ui_update_reason::content_changed);
        return;
    }
    const std::size_t entry_index = item_index - 1U;
    if (entry_index >= result->entries.size()) {
        return;
    }
    const file_entry& entry = result->entries[entry_index];
    if (entry.type == file_entry_type::file) {
        popup_visible_ = true;
        popup_started_ms_ = system_tick_now_ms();
        submit_frame(ui_update_reason::popup_changed);
        return;
    }
    std::string child = current_path_;
    if (child != "/") {
        child += '/';
    }
    child += entry.name;
    if (child.size() > STORAGE_MAX_PATH_LENGTH) {
        status_ = file_view_status::path_too_long;
    } else {
        request_directory(child);
    }
    submit_frame(ui_update_reason::content_changed);
}

void file_app::release_directory_result()
{
    storage_service_release_result(directory_handle_);
}

void file_app::submit_frame(ui_update_reason reason)
{
    if (!ui_write_file_frame(reason, write_frame, this)) {
        ESP_LOGW(log_tag, "renderer queue unavailable");
    }
}

bool file_app::write_frame(
    file_view_state& view,
    const void* context)
{
    const auto* instance = static_cast<const file_app*>(context);
    if (instance == nullptr) {
        return false;
    }
    instance->build_view(view);
    return true;
}

void file_app::build_view(file_view_state& view) const
{
    view = {};
    make_path_view(current_path_, view.path, sizeof(view.path));
    view.status = status_;
    view.page_index = page_index_;
    view.page_count = page_count();
    view.popup_visible = popup_visible_;
    const storage_directory_result* result = nullptr;
    if (status_ != file_view_status::ready ||
        !storage_service_resolve_result(directory_handle_, result) || result == nullptr) {
        return;
    }
    const std::size_t first = static_cast<std::size_t>(page_index_) * FILE_VIEW_ROW_COUNT;
    const std::size_t total = result->entries.size() + 1U;
    for (std::size_t item = first;
         item < total && view.row_count < FILE_VIEW_ROW_COUNT;
         ++item) {
        file_row_view_state& row = view.rows[view.row_count++];
        if (item == 0U) {
            std::snprintf(row.name, sizeof(row.name), "%s", FILE_PARENT_ENTRY_NAME);
            row.directory = true;
            row.parent = true;
            row.enabled = current_path_ != "/";
            continue;
        }
        const file_entry& entry = result->entries[item - 1U];
        copy_utf8_prefix(entry.name, row.name, sizeof(row.name));
        row.directory = entry.type == file_entry_type::directory;
        row.enabled = true;
        row.name_truncated = entry.name.size() >= sizeof(row.name);
    }
}

std::uint16_t file_app::page_count() const
{
    const storage_directory_result* result = nullptr;
    const std::size_t entry_count =
        storage_service_resolve_result(directory_handle_, result) && result != nullptr
            ? result->entries.size()
            : 0U;
    const std::size_t total = entry_count + 1U;
    return static_cast<std::uint16_t>(
        std::max<std::size_t>(1U, (total + FILE_VIEW_ROW_COUNT - 1U) / FILE_VIEW_ROW_COUNT));
}
