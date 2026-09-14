#pragma once

#include "app_base.hpp"
#include "text_paginator.hpp"
#include "book_service.hpp"
#include "reader_launch.hpp"
#include "storage_service.hpp"
#include "ui_renderer.hpp"
#include "reader_cover.hpp"

class reader_app final : public app_base {
public:
    bool prepare_launch(const app_launch_context& context) override;
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_running() override;
    void on_close() override;

private:
    enum class page_operation : std::uint8_t { open, next, previous, rebuild_previous };

    struct session_state {
        char path[STORAGE_MAX_PATH_LENGTH + 1U] = {};
        std::uint32_t id = 0U;
        std::uint32_t media_generation = 0U;
        book_file_format format = book_file_format::unknown;
        bool prepared = false;
        bool active = false;
        bool media_valid = false;
    };

    struct page_state {
        std::uint64_t current_offset = 0U;
        std::uint64_t next_offset = 0U;
        std::uint64_t rebuild_target = 0U;
        std::uint32_t current_number = 0U;
        std::uint32_t total_count = 0U;
        reader_view_status status = reader_view_status::loading;
        page_operation operation = page_operation::open;
        bool position_valid = false;
        bool end_of_file = false;
    };

    struct content_request_state {
        std::uint64_t requested_offset = 0U;
        std::uint32_t id = 0U;
        std::uint32_t started_ms = 0U;
        std::uint32_t loading_started_ms = 0U;
        bool busy = false;
        bool waiting = false;
        bool loading_shown = false;
    };

    struct book_state {
        book_file_identity identity = {};
        std::uint64_t indexed_target_offset = 0U;
        std::uint64_t queried_offset = 0U;
        std::uint32_t request_id = 0U;
        std::uint32_t request_started_ms = 0U;
        std::uint32_t indexed_target_page = 0U;
        page_operation indexed_operation = page_operation::open;
        bool metadata_known = false;
        bool opened = false;
        bool submitted = false;
        bool progress_persistent = false;
        bool index_valid = false;
        bool index_position_valid = false;
        bool waiting = false;
        bool index_lookup = false;
        bool indexed_target_valid = false;
        bool content_ready = false;
    };

    struct cover_state {
        std::uint64_t offset = 0U;
        std::uint32_t generation = 0U;
        bool available = false;
        bool showing = false;
        bool waiting = false;
        bool started = false;
    };

    struct navigation_state {
        bool menu_visible = false;
        bool user_navigated = false;
    };

    struct presentation_state {
        ui_update_reason pending_reason = ui_update_reason::view_opened;
        bool frame_pending = false;
        bool opening = true;
    };

    struct reader_runtime_state {
        session_state session;
        page_state page;
        content_request_state content_request;
        book_state book;
        cover_state cover;
        navigation_state navigation;
        presentation_state presentation;
    };

    reader_runtime_state state_;
    std::uint32_t session_serial_ = 0U;
    reader_paginator paginator_;
    reader_page_history history_;
    text_layout_profile layout_ = {};
    // Stable body snapshot: a menu toggle must not expose an in-flight paginator.
    reader_view_state view_ = {};

    void handle_action(const ui_action_event& action);
    void handle_result(const result_handle& handle);
    void handle_book_result(const result_handle& handle);
    void handle_book_event(const book_service_event& event);
    bool query_index(bool by_page, std::uint32_t page = 0U);
    void update_status_page(bool notify = true);
    bool check_media();
    void start_page(std::uint64_t offset, page_operation operation);
    void start_indexed_page(
        std::uint64_t offset,
        page_operation operation,
        std::uint32_t page);
    void request_chunk();
    bool request_cover();
    void start_body(std::uint64_t offset, page_operation operation);
    void complete_page();
    void fail(reader_view_status status);
    void submit_frame(ui_update_reason reason);
    static bool write_frame(reader_view_state& view, const void* context);
};
