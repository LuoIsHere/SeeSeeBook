#pragma once

#include <cstdint>
#include <string>
#include "app_base.hpp"
#include "file_view.hpp"
#include "result_handle.hpp"
#include "storage_service.hpp"
#include "ui_renderer.hpp"

#define FILE_POPUP_DURATION_MS 3000U
#define FILE_PARENT_ENTRY_NAME ".."

class file_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_running() override;
    void on_close() override;

private:
    std::string current_path_ = "/";
    result_handle directory_handle_ = invalid_result_handle();
    file_view_status status_ = file_view_status::no_card;
    std::uint16_t page_index_ = 0U;
    std::uint32_t request_id_ = 0U;
    std::uint32_t session_id_ = 0U;
    std::uint32_t requested_generation_ = 0U;
    std::uint32_t popup_started_ms_ = 0U;
    bool popup_visible_ = false;

    void handle_action(const ui_action_event& action);
    void handle_storage_status(const app_storage_status_event& event);
    void handle_directory_result(const app_storage_result_event& event);
    void request_directory(const std::string& path);
    void activate_row(std::uint8_t row_index);
    void release_directory_result();
    void submit_frame(ui_update_reason reason);
    static bool write_frame(
        file_view_state& view,
        const void* context);
    void build_view(file_view_state& view) const;
    std::uint16_t page_count() const;
};
