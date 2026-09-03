#include "reader_app.hpp"

#include <cstring>

#include <esp_log.h>

#include "app.hpp"
#include "system_tick_service.hpp"
#include "text_layout_provider.hpp"

namespace {
constexpr char log_tag[] = "app_reader";
constexpr std::uint32_t request_timeout_ms = 10000U;
constexpr std::uint32_t loading_delay_ms = 400U;
}

bool reader_app::prepare_launch(const app_launch_context& context)
{
    if (active_ || context.file_path[0] != '/' ||
        std::memchr(context.file_path, '\0', sizeof(context.file_path)) == nullptr) {
        return false;
    }
    std::strcpy(path_, context.file_path);
    media_generation_ = context.media_generation;
    prepared_ = true;
    return true;
}

void reader_app::on_open()
{
    ++session_id_;
    active_ = true;
    media_valid_ = true;
    metadata_known_ = false;
    position_valid_ = false;
    waiting_ = false;
    busy_ = false;
    frame_pending_ = false;
    book_opened_ = book_submitted_ = progress_persistent_ = false;
    index_valid_ = index_position_valid_ = book_waiting_ = index_lookup_ = false;
    user_navigated_ = indexed_target_valid_ = false;
    current_page_ = total_pages_ = 0U;
    identity_ = {};
    current_offset_ = next_offset_ = 0U;
    end_of_file_ = false;
    history_.clear();
    layout_ = ui_reader_text_layout();
    status_ = reader_view_status::loading;
    if (!prepared_) {
        status_ = reader_view_status::file_not_found;
    } else if (storage_service_get_state() != storage_state::ready ||
               storage_service_get_media_generation() != media_generation_) {
        media_valid_ = false;
        status_ = storage_service_get_state() == storage_state::no_card
                      ? reader_view_status::no_card : reader_view_status::storage_error;
    } else {
        start_page(0U, page_operation::open);
        book_submitted_ = book_service_open(path_, layout_, session_id_, media_generation_);
        if (!book_submitted_) { ESP_LOGW(log_tag, "book open not queued; reading without SD progress"); }
    }
    prepared_ = false;
    loading_shown_ = true;
    submit_frame(ui_update_reason::view_opened);
    update_status_page();
}

void reader_app::on_close()
{
    active_ = false;
    waiting_ = busy_ = frame_pending_ = false;
    if (position_valid_ && book_submitted_ &&
        !book_service_close(session_id_, media_generation_, current_offset_)) {
        ESP_LOGW(log_tag, "SD progress save not queued");
    }
    ++session_id_;
    book_waiting_ = index_lookup_ = index_valid_ = index_position_valid_ = false;
    update_status_page();
    path_[0] = '\0';
    history_.clear();
    prepared_ = false;
}

bool reader_app::check_media()
{
    if (!media_valid_) {
        return false;
    }
    const auto state = storage_service_get_state();
    if (storage_service_get_media_generation() != media_generation_ ||
        state != storage_state::ready) {
        media_valid_ = false;
        fail(state == storage_state::no_card ? reader_view_status::no_card
                                            : reader_view_status::storage_error);
        return false;
    }
    return true;
}

void reader_app::on_running()
{
    if (!active_) {
        return;
    }
    check_media();
    const auto now = system_tick_now_ms();
    if (book_waiting_ && now - book_request_started_ms_ >= request_timeout_ms) {
        book_waiting_ = false;
        index_valid_ = index_position_valid_ = false;
        if (index_lookup_) { index_lookup_ = false; fail(reader_view_status::storage_error); }
        update_status_page();
    }
    if (busy_ && !index_lookup_) {
        const auto now = system_tick_now_ms();
        if (waiting_ && now - request_started_ms_ >= request_timeout_ms) {
            fail(reader_view_status::storage_error);
        } else if (!waiting_) {
            request_chunk();
        }
        if (busy_ && !loading_shown_ && now - loading_started_ms_ >= loading_delay_ms) {
            loading_shown_ = true;
            submit_frame(ui_update_reason::content_changed);
        }
    }
    if (frame_pending_) {
        submit_frame(pending_reason_);
    }
    if (index_lookup_ && !loading_shown_ && now - loading_started_ms_ >= loading_delay_ms) {
        loading_shown_ = true;
        submit_frame(ui_update_reason::content_changed);
    }
    if (index_valid_ && !index_position_valid_ && !busy_ && !book_waiting_ && position_valid_ &&
        (status_ == reader_view_status::ready || status_ == reader_view_status::empty_file)) {
        query_index(false);
    }
}

void reader_app::handle_app_event(const app_event& event)
{
    if (!active_) {
        return;
    }
    switch (event.type) {
        case app_event_type::ui_action:
            handle_action(event.action);
            break;
        case app_event_type::storage_result:
            handle_result(event.storage_result.handle);
            break;
        case app_event_type::storage_status:
            // Use live state: a queued status may predate this Reader session.
            check_media();
            break;
        case app_event_type::book:
            handle_book_event(event.book);
            break;
        case app_event_type::rtc:
        case app_event_type::battery:
            break;
    }
}

void reader_app::handle_action(const ui_action_event& action)
{
    if (action.input.gesture != input_gesture_type::click) {
        return;
    }
    if (action.control == ui_control_type::navigate_back) {
        app_request_back();
        return;
    }
    if (busy_ || status_ != reader_view_status::ready || !check_media()) {
        return;
    }
    if (action.control == ui_control_type::reader_next_page && !end_of_file_) {
        user_navigated_ = true;
        if (index_valid_ && index_position_valid_ && !book_waiting_ && current_page_ + 1U < total_pages_) {
            indexed_operation_ = page_operation::next;
            if (query_index(true, current_page_ + 1U)) { return; }
        }
        start_page(next_offset_, page_operation::next);
    } else if (action.control == ui_control_type::reader_previous_page && current_offset_ > 0U) {
        user_navigated_ = true;
        if (index_valid_ && index_position_valid_ && !book_waiting_ && current_page_ > 0U) {
            indexed_operation_ = page_operation::previous;
            if (query_index(true, current_page_ - 1U)) { return; }
        }
        std::uint64_t previous = 0U;
        if (history_.previous(previous)) {
            start_page(previous, page_operation::previous);
        } else {
            rebuild_target_ = current_offset_;
            start_page(0U, page_operation::rebuild_previous);
        }
    }
}

void reader_app::start_page(std::uint64_t offset, page_operation operation)
{
    operation_ = operation;
    paginator_.reset(offset, layout_);
    status_ = reader_view_status::loading;
    busy_ = true;
    waiting_ = false;
    loading_shown_ = false;
    loading_started_ms_ = system_tick_now_ms();
}

void reader_app::request_chunk()
{
    if (!check_media()) {
        return;
    }
    ++request_id_;
    requested_offset_ = paginator_.read_offset();
    request_started_ms_ = system_tick_now_ms();
    waiting_ = storage_service_read_file_chunk(
        path_, requested_offset_, request_id_, session_id_, media_generation_);
    if (!waiting_) {
        fail(reader_view_status::storage_error);
    }
}

void reader_app::handle_result(const result_handle& handle)
{
    const storage_file_chunk_result* result = nullptr;
    if (!waiting_ || !storage_service_resolve_file_result(handle, result) ||
        result->session_id != session_id_ || result->request_id != request_id_ ||
        result->media_generation != media_generation_ || result->offset != requested_offset_) {
        return;
    }
    // The dispatcher owns the reference throughout this call. Consume/copy
    // synchronously; never retain a raw pointer after returning to it.
    waiting_ = false;
    if (!check_media()) {
        return;
    }
    if (result->code != storage_result_code::ok) {
        fail(result->code == storage_result_code::file_not_found
                 ? reader_view_status::file_not_found
             : result->code == storage_result_code::no_card
                 ? reader_view_status::no_card : reader_view_status::storage_error);
        return;
    }
    if (result->length > sizeof(result->data) || result->offset > result->file_size ||
        result->length > result->file_size - result->offset ||
        (result->length == 0U && !result->end_of_file)) {
        fail(reader_view_status::storage_error);
        return;
    }
    if (!metadata_known_) {
        identity_.file_size = result->file_size;
        identity_.modified_time = result->modified_time;
        metadata_known_ = true;
    } else if (identity_.file_size != result->file_size ||
               identity_.modified_time != result->modified_time) {
        position_valid_ = false;
        fail(reader_view_status::storage_error);
        return;
    }
    const auto parsed = paginator_.feed(result->data, result->length, result->end_of_file);
    if (parsed == reader_parse_status::invalid_utf8) {
        fail(reader_view_status::invalid_utf8);
    } else if (parsed == reader_parse_status::page_ready) {
        complete_page();
    }
}

void reader_app::complete_page()
{
    const auto& page = paginator_.page();
    if (!page.end_of_file && page.next_page_start_offset <= page.current_page_start_offset) {
        fail(reader_view_status::storage_error);
        return;
    }
    if (operation_ == page_operation::rebuild_previous &&
        page.next_page_start_offset < rebuild_target_ && !page.end_of_file) {
        history_.push(page.current_page_start_offset);
        const auto next = page.next_page_start_offset;
        paginator_.reset(next, layout_);
        return;
    }
    if (operation_ == page_operation::next) {
        history_.push(current_offset_);
    } else if (operation_ == page_operation::previous) {
        history_.pop();
    }
    current_offset_ = page.current_page_start_offset;
    next_offset_ = page.next_page_start_offset;
    end_of_file_ = page.end_of_file;
    position_valid_ = true;
    busy_ = false;
    status_ = page.empty && page.end_of_file && current_offset_ == 0U
                  ? reader_view_status::empty_file : reader_view_status::ready;
    index_position_valid_ = index_valid_ && indexed_target_valid_ && current_offset_ == indexed_target_offset_;
    if (index_position_valid_) { current_page_ = indexed_target_page_; }
    indexed_target_valid_ = false;
    update_status_page();
    submit_frame(ui_update_reason::content_changed);
}

void reader_app::fail(reader_view_status status)
{
    waiting_ = busy_ = false;
    index_lookup_ = book_waiting_ = index_position_valid_ = false;
    status_ = status;
    update_status_page();
    submit_frame(ui_update_reason::content_changed);
}

void reader_app::submit_frame(ui_update_reason reason)
{
    if (frame_pending_ && pending_reason_ == ui_update_reason::view_opened) {
        reason = ui_update_reason::view_opened;
    }
    pending_reason_ = reason;
    frame_pending_ = !ui_write_reader_frame(reason, write_frame, this);
}

bool reader_app::write_frame(reader_view_state& view, const void* context)
{
    const auto& instance = *static_cast<const reader_app*>(context);
    view = {};
    view.status = instance.status_;
    view.file_size = instance.identity_.file_size;
    view.progress_persistent = instance.progress_persistent_;
    if (view.status == reader_view_status::ready || view.status == reader_view_status::empty_file) {
        view.page = instance.paginator_.page();
        view.previous_enabled = instance.current_offset_ > 0U;
        view.next_enabled = !instance.end_of_file_;
    }
    return true;
}

void reader_app::update_status_page()
{
    const bool valid = active_ && index_valid_ && index_position_valid_ && position_valid_ &&
        current_page_ < total_pages_ &&
        (status_ == reader_view_status::ready || status_ == reader_view_status::empty_file);
    if (ui_status_bar_update_reader_page(valid, valid ? current_page_ + 1U : 0U, valid ? total_pages_ : 0U)) {
        ui_renderer_notify_status_bar();
    }
}

bool reader_app::query_index(bool by_page, std::uint32_t page)
{
    if (!index_valid_ || book_waiting_ || !book_submitted_) { return false; }
    ++book_request_id_;
    if (!book_service_query(session_id_, media_generation_, book_request_id_, by_page, page, current_offset_)) {
        return false;
    }
    book_waiting_ = true;
    queried_offset_ = current_offset_;
    book_request_started_ms_ = system_tick_now_ms();
    if (by_page) {
        index_lookup_ = busy_ = true;
        waiting_ = false;
        loading_shown_ = false;
        loading_started_ms_ = book_request_started_ms_;
        status_ = reader_view_status::loading;
    }
    return true;
}

void reader_app::handle_book_event(const book_service_event& event)
{
    if (!book_submitted_ || event.session_id != session_id_ || event.media_generation != media_generation_ ||
        !check_media()) { return; }
    const bool persistence_changed = progress_persistent_ != event.persistent;
    progress_persistent_ = event.persistent;
    if (event.error != ESP_OK) {
        index_valid_ = index_position_valid_ = book_waiting_ = false;
        if (index_lookup_) { index_lookup_ = false; fail(reader_view_status::storage_error); }
        update_status_page();
        if (persistence_changed) { submit_frame(ui_update_reason::content_changed); }
        return;
    }
    if (metadata_known_ && (event.file_size != identity_.file_size || event.modified_time != identity_.modified_time)) {
        position_valid_ = false;
        index_valid_ = false;
        fail(reader_view_status::storage_error);
        return;
    }
    if (event.type == book_event_type::opened) {
        book_opened_ = true;
        if (!user_navigated_ && event.progress.byte_offset > 0U && event.progress.byte_offset < event.file_size) {
            history_.clear();
            start_page(event.progress.byte_offset, page_operation::open);
        }
    } else if (event.type == book_event_type::ready) {
        book_opened_ = true;
        index_valid_ = event.index_valid && event.page_count > 0U && event.progress.page < event.page_count;
        total_pages_ = index_valid_ ? event.page_count : 0U;
        if (index_valid_ && !user_navigated_) {
            indexed_target_valid_ = true;
            indexed_target_page_ = event.progress.page;
            indexed_target_offset_ = event.progress.byte_offset;
            if (position_valid_ && !busy_ && current_offset_ == event.progress.byte_offset) {
                current_page_ = event.progress.page;
                index_position_valid_ = true;
                indexed_target_valid_ = false;
                update_status_page();
            } else {
                history_.clear();
                start_page(event.progress.byte_offset, page_operation::open);
            }
        }
    } else if (event.type == book_event_type::position && book_waiting_ && event.request_id == book_request_id_) {
        book_waiting_ = false;
        const bool load = index_lookup_;
        index_lookup_ = false;
        if (!event.index_valid || event.page_count != total_pages_ || event.progress.page >= total_pages_) {
            index_valid_ = index_position_valid_ = false;
            if (load) { fail(reader_view_status::storage_error); }
            update_status_page();
            return;
        }
        if (load || (!busy_ && queried_offset_ == current_offset_ &&
            (status_ == reader_view_status::ready || status_ == reader_view_status::empty_file))) {
            indexed_target_page_ = event.progress.page;
            indexed_target_offset_ = event.progress.byte_offset;
            indexed_target_valid_ = true;
            if (load || current_offset_ != event.progress.byte_offset) {
                if (!load) { history_.clear(); }
                start_page(event.progress.byte_offset, load ? indexed_operation_ : page_operation::open);
            } else {
                current_page_ = event.progress.page;
                index_position_valid_ = true;
                indexed_target_valid_ = false;
                update_status_page();
            }
        }
    }
    if (persistence_changed) { submit_frame(ui_update_reason::content_changed); }
}
