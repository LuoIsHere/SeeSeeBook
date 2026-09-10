#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "books_file_name.hpp"
#include "books_interaction.hpp"
#include "books_layout.hpp"
#include "books_model.hpp"
#include "launcher_layout.hpp"
#include "menu_layout.hpp"

namespace {

#define CHECK_BOOKS(condition) do { if (!(condition)) { \
    std::printf("TEST_FAILURE books line=%d: %s\n", __LINE__, #condition); \
    std::fflush(stdout); std::abort(); \
} } while (0)

std::uint16_t test_glyph_width(std::uint32_t codepoint)
{
    return codepoint < 128U ? 1U : 2U;
}

void test_entry_configuration()
{
    CHECK_BOOKS(launcher_layout_entry_count == 3U);
    CHECK_BOOKS(launcher_entries[0].target == app_kind::books);
    CHECK_BOOKS(launcher_entries[1].target == app_kind::file);
    CHECK_BOOKS(launcher_entries[2].target == app_kind::menu);
    CHECK_BOOKS(std::strcmp(launcher_entries[0].label, "Books") == 0);
    CHECK_BOOKS(std::strcmp(launcher_entries[1].label, "File") == 0);
    CHECK_BOOKS(std::strcmp(launcher_entries[2].label, "Menu") == 0);
    CHECK_BOOKS(menu_layout_entry_count == 4U);
    CHECK_BOOKS(menu_entries[0].target == app_kind::test);
    CHECK_BOOKS(menu_entries[1].target == app_kind::rtc_setting);
    CHECK_BOOKS(menu_entries[2].target == app_kind::battery);
    CHECK_BOOKS(menu_entries[3].target == app_kind::gray4_test);
    CHECK_BOOKS(!menu_layout_contains(app_kind::launcher));
    CHECK_BOOKS(!menu_layout_contains(app_kind::books));
    CHECK_BOOKS(!menu_layout_contains(app_kind::file));
    CHECK_BOOKS(!menu_layout_contains(app_kind::reader));
}

void test_pagination_and_layout()
{
    CHECK_BOOKS(BOOKS_GRID_COLUMNS == 3U);
    CHECK_BOOKS(books_page_count(0U) == 1U);
    CHECK_BOOKS(books_page_count(1U) == 1U);
    CHECK_BOOKS(books_page_count(6U) == 1U);
    CHECK_BOOKS(books_page_count(7U) == 2U);
    CHECK_BOOKS(books_page_count(12U) == 2U);
    CHECK_BOOKS(books_page_first_item(1U) == 6U);
    CHECK_BOOKS(books_page_item_count(7U, 0U) == 6U);
    CHECK_BOOKS(books_page_item_count(7U, 1U) == 1U);
    CHECK_BOOKS(books_page_item_count(7U, 2U) == 0U);
    CHECK_BOOKS(BOOKS_COVER_WIDTH * 3 == BOOKS_COVER_HEIGHT * 2);
    for (std::uint8_t index = 0U; index < books_view_item_capacity; ++index) {
        const display_rect cell = books_item_rect(index);
        const display_rect cover = books_cover_rect(index);
        const display_rect name = books_file_name_rect(index);
        CHECK_BOOKS(cell.left >= 0 && cell.top >= BOOKS_TOOLBAR_HEIGHT);
        CHECK_BOOKS(cell.left + cell.width <= UI_DISPLAY_WIDTH);
        CHECK_BOOKS(cell.top + cell.height <= BOOKS_PAGER_TOP);
        CHECK_BOOKS(cover.left >= cell.left && cover.top >= cell.top);
        CHECK_BOOKS(cover.left + cover.width <= cell.left + cell.width);
        CHECK_BOOKS(name.top + name.height <= cell.top + cell.height);
    }
    CHECK_BOOKS(books_content_rect().top == BOOKS_TOOLBAR_HEIGHT);
    CHECK_BOOKS(books_content_rect().top + books_content_rect().height == STATUS_BAR_TOP);
    CHECK_BOOKS(books_previous_page_rect().top + books_previous_page_rect().height == STATUS_BAR_TOP);
    CHECK_BOOKS(books_next_page_rect().top + books_next_page_rect().height == STATUS_BAR_TOP);
    CHECK_BOOKS(books_modal_rect().top >= BOOKS_TOOLBAR_HEIGHT);
    CHECK_BOOKS(books_modal_rect().top + books_modal_rect().height < STATUS_BAR_TOP);
}

void test_settings_model()
{
    books_settings_model settings;
    CHECK_BOOKS(settings.saved().auto_scan_txt);
    CHECK_BOOKS(!settings.saved().auto_scan_epub);
    CHECK_BOOKS(settings.load_saved({false, true}));
    CHECK_BOOKS(!settings.saved().auto_scan_txt);
    CHECK_BOOKS(settings.saved().auto_scan_epub);
    settings.open();
    CHECK_BOOKS(!settings.load_saved({true, false}));
    settings.cancel();
    for (bool txt : {false, true}) {
        for (bool epub : {false, true}) {
            settings.open();
            if (settings.pending().auto_scan_txt != txt) {
                settings.toggle_txt();
            }
            if (settings.pending().auto_scan_epub != epub) {
                settings.toggle_epub();
            }
            settings.confirm();
            CHECK_BOOKS(!settings.visible());
            CHECK_BOOKS(settings.saved().auto_scan_txt == txt);
            CHECK_BOOKS(settings.saved().auto_scan_epub == epub);
            settings.open();
            settings.toggle_txt();
            settings.toggle_epub();
            settings.cancel();
            CHECK_BOOKS(settings.saved().auto_scan_txt == txt);
            CHECK_BOOKS(settings.saved().auto_scan_epub == epub);
            settings.open();
            CHECK_BOOKS(settings.pending().auto_scan_txt == txt);
            CHECK_BOOKS(settings.pending().auto_scan_epub == epub);
            settings.cancel();
        }
    }
}

void test_modal_capture_and_filename()
{
    books_view_state state = {};
    state.item_count = 1U;
    state.page_count = 2U;
    state.items[0].occupied = true;
    state.items[0].enabled = true;
    ui_control_type control = ui_control_type::none;
    std::uint8_t index = 0U;
    const display_rect item = books_item_rect(0U);
    CHECK_BOOKS(books_hit_test(
        state, item.left + 2, item.top + 2, control, index));
    CHECK_BOOKS(control == ui_control_type::books_select_item && index == 0U);
    state.settings_visible = true;
    CHECK_BOOKS(!books_hit_test(
        state, item.left + 2, item.top + 2, control, index));
    const display_rect txt = books_setting_row_rect(false);
    CHECK_BOOKS(books_hit_test(
        state, txt.left + 2, txt.top + 2, control, index));
    CHECK_BOOKS(control == ui_control_type::books_setting_toggle_txt);
    const display_rect confirm = books_setting_confirm_rect();
    CHECK_BOOKS(books_hit_test(
        state, confirm.left + 2, confirm.top + 2, control, index));
    CHECK_BOOKS(control == ui_control_type::books_setting_confirm);

    books_file_name_view_state name = {};
    const text_layout_profile layout = {8U, 2U, test_glyph_width};
    format_books_file_name("ABCDEFGHIJK.txt", name, layout);
    CHECK_BOOKS(name.line_count == 2U);
    CHECK_BOOKS(std::strcmp(name.lines[0], "ABCDEFGH") == 0);
    CHECK_BOOKS(std::strcmp(name.lines[1], "IJK.txt") == 0);
    format_books_file_name("中文书名.epub", name, layout);
    CHECK_BOOKS(name.line_count == 2U);
    const std::string_view second(name.lines[1]);
    CHECK_BOOKS(second.size() >= 5U &&
                second.substr(second.size() - 5U) == ".epub");
    format_books_file_name(std::string_view("\xff", 1U), name, layout);
    CHECK_BOOKS(name.line_count > 0U);
}

}  // namespace

void test_books_ui_support()
{
    test_entry_configuration();
    test_pagination_and_layout();
    test_settings_model();
    test_modal_capture_and_filename();
    std::puts("PASS Books UI: launcher/menu descriptors, 3-column layout, pagination, settings transaction, modal capture, filenames");
}
