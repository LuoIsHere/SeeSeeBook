#include "books_model.hpp"

bool books_settings_model::load_saved(const books_scan_settings& value)
{
    if (visible_) {
        return false;
    }
    saved_ = value;
    pending_ = value;
    return true;
}

void books_settings_model::open()
{
    pending_ = saved_;
    visible_ = true;
}

void books_settings_model::toggle_txt()
{
    if (visible_) {
        pending_.auto_scan_txt = !pending_.auto_scan_txt;
    }
}

void books_settings_model::toggle_epub()
{
    if (visible_) {
        pending_.auto_scan_epub = !pending_.auto_scan_epub;
    }
}

void books_settings_model::confirm()
{
    if (visible_) {
        saved_ = pending_;
        visible_ = false;
    }
}

void books_settings_model::cancel()
{
    pending_ = saved_;
    visible_ = false;
}
