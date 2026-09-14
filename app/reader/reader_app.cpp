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
    reader_launch_parameters parameters = {};
    if (state_.session.active || !reader_read_launch_context(context, parameters)) {
        return false;
    }
    std::strcpy(state_.session.path, parameters.file_path);
    state_.session.media_generation = parameters.media_generation;
    state_.session.format = parameters.format;
    state_.session.prepared = true;
    return true;
}

void reader_app::on_open()
{
    const session_state prepared_session = state_.session;
    state_ = {};
    state_.session = prepared_session;
    state_.session.id = ++session_serial_;
    state_.session.active = true;
    state_.session.media_valid = true;
    state_.book.content_ready = state_.session.format == book_file_format::txt;
    view_ = {};
    ui_reader_cover_clear();
    history_.clear();
    layout_ = ui_reader_text_layout();
    if (!state_.session.prepared) {
        state_.page.status = reader_view_status::file_not_found;
    } else if (storage_service_get_state() != storage_state::ready ||
               storage_service_get_media_generation() != state_.session.media_generation) {
        state_.session.media_valid = false;
        state_.page.status = storage_service_get_state() == storage_state::no_card
                      ? reader_view_status::no_card : reader_view_status::storage_error;
    } else {
        if (state_.session.format == book_file_format::txt) { start_page(0U, page_operation::open); }
        state_.book.submitted = book_service_open(state_.session.path, layout_, state_.session.id,
                                                  state_.session.media_generation,
                                                  state_.session.format);
        if (!state_.book.submitted) {
            ESP_LOGW(log_tag, "book open not queued; reading without SD progress");
            if (state_.session.format == book_file_format::epub) {
                state_.page.status = reader_view_status::storage_error;
            }
        }
    }
    if (state_.page.status != reader_view_status::loading) {
        state_.content_request.loading_shown = true;
        submit_frame(ui_update_reason::view_opened);
    }
}

void reader_app::on_close()
{
    state_.session.active = false;
    ui_reader_cover_clear();
    if (state_.page.position_valid && state_.book.submitted &&
        !book_service_close(state_.session.id, state_.session.media_generation, state_.page.current_offset, state_.cover.showing)) {
        ESP_LOGW(log_tag, "SD progress save not queued");
    }
    ++session_serial_;
    state_ = {};
    view_ = {};
    history_.clear();
    update_status_page();
}

bool reader_app::check_media()
{
    if (!state_.session.media_valid) {
        return false;
    }
    const auto state = storage_service_get_state();
    if (storage_service_get_media_generation() != state_.session.media_generation ||
        state != storage_state::ready) {
        state_.session.media_valid = false;
        fail(state == storage_state::no_card ? reader_view_status::no_card
                                            : reader_view_status::storage_error);
        return false;
    }
    return true;
}

void reader_app::on_running()
{
    if (!state_.session.active) {
        return;
    }
    check_media();
    const auto now = system_tick_now_ms();
    if (state_.book.waiting && now - state_.book.request_started_ms >= request_timeout_ms) {
        state_.book.waiting = false;
        state_.book.index_valid = state_.book.index_position_valid = false;
        if (state_.book.index_lookup) { state_.book.index_lookup = false; fail(reader_view_status::storage_error); }
        update_status_page();
    }
    if (state_.content_request.busy && !state_.book.index_lookup) {
        const auto now = system_tick_now_ms();
        if (state_.content_request.waiting && now - state_.content_request.started_ms >= request_timeout_ms) {
            fail(reader_view_status::storage_error);
        } else if (!state_.content_request.waiting) {
            if (state_.cover.waiting) { request_cover(); }
            else { request_chunk(); }
        }
        if (state_.content_request.busy && !state_.presentation.opening &&
            !state_.content_request.loading_shown &&
            now - state_.content_request.loading_started_ms >= loading_delay_ms) {
            state_.content_request.loading_shown = true;
            submit_frame(ui_update_reason::content_changed);
        }
    }
    if (state_.presentation.frame_pending) {
        const bool submitted = ui_write_reader_frame(
            state_.presentation.pending_reason, write_frame, this);
        state_.presentation.frame_pending = !submitted;
        if (submitted &&
            state_.presentation.pending_reason == ui_update_reason::view_opened) {
            state_.presentation.opening = false;
        }
    }
    if (state_.book.index_lookup && !state_.presentation.opening &&
        !state_.content_request.loading_shown &&
        now - state_.content_request.loading_started_ms >= loading_delay_ms) {
        state_.content_request.loading_shown = true;
        submit_frame(ui_update_reason::content_changed);
    }
    if (state_.book.index_valid && !state_.book.index_position_valid &&
        !state_.content_request.busy && !state_.book.waiting && state_.page.position_valid &&
        (state_.page.status == reader_view_status::ready || state_.page.status == reader_view_status::empty_file)) {
        query_index(false);
    }
}

void reader_app::handle_app_event(const app_event& event)
{
    if (!state_.session.active) {
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
        case app_event_type::book_result:
            handle_book_result(event.book_result);
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
        if (state_.navigation.menu_visible) { app_request_back(); }
        return;
    }
    if (action.control == ui_control_type::reader_menu_zone) {
        state_.navigation.menu_visible = !state_.navigation.menu_visible;
        submit_frame(ui_update_reason::popup_changed);
        return;
    }
    if (state_.navigation.menu_visible || state_.content_request.busy ||
        state_.page.status != reader_view_status::ready || !check_media()) {
        return;
    }
    if (state_.cover.showing) {
        if (action.control == ui_control_type::reader_next_zone) {
            state_.cover.showing = false;
            state_.navigation.user_navigated = true;
            start_body(0U, page_operation::open);
        }
        return;
    }
    if (action.control == ui_control_type::reader_next_zone && !state_.page.end_of_file) {
        state_.navigation.user_navigated = true;
        if (state_.book.index_valid && state_.book.index_position_valid && !state_.book.waiting &&
            state_.page.current_number + 1U < state_.page.total_count) {
            start_indexed_page(
                state_.page.next_offset,
                page_operation::next,
                state_.page.current_number + 1U);
            return;
        }
        start_page(state_.page.next_offset, page_operation::next);
    } else if (action.control == ui_control_type::reader_previous_zone && state_.page.current_offset == 0U &&
               state_.session.format == book_file_format::epub && state_.cover.available) {
        state_.navigation.user_navigated = true;
        if (state_.cover.generation != 0U) {
            state_.cover.showing = true;
            submit_frame(ui_update_reason::content_changed);
        } else {
            state_.cover.offset = 0U;
            state_.cover.started = false;
            state_.cover.waiting = state_.content_request.busy = true;
            state_.page.status = reader_view_status::loading;
            state_.content_request.loading_shown = false;
            state_.content_request.loading_started_ms = system_tick_now_ms();
            if (!request_cover()) {
                state_.cover.waiting = state_.cover.available = false;
                start_body(0U, page_operation::open);
            }
        }
    } else if (action.control == ui_control_type::reader_previous_zone && state_.page.current_offset > 0U) {
        state_.navigation.user_navigated = true;
        std::uint64_t previous = 0U;
        if (history_.previous(previous)) {
            if (state_.book.index_valid && state_.book.index_position_valid &&
                state_.page.current_number > 0U) {
                start_indexed_page(
                    previous,
                    page_operation::previous,
                    state_.page.current_number - 1U);
            } else {
                start_page(previous, page_operation::previous);
            }
        } else if (state_.book.index_valid && state_.book.index_position_valid &&
                   !state_.book.waiting && state_.page.current_number > 0U) {
            state_.book.indexed_operation = page_operation::previous;
            if (query_index(true, state_.page.current_number - 1U)) { return; }
            state_.page.rebuild_target = state_.page.current_offset;
            start_page(0U, page_operation::rebuild_previous);
        } else {
            state_.page.rebuild_target = state_.page.current_offset;
            start_page(0U, page_operation::rebuild_previous);
        }
    }
}

void reader_app::start_body(std::uint64_t offset, page_operation operation)
{
    state_.cover.showing = false;
    state_.cover.waiting = false;
    start_page(offset, operation);
}

void reader_app::start_page(std::uint64_t offset, page_operation operation)
{
    state_.page.operation = operation;
    paginator_.reset(offset, layout_);
    state_.page.status = reader_view_status::loading;
    state_.content_request.busy = true;
    state_.content_request.waiting = false;
    state_.content_request.loading_shown = false;
    state_.content_request.loading_started_ms = system_tick_now_ms();
}

void reader_app::start_indexed_page(
    std::uint64_t offset,
    page_operation operation,
    std::uint32_t page)
{
    state_.book.indexed_target_offset = offset;
    state_.book.indexed_target_page = page;
    state_.book.indexed_target_valid = true;
    state_.book.index_position_valid = false;
    start_page(offset, operation);
}

void reader_app::request_chunk()
{
    if (!check_media()) {
        return;
    }
    ++state_.content_request.id;
    state_.content_request.requested_offset = paginator_.read_offset();
    state_.content_request.started_ms = system_tick_now_ms();
    state_.content_request.waiting = state_.session.format == book_file_format::epub
                   ? book_service_read(state_.session.id, state_.session.media_generation, state_.content_request.id,
                                       book_content_kind::text, state_.content_request.requested_offset)
                   : storage_service_read_file_chunk(
                         state_.session.path, state_.content_request.requested_offset,
                         state_.content_request.id, state_.session.id,
                         state_.session.media_generation);
    if (!state_.content_request.waiting) {
        fail(reader_view_status::storage_error);
    }
}

void reader_app::handle_result(const result_handle& handle)
{
    const storage_file_chunk_result* result = nullptr;
    if (state_.session.format != book_file_format::txt || !state_.content_request.waiting ||
        !storage_service_resolve_file_result(handle, result) ||
        result->session_id != state_.session.id || result->request_id != state_.content_request.id ||
        result->media_generation != state_.session.media_generation || result->offset != state_.content_request.requested_offset) {
        return;
    }
    // The dispatcher owns the reference throughout this call. Consume/copy
    // synchronously; never retain a raw pointer after returning to it.
    state_.content_request.waiting = false;
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
    if (!state_.book.metadata_known) {
        state_.book.identity.file_size = result->file_size;
        state_.book.identity.modified_time = result->modified_time;
        state_.book.metadata_known = true;
    } else if (state_.book.identity.file_size != result->file_size ||
               state_.book.identity.modified_time != result->modified_time) {
        state_.page.position_valid = false;
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

bool reader_app::request_cover()
{
    ++state_.content_request.id;
    state_.content_request.requested_offset = state_.cover.offset;
    state_.content_request.started_ms = system_tick_now_ms();
    state_.content_request.waiting = book_service_read(state_.session.id, state_.session.media_generation, state_.content_request.id,
                                 book_content_kind::cover, state_.cover.offset);
    if (!state_.content_request.waiting) { ui_reader_cover_cancel(); }
    return state_.content_request.waiting;
}

void reader_app::handle_book_result(const result_handle& handle)
{
    const book_content_result* result = nullptr;
    if (state_.session.format != book_file_format::epub || !state_.content_request.waiting ||
        !book_service_resolve_result(handle, result) || result == nullptr ||
        result->session_id != state_.session.id || result->request_id != state_.content_request.id ||
        result->media_generation != state_.session.media_generation ||
        result->offset != state_.content_request.requested_offset) { return; }
    state_.content_request.waiting = false;
    if (!check_media()) { return; }
    if (result->error != ESP_OK || result->length > sizeof(result->data) ||
        result->offset > result->file_size || result->length > result->file_size - result->offset ||
        (result->length == 0U && !result->end_of_file)) {
        if (state_.cover.waiting) {
            ui_reader_cover_cancel();
            state_.cover.waiting = state_.cover.available = false;
            start_body(0U, page_operation::open);
        } else { fail(reader_view_status::storage_error); }
        return;
    }
    if (result->kind == book_content_kind::cover && state_.cover.waiting) {
        if (!state_.cover.started) {
            state_.cover.started = ui_reader_cover_begin(static_cast<std::size_t>(result->file_size),
                                                    result->cover_encoding);
            if (!state_.cover.started) {
                state_.cover.waiting = state_.cover.available = false;
                start_body(0U, page_operation::open);
                return;
            }
        }
        if (!ui_reader_cover_append(static_cast<std::size_t>(result->offset), result->data, result->length)) {
            ui_reader_cover_cancel();
            state_.cover.waiting = state_.cover.available = false;
            start_body(0U, page_operation::open);
            return;
        }
        state_.cover.offset += result->length;
        if (result->end_of_file) {
            state_.cover.generation = ui_reader_cover_commit();
            state_.cover.waiting = state_.content_request.busy = false;
            state_.cover.showing = state_.cover.generation != 0U;
            if (!state_.cover.showing) {
                state_.cover.available = false;
                start_body(0U, page_operation::open);
            } else {
                state_.page.status = reader_view_status::ready;
                submit_frame(ui_update_reason::content_changed);
            }
        }
        return;
    }
    if (result->kind != book_content_kind::text || state_.cover.waiting) { return; }
    if (!state_.book.metadata_known) {
        state_.book.identity.file_size = result->file_size;
        state_.book.identity.modified_time = 0;
        state_.book.metadata_known = true;
    } else if (state_.book.identity.file_size != result->file_size) {
        state_.page.position_valid = false;
        fail(reader_view_status::storage_error);
        return;
    }
    const auto parsed = paginator_.feed(reinterpret_cast<const char*>(result->data),
                                        result->length, result->end_of_file);
    if (parsed == reader_parse_status::invalid_utf8) { fail(reader_view_status::invalid_epub); }
    else if (parsed == reader_parse_status::page_ready) { complete_page(); }
}

void reader_app::complete_page()
{
    const auto& page = paginator_.page();
    if (!page.end_of_file && page.next_page_start_offset <= page.current_page_start_offset) {
        fail(reader_view_status::storage_error);
        return;
    }
    if (state_.page.operation == page_operation::rebuild_previous &&
        page.next_page_start_offset < state_.page.rebuild_target && !page.end_of_file) {
        history_.push(page.current_page_start_offset);
        const auto next = page.next_page_start_offset;
        paginator_.reset(next, layout_);
        return;
    }
    if (state_.page.operation == page_operation::next) {
        history_.push(state_.page.current_offset);
    } else if (state_.page.operation == page_operation::previous) {
        history_.pop();
    }
    state_.page.current_offset = page.current_page_start_offset;
    state_.page.next_offset = page.next_page_start_offset;
    state_.page.end_of_file = page.end_of_file;
    state_.page.position_valid = true;
    state_.content_request.busy = false;
    state_.page.status = page.empty && page.end_of_file && state_.page.current_offset == 0U
                  ? reader_view_status::empty_file : reader_view_status::ready;
    state_.book.index_position_valid = state_.book.index_valid &&
        state_.book.indexed_target_valid &&
        state_.page.current_offset == state_.book.indexed_target_offset;
    if (state_.book.index_position_valid) { state_.page.current_number = state_.book.indexed_target_page; }
    state_.book.indexed_target_valid = false;
    submit_frame(ui_update_reason::content_changed);
}

void reader_app::fail(reader_view_status status)
{
    state_.content_request.waiting = state_.content_request.busy = false;
    state_.cover.waiting = false;
    ui_reader_cover_cancel();
    state_.book.index_lookup = state_.book.waiting = state_.book.index_position_valid = false;
    state_.page.status = status;
    submit_frame(ui_update_reason::content_changed);
}

void reader_app::submit_frame(ui_update_reason reason)
{
    if (reason != ui_update_reason::popup_changed) {
        view_ = {};
        view_.status = state_.page.status;
        view_.file_size = state_.book.identity.file_size;
        view_.progress_persistent = state_.book.progress_persistent;
        view_.showing_cover = state_.cover.showing;
        view_.cover_generation = state_.cover.generation;
        if (state_.page.status == reader_view_status::ready || state_.page.status == reader_view_status::empty_file) {
            view_.page = paginator_.page();
            view_.previous_enabled = !state_.cover.showing &&
                (state_.page.current_offset > 0U || (state_.session.format == book_file_format::epub && state_.cover.available));
            view_.next_enabled = state_.cover.showing || !state_.page.end_of_file;
        }
    }
    view_.menu_visible = state_.navigation.menu_visible;
    if (state_.presentation.opening ||
        (state_.presentation.frame_pending &&
         state_.presentation.pending_reason == ui_update_reason::view_opened)) {
        reason = ui_update_reason::view_opened;
    } else if (state_.presentation.frame_pending && state_.presentation.pending_reason == ui_update_reason::content_changed &&
               reason == ui_update_reason::popup_changed) {
        reason = ui_update_reason::content_changed;
    }
    state_.presentation.pending_reason = reason;
    update_status_page(false);
    const bool submitted = ui_write_reader_frame(reason, write_frame, this);
    state_.presentation.frame_pending = !submitted;
    if (submitted && reason == ui_update_reason::view_opened) {
        state_.presentation.opening = false;
    }
}

bool reader_app::write_frame(reader_view_state& view, const void* context)
{
    const auto& instance = *static_cast<const reader_app*>(context);
    view = instance.view_;
    return true;
}

void reader_app::update_status_page(bool notify)
{
    const bool valid = state_.session.active && state_.book.index_valid && state_.book.index_position_valid && state_.page.position_valid &&
        !state_.cover.showing && state_.page.current_number < state_.page.total_count &&
        (state_.page.status == reader_view_status::ready || state_.page.status == reader_view_status::empty_file);
    if (ui_status_bar_update_reader_page(
            valid,
            valid ? state_.page.current_number + 1U : 0U,
            valid ? state_.page.total_count : 0U) && notify) {
        ui_renderer_notify_status_bar();
    }
}

bool reader_app::query_index(bool by_page, std::uint32_t page)
{
    if (!state_.book.index_valid || state_.book.waiting || !state_.book.submitted) { return false; }
    ++state_.book.request_id;
    if (!book_service_query(state_.session.id, state_.session.media_generation,
                            state_.book.request_id, by_page, page,
                            state_.page.current_offset)) {
        return false;
    }
    state_.book.waiting = true;
    state_.book.queried_offset = state_.page.current_offset;
    state_.book.request_started_ms = system_tick_now_ms();
    if (by_page) {
        state_.book.index_lookup = state_.content_request.busy = true;
        state_.content_request.waiting = false;
        state_.content_request.loading_shown = false;
        state_.content_request.loading_started_ms = state_.book.request_started_ms;
        state_.page.status = reader_view_status::loading;
    }
    return true;
}

void reader_app::handle_book_event(const book_service_event& event)
{
    if (!state_.book.submitted || event.session_id != state_.session.id || event.media_generation != state_.session.media_generation ||
        event.format != state_.session.format || !check_media()) { return; }
    const bool persistence_changed = state_.book.progress_persistent != event.persistent;
    state_.book.progress_persistent = event.persistent;
    if (event.error != ESP_OK) {
        state_.book.index_valid = state_.book.index_position_valid = state_.book.waiting = false;
        if (state_.session.format == book_file_format::epub && !state_.book.content_ready) {
            const auto status = event.error == ESP_ERR_NOT_SUPPORTED
                                    ? reader_view_status::unsupported_epub
                                    : (event.error == ESP_ERR_INVALID_ARG || event.error == ESP_ERR_INVALID_SIZE ||
                                       event.error == ESP_ERR_INVALID_RESPONSE || event.error == ESP_ERR_INVALID_CRC ||
                                       event.error == ESP_ERR_NOT_FOUND)
                                          ? reader_view_status::invalid_epub
                                          : reader_view_status::storage_error;
            fail(status);
            return;
        }
        if (state_.book.index_lookup) { state_.book.index_lookup = false; fail(reader_view_status::storage_error); }
        update_status_page();
        if (persistence_changed) { submit_frame(ui_update_reason::content_changed); }
        return;
    }
    if (event.content_ready && state_.book.metadata_known &&
        (event.file_size != state_.book.identity.file_size ||
         (state_.session.format == book_file_format::txt && event.modified_time != state_.book.identity.modified_time))) {
        state_.page.position_valid = false;
        state_.book.index_valid = false;
        fail(reader_view_status::storage_error);
        return;
    }
    if (event.type == book_event_type::opened) {
        state_.book.opened = true;
        if (!event.content_ready) { return; }
        state_.book.content_ready = true;
        if (!state_.book.metadata_known) {
            state_.book.identity.file_size = event.file_size;
            state_.book.identity.modified_time = event.modified_time;
            state_.book.metadata_known = true;
        }
        state_.cover.available = event.cover_available;
        if (state_.session.format == book_file_format::epub && !state_.navigation.user_navigated &&
            event.resume_at_cover && state_.cover.available) {
            state_.cover.offset = 0U;
            state_.cover.started = false;
            state_.cover.waiting = state_.content_request.busy = true;
            state_.page.status = reader_view_status::loading;
            state_.content_request.loading_started_ms = system_tick_now_ms();
            if (!request_cover()) {
                state_.cover.waiting = state_.cover.available = false;
                start_body(0U, page_operation::open);
            }
        } else if (!state_.navigation.user_navigated && event.progress.byte_offset > 0U && event.progress.byte_offset < event.file_size) {
            history_.clear();
            start_body(event.progress.byte_offset, page_operation::open);
        } else if (state_.session.format == book_file_format::epub && !state_.page.position_valid && !state_.content_request.busy) {
            start_body(event.progress.byte_offset, page_operation::open);
        }
    } else if (event.type == book_event_type::ready) {
        state_.book.opened = true;
        state_.book.index_valid = event.index_valid && event.page_count > 0U && event.progress.page < event.page_count;
        state_.page.total_count = state_.book.index_valid ? event.page_count : 0U;
        if (state_.book.index_valid && !state_.navigation.user_navigated && !state_.cover.showing && !state_.cover.waiting) {
            state_.book.indexed_target_valid = true;
            state_.book.indexed_target_page = event.progress.page;
            state_.book.indexed_target_offset = event.progress.byte_offset;
            if (state_.page.position_valid && !state_.content_request.busy && state_.page.current_offset == event.progress.byte_offset) {
                state_.page.current_number = event.progress.page;
                state_.book.index_position_valid = true;
                state_.book.indexed_target_valid = false;
                update_status_page();
            } else {
                history_.clear();
                start_page(event.progress.byte_offset, page_operation::open);
            }
        }
    } else if (event.type == book_event_type::position && state_.book.waiting && event.request_id == state_.book.request_id) {
        state_.book.waiting = false;
        const bool load = state_.book.index_lookup;
        state_.book.index_lookup = false;
        if (!event.index_valid || event.page_count != state_.page.total_count || event.progress.page >= state_.page.total_count) {
            state_.book.index_valid = state_.book.index_position_valid = false;
            if (load) { fail(reader_view_status::storage_error); }
            update_status_page();
            return;
        }
        if (load || (!state_.content_request.busy && state_.book.queried_offset == state_.page.current_offset &&
            (state_.page.status == reader_view_status::ready || state_.page.status == reader_view_status::empty_file))) {
            state_.book.indexed_target_page = event.progress.page;
            state_.book.indexed_target_offset = event.progress.byte_offset;
            state_.book.indexed_target_valid = true;
            if (load || state_.page.current_offset != event.progress.byte_offset) {
                if (!load) { history_.clear(); }
                start_page(event.progress.byte_offset, load ? state_.book.indexed_operation : page_operation::open);
            } else {
                state_.page.current_number = event.progress.page;
                state_.book.index_position_valid = true;
                state_.book.indexed_target_valid = false;
                update_status_page();
            }
        }
    }
    if (persistence_changed) { submit_frame(ui_update_reason::content_changed); }
}
