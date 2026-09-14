#include "books_app.hpp"

#include <algorithm>
#include <cstring>

#include <esp_log.h>

#include "app.hpp"
#include "books_cover.hpp"
#include "books_file_name.hpp"
#include "reader_launch.hpp"
#include "system_tick_service.hpp"
#include "text_layout_provider.hpp"

namespace {
constexpr char log_tag[] = "app_books";

books_preview_view_state preview_view_state(book_catalog_preview_state state)
{
    switch (state) {
        case book_catalog_preview_state::none:
            return books_preview_view_state::none;
        case book_catalog_preview_state::ready:
            return books_preview_view_state::ready;
        case book_catalog_preview_state::invalid_utf8:
            return books_preview_view_state::invalid_utf8;
        case book_catalog_preview_state::unavailable:
            return books_preview_view_state::unavailable;
    }
    return books_preview_view_state::unavailable;
}
}

void books_app::handle_app_event(const app_event& event)
{
    switch (event.type) {
        case app_event_type::ui_action:
            handle_action(event.action);
            break;
        case app_event_type::storage_status:
            handle_storage_status(event.storage_status);
            break;
        case app_event_type::storage_result:
            handle_storage_result(event.storage_result.handle);
            break;
        default:
            break;
    }
}

void books_app::on_open()
{
    settings_.cancel();
    media_generation_ = storage_service_get_media_generation();
    book_catalog_service_activate(media_generation_);
    refresh_catalog(false);
    submit_frame(ui_update_reason::view_opened);
    start_next_cover();
    ESP_LOGI(log_tag, "BooksApp opened generation=%lu page=%u items=%u",
             static_cast<unsigned long>(media_generation_),
             static_cast<unsigned>(page_index_),
             static_cast<unsigned>(total_items_));
}

void books_app::on_running()
{
    book_catalog_event event = {};
    bool changed = false;
    while (book_catalog_service_try_get_event(event)) {
        if (event.media_generation == media_generation_ &&
            event.revision != catalog_revision_) {
            changed = true;
        }
    }
    if (changed) {
        if (settings_.visible()) {
            catalog_dirty_ = true;
        } else {
            refresh_catalog(true);
        }
    }
    if (reader_launch_pending_) { try_launch_reader(); }
    if (!cover_request_.busy) { start_next_cover(); }
    if (cover_refresh_pending_) {
        const std::uint32_t now = system_tick_now_ms();
        bool all_finished = !cover_request_.busy;
        for (std::size_t index = 0U; index < page_item_count_; ++index) {
            if (page_items_[index].format == book_file_format::epub &&
                page_items_[index].cover_state == book_catalog_cover_state::ready &&
                cover_generations_[index] == 0U) {
                all_finished = false;
                break;
            }
        }
        if (all_finished || static_cast<std::int32_t>(now - cover_refresh_deadline_ms_) >= 0) {
            cover_refresh_pending_ = false;
            submit_frame(ui_update_reason::content_changed);
        }
    }
}

void books_app::on_close()
{
    settings_.cancel();
    cancel_cover_request();
    book_catalog_service_pause();
    reader_launch_pending_ = false;
    ESP_LOGI(log_tag, "BooksApp closed");
}

void books_app::handle_action(const ui_action_event& action)
{
    if (action.input.gesture != input_gesture_type::click) {
        return;
    }
    if (settings_.visible()) {
        switch (action.control) {
            case ui_control_type::books_setting_toggle_txt:
                settings_.toggle_txt();
                submit_frame(ui_update_reason::selection_changed, action.control);
                break;
            case ui_control_type::books_setting_toggle_epub:
                settings_.toggle_epub();
                submit_frame(ui_update_reason::selection_changed, action.control);
                break;
            case ui_control_type::books_setting_confirm:
                settings_.confirm();
                book_catalog_service_update_settings(media_generation_, settings_.saved());
                if (catalog_dirty_) {
                    catalog_dirty_ = false;
                    refresh_catalog(false);
                }
                submit_frame(ui_update_reason::popup_changed);
                break;
            case ui_control_type::books_setting_cancel:
                settings_.cancel();
                if (catalog_dirty_) {
                    catalog_dirty_ = false;
                    refresh_catalog(false);
                }
                submit_frame(ui_update_reason::popup_changed);
                break;
            default:
                break;
        }
        return;
    }

    switch (action.control) {
        case ui_control_type::books_back:
            app_request_back();
            break;
        case ui_control_type::books_settings:
            settings_.open();
            submit_frame(ui_update_reason::popup_changed);
            break;
        case ui_control_type::books_page_previous:
            if (page_index_ > 0U) {
                --page_index_;
                load_page(true);
                submit_frame(ui_update_reason::content_changed);
                start_next_cover();
            }
            break;
        case ui_control_type::books_page_next:
            if (page_index_ + 1U < page_count_) {
                ++page_index_;
                load_page(true);
                submit_frame(ui_update_reason::content_changed);
                start_next_cover();
            }
            break;
        case ui_control_type::books_select_item:
            request_reader(action.index);
            break;
        default:
            break;
    }
}

void books_app::handle_storage_status(const app_storage_status_event& event)
{
    if (event.media_generation == media_generation_ &&
        event.state != storage_state::ready) {
        catalog_state_ = book_catalog_state::unavailable;
    }
    if (event.media_generation != media_generation_ || event.state == storage_state::ready) {
        media_generation_ = event.media_generation;
        page_index_ = 0U;
        cancel_cover_request();
        ui_books_cover_clear();
        std::memset(cover_generations_, 0, sizeof(cover_generations_));
        book_catalog_service_activate(media_generation_);
        refresh_catalog(true);
    } else if (event.state != storage_state::ready) {
        total_items_ = page_item_count_ = page_count_ = 0U;
        std::memset(page_items_, 0, sizeof(page_items_));
        ui_books_cover_clear();
        submit_frame(ui_update_reason::content_changed);
    }
}

void books_app::refresh_catalog(bool render)
{
    book_catalog_snapshot snapshot = {};
    if (!book_catalog_service_snapshot(snapshot) ||
        snapshot.media_generation != media_generation_) {
        total_items_ = page_item_count_ = page_count_ = 0U;
        catalog_state_ = book_catalog_state::unavailable;
    } else {
        catalog_revision_ = snapshot.revision;
        total_items_ = snapshot.item_count;
        catalog_state_ = snapshot.state;
        settings_.load_saved(snapshot.settings);
        page_count_ = static_cast<std::uint16_t>(books_page_count(total_items_));
        page_index_ = static_cast<std::uint16_t>(
            books_clamp_page(total_items_, page_index_));
        load_page(false);
    }
    if (render) { submit_frame(ui_update_reason::content_changed); }
    start_next_cover();
}

void books_app::load_page(bool reset_covers)
{
    struct page_identity {
        char book_id[65];
        std::uint64_t source_size;
        std::int64_t modified_time;
        std::uint32_t cover_size;
        book_catalog_cover_state cover_state;
        book_cover_encoding cover_encoding;
    } previous[BOOK_CATALOG_PAGE_CAPACITY] = {};
    for (std::size_t index = 0U; index < page_item_count_; ++index) {
        std::strcpy(previous[index].book_id, page_items_[index].book_id);
        previous[index].source_size = page_items_[index].source_size;
        previous[index].modified_time = page_items_[index].modified_time;
        previous[index].cover_size = page_items_[index].cover_size;
        previous[index].cover_state = page_items_[index].cover_state;
        previous[index].cover_encoding = page_items_[index].cover_encoding;
    }
    const std::uint8_t previous_count = page_item_count_;
    std::memset(page_items_, 0, sizeof(page_items_));
    std::size_t copied = 0U;
    if (total_items_ != 0U) {
        book_catalog_service_copy_page(
            media_generation_, books_page_first_item(page_index_), page_items_,
            BOOK_CATALOG_PAGE_CAPACITY, copied);
    }
    if (!reset_covers) {
        reset_covers = copied != previous_count;
        for (std::size_t index = 0U; !reset_covers && index < copied; ++index) {
            if (std::strcmp(page_items_[index].book_id, previous[index].book_id) != 0 ||
                page_items_[index].source_size != previous[index].source_size ||
                page_items_[index].modified_time != previous[index].modified_time ||
                page_items_[index].cover_state != previous[index].cover_state ||
                page_items_[index].cover_size != previous[index].cover_size ||
                page_items_[index].cover_encoding != previous[index].cover_encoding) {
                reset_covers = true;
                break;
            }
        }
    }
    if (reset_covers) {
        cancel_cover_request();
        ui_books_cover_clear();
        std::memset(cover_generations_, 0, sizeof(cover_generations_));
        ++cover_session_;
    }
    page_item_count_ = static_cast<std::uint8_t>(copied);
}

void books_app::start_next_cover()
{
    if (cover_request_.busy || reader_launch_pending_ ||
        storage_service_get_state() != storage_state::ready ||
        storage_service_get_media_generation() != media_generation_) {
        return;
    }
    for (std::uint8_t slot = 0U; slot < page_item_count_; ++slot) {
        const auto& item = page_items_[slot];
        if (item.format != book_file_format::epub ||
            item.cover_state != book_catalog_cover_state::ready ||
            cover_generations_[slot] != 0U) {
            continue;
        }
        cover_request_ = {};
        cover_request_.slot = slot;
        cover_request_.expected_size = item.cover_size;
        if (!book_catalog_cover_path(item, cover_request_.path, sizeof(cover_request_.path)) ||
            !request_cover_chunk()) {
            cover_generations_[slot] = UINT32_MAX;
            cover_request_ = {};
            continue;
        }
        return;
    }
}

bool books_app::request_cover_chunk()
{
    cover_request_.request_id = ++next_cover_request_;
    cover_request_.busy = storage_service_read_file_chunk(
        cover_request_.path, cover_request_.offset,
        cover_request_.request_id, cover_session_, media_generation_);
    return cover_request_.busy;
}

void books_app::handle_storage_result(const result_handle& handle)
{
    const storage_file_chunk_result* result = nullptr;
    if (!storage_service_resolve_file_result(handle, result) || !cover_request_.busy ||
        result->request_id != cover_request_.request_id ||
        result->session_id != cover_session_ ||
        result->media_generation != media_generation_ ||
        result->offset != cover_request_.offset) {
        return;
    }
    cover_request_.busy = false;
    const std::uint8_t slot = cover_request_.slot;
    if (result->code != storage_result_code::ok ||
        result->file_size != cover_request_.expected_size ||
        (result->length == 0U && !result->end_of_file)) {
        ui_books_cover_cancel(slot);
        cover_generations_[slot] = UINT32_MAX;
        cover_request_ = {};
        start_next_cover();
        return;
    }
    if (!cover_request_.started) {
        cover_request_.started = ui_books_cover_begin(
            slot, static_cast<std::size_t>(result->file_size),
            page_items_[slot].cover_encoding);
    }
    if (!cover_request_.started ||
        !ui_books_cover_append(slot, static_cast<std::size_t>(result->offset),
                               reinterpret_cast<const std::uint8_t*>(result->data),
                               result->length)) {
        ui_books_cover_cancel(slot);
        cover_generations_[slot] = UINT32_MAX;
        cover_request_ = {};
        start_next_cover();
        return;
    }
    cover_request_.offset += result->length;
    if (result->end_of_file) {
        cover_generations_[slot] = ui_books_cover_commit(slot);
        if (cover_generations_[slot] == 0U) { cover_generations_[slot] = UINT32_MAX; }
        cover_request_ = {};
        cover_refresh_pending_ = true;
        cover_refresh_deadline_ms_ = system_tick_now_ms() + 1500U;
        start_next_cover();
    } else if (!request_cover_chunk()) {
        ui_books_cover_cancel(slot);
        cover_generations_[slot] = UINT32_MAX;
        cover_request_ = {};
        start_next_cover();
    }
}

void books_app::cancel_cover_request()
{
    if (cover_request_.started) { ui_books_cover_cancel(cover_request_.slot); }
    cover_request_ = {};
    ++cover_session_;
}

void books_app::request_reader(std::uint8_t index)
{
    if (index >= page_item_count_ || reader_launch_pending_) { return; }
    const auto& item = page_items_[index];
    std::strcpy(reader_path_, item.path);
    reader_format_ = item.format;
    reader_launch_pending_ = true;
    cancel_cover_request();
    book_catalog_service_pause();
    try_launch_reader();
}

void books_app::try_launch_reader()
{
    if (!reader_launch_pending_ || !book_catalog_service_idle()) { return; }
    app_launch_context context = {};
    if (reader_make_launch_context(context, reader_path_, media_generation_, reader_format_) &&
        app_request_launch(app_kind::reader, context)) {
        reader_launch_pending_ = false;
    }
}

books_view_state books_app::build_view() const
{
    books_view_state view = {};
    view.page_index = page_index_;
    view.page_count = page_count_;
    view.item_count = page_item_count_;
    view.settings_visible = settings_.visible();
    view.pending_settings.auto_scan_txt = settings_.pending().auto_scan_txt;
    view.pending_settings.auto_scan_epub = settings_.pending().auto_scan_epub;
    view.catalog_busy = catalog_state_ == book_catalog_state::loading_cache ||
                        catalog_state_ == book_catalog_state::scanning ||
                        catalog_state_ == book_catalog_state::enriching;
    view.catalog_error = catalog_state_ == book_catalog_state::error;
    const auto file_layout = ui_books_file_name_text_layout();
    const auto preview_layout = ui_books_preview_text_layout();
    for (std::uint8_t index = 0U; index < page_item_count_; ++index) {
        const auto& source = page_items_[index];
        auto& target = view.items[index];
        target.occupied = true;
        target.enabled = !reader_launch_pending_;
        target.format = source.format;
        target.cover_generation = cover_generations_[index] == UINT32_MAX
                                      ? 0U : cover_generations_[index];
        target.preview_state = preview_view_state(source.preview_state);
        format_books_file_name(source.path, target.file_name, file_layout);
        if (source.format == book_file_format::txt) {
            format_books_preview(source.preview, target, preview_layout);
        }
    }
    return view;
}

void books_app::submit_frame(
    ui_update_reason reason,
    ui_control_type changed_control)
{
    update_status_bar();
    ui_render_books(build_view(), reason, changed_control);
}

void books_app::update_status_bar() const
{
    const std::uint32_t current = page_count_ == 0U ? 0U : page_index_ + 1U;
    ui_status_bar_set_page_status(true, current, page_count_);
}
