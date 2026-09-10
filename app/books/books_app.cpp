#include "books_app.hpp"

#include <esp_log.h>

#include "app.hpp"

namespace {
constexpr char log_tag[] = "app_books";
}

void books_app::handle_app_event(const app_event& event)
{
    if (event.type == app_event_type::ui_action) {
        handle_action(event.action);
    }
}

void books_app::on_open()
{
    settings_.cancel();
    page_index_ = 0U;
    page_count_ = 1U;
    update_status_bar();
    submit_frame(ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "BooksApp opened with empty catalog placeholder");
}

void books_app::on_close()
{
    settings_.cancel();
    page_index_ = 0U;
    page_count_ = 1U;
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
                submit_frame(ui_update_reason::popup_changed);
                break;
            case ui_control_type::books_setting_cancel:
                settings_.cancel();
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
                update_status_bar();
                submit_frame(ui_update_reason::content_changed);
            }
            break;
        case ui_control_type::books_page_next:
            if (page_index_ + 1U < page_count_) {
                ++page_index_;
                update_status_bar();
                submit_frame(ui_update_reason::content_changed);
            }
            break;
        case ui_control_type::books_select_item:
            // Empty placeholders are disabled. Catalog results will provide
            // enabled item rows and Reader launch data without changing UI.
            break;
        default:
            break;
    }
}

books_view_state books_app::build_view() const
{
    books_view_state view = {};
    view.page_index = page_index_;
    view.page_count = page_count_;
    view.settings_visible = settings_.visible();
    view.pending_settings.auto_scan_txt = settings_.pending().auto_scan_txt;
    view.pending_settings.auto_scan_epub = settings_.pending().auto_scan_epub;
    return view;
}

void books_app::submit_frame(
    ui_update_reason reason,
    ui_control_type changed_control)
{
    ui_render_books(build_view(), reason, changed_control);
}

void books_app::update_status_bar() const
{
    if (ui_status_bar_set_page_status(
            true,
            static_cast<std::uint32_t>(page_index_) + 1U,
            page_count_)) {
        ui_renderer_notify_status_bar();
    }
}
