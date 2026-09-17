#pragma once

#include <cstdint>

#include "app_base.hpp"
#include "book_catalog.hpp"
#include "books_model.hpp"
#include "books_view.hpp"
#include "storage_service.hpp"
#include "selection_controller.hpp"
#include "ui_renderer.hpp"

class books_app final : public app_base {
public:
    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_running() override;
    void on_close() override;

private:
    books_settings_model settings_;
    book_catalog_item page_items_[BOOK_CATALOG_PAGE_CAPACITY] = {};
    std::uint32_t cover_generations_[BOOK_CATALOG_PAGE_CAPACITY] = {};
    std::uint32_t media_generation_ = 0U;
    std::uint32_t catalog_revision_ = 0U;
    std::uint32_t cover_session_ = 0U;
    std::uint32_t next_cover_request_ = 0U;
    std::uint32_t cover_refresh_deadline_ms_ = 0U;
    std::uint16_t total_items_ = 0U;
    std::uint16_t page_index_ = 0U;
    std::uint16_t page_count_ = 0U;
    std::uint8_t page_item_count_ = 0U;
    book_catalog_state catalog_state_ = book_catalog_state::unavailable;
    bool cover_refresh_pending_ = false;
    bool catalog_dirty_ = false;
    bool reader_launch_pending_ = false;
    char reader_path_[BOOK_PATH_CAPACITY] = {};
    book_file_format reader_format_ = book_file_format::unknown;
    selection_controller selection_;

    struct cover_request_state {
        char path[STORAGE_MAX_PATH_LENGTH + 1U] = {};
        std::uint64_t offset = 0U;
        std::uint32_t request_id = 0U;
        std::uint32_t expected_size = 0U;
        std::uint8_t slot = 0U;
        bool busy = false;
        bool started = false;
    } cover_request_;

    void handle_action(const ui_action_event& action);
    void handle_navigation(navigation_action action);
    void handle_storage_status(const app_storage_status_event& event);
    void handle_storage_result(const result_handle& handle);
    void refresh_catalog(bool render);
    void load_page(bool reset_covers);
    void start_next_cover();
    bool request_cover_chunk();
    void cancel_cover_request();
    void request_reader(std::uint8_t index);
    void try_launch_reader();
    books_view_state build_view() const;
    void submit_frame(
        ui_update_reason reason,
        ui_control_type changed_control = ui_control_type::none);
    void update_status_bar() const;
};
