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
    current_offset_ = next_offset_ = 0U;
    end_of_file_ = false;
    history_.clear();
    layout_ = ui_reader_text_layout();
    status_ = reader_view_status::loading;
    if (!prepared_ || !reading_progress_identify(path_, identity_)) {
        status_ = reader_view_status::file_not_found;
    } else if (storage_service_get_state() != storage_state::ready ||
               storage_service_get_media_generation() != media_generation_) {
        media_valid_ = false;
        status_ = storage_service_get_state() == storage_state::no_card
                      ? reader_view_status::no_card : reader_view_status::storage_error;
    } else {
        start_page(0U, page_operation::open);
    }
    prepared_ = false;
    loading_shown_ = true;
    submit_frame(ui_update_reason::view_opened);
}

void reader_app::on_close()
{
    active_ = false;
    ++session_id_;
    waiting_ = busy_ = frame_pending_ = false;
    if (position_valid_) {
        const esp_err_t error = reading_progress_save(identity_, current_offset_);
        if (error != ESP_OK) {
            ESP_LOGW(log_tag, "progress retained in RAM; persistent save=%s", esp_err_to_name(error));
        }
    }
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
    if (busy_) {
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
        start_page(next_offset_, page_operation::next);
    } else if (action.control == ui_control_type::reader_previous_page && current_offset_ > 0U) {
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
        std::uint64_t saved_offset = 0U;
        if (reading_progress_load(identity_, saved_offset) && saved_offset != 0U) {
            paginator_.reset(saved_offset, layout_);
            return;
        }
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
    submit_frame(ui_update_reason::content_changed);
}

void reader_app::fail(reader_view_status status)
{
    waiting_ = busy_ = false;
    status_ = status;
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
    view.progress_persistent = reading_progress_is_persistent();
    if (view.status == reader_view_status::ready || view.status == reader_view_status::empty_file) {
        view.page = instance.paginator_.page();
        view.previous_enabled = instance.current_offset_ > 0U;
        view.next_enabled = !instance.end_of_file_;
    }
    return true;
}
