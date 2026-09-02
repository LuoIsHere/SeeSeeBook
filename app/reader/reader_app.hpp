#pragma once

#include "app_base.hpp"
#include "reader_paginator.hpp"
#include "reading_progress_service.hpp"
#include "storage_service.hpp"
#include "ui_renderer.hpp"

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

    char path_[STORAGE_MAX_PATH_LENGTH + 1U] = {};
    reading_file_identity identity_ = {};
    reader_paginator paginator_;
    reader_page_history history_;
    text_layout_profile layout_ = {};
    std::uint64_t current_offset_ = 0U;
    std::uint64_t next_offset_ = 0U;
    std::uint64_t requested_offset_ = 0U;
    std::uint64_t rebuild_target_ = 0U;
    std::uint32_t session_id_ = 0U;
    std::uint32_t request_id_ = 0U;
    std::uint32_t media_generation_ = 0U;
    std::uint32_t request_started_ms_ = 0U;
    std::uint32_t loading_started_ms_ = 0U;
    reader_view_status status_ = reader_view_status::loading;
    page_operation operation_ = page_operation::open;
    ui_update_reason pending_reason_ = ui_update_reason::view_opened;
    bool prepared_ = false;
    bool active_ = false;
    bool media_valid_ = false;
    bool metadata_known_ = false;
    bool position_valid_ = false;
    bool end_of_file_ = false;
    bool busy_ = false;
    bool waiting_ = false;
    bool loading_shown_ = false;
    bool frame_pending_ = false;

    void handle_action(const ui_action_event& action);
    void handle_result(const result_handle& handle);
    bool check_media();
    void start_page(std::uint64_t offset, page_operation operation);
    void request_chunk();
    void complete_page();
    void fail(reader_view_status status);
    void submit_frame(ui_update_reason reason);
    static bool write_frame(reader_view_state& view, const void* context);
};
