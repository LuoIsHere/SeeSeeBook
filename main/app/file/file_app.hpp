#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_back_button.hpp"
#include "app_base.hpp"
#include "hal_display.hpp"
#include "storage_service.hpp"

#define FILE_POPUP_DURATION_MS 3000U
#define FILE_PARENT_ENTRY_NAME ".."

class file_app final : public app_base {
public:
    file_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_running() override;
    void on_close() override;

private:
    enum class active_control : std::uint8_t {
        none,
        row,
        previous_page,
        next_page,
    };

    app_back_button back_button_;
    std::string current_path_ = "/";
    std::vector<file_entry> entries_;
    file_view_status status_ = file_view_status::no_card;
    active_control active_control_ = active_control::none;
    std::uint8_t active_index_ = 0U;
    std::uint16_t page_index_ = 0U;
    std::uint32_t request_id_ = 0U;
    std::uint32_t session_id_ = 0U;
    std::uint32_t requested_generation_ = 0U;
    std::uint32_t popup_started_ms_ = 0U;
    bool popup_visible_ = false;

    void handle_touch(const touch_event& event);
    void handle_storage_status(const sd_status_event& event);
    void handle_directory_result(const storage_event& event);
    void request_directory(const std::string& path);
    void activate_row(std::uint8_t row_index);
    void submit_frame(refresh_mode mode, display_update_region update_region);
    void submit_control_feedback(
        display_control_type type,
        std::uint8_t button_index,
        const ui_rect& rect,
        bool pressed);
    void build_view(file_view_state& view) const;
    std::uint16_t page_count() const;
};
