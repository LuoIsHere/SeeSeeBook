#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "books_file_name.hpp"
#include "book_catalog_support.hpp"
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
    CHECK_BOOKS(books_page_count(0U) == 0U);
    CHECK_BOOKS(books_page_count(1U) == 1U);
    CHECK_BOOKS(books_page_count(6U) == 1U);
    CHECK_BOOKS(books_page_count(7U) == 2U);
    CHECK_BOOKS(books_page_count(12U) == 2U);
    CHECK_BOOKS(books_page_first_item(1U) == 6U);
    CHECK_BOOKS(books_page_item_count(7U, 0U) == 6U);
    CHECK_BOOKS(books_page_item_count(7U, 1U) == 1U);
    CHECK_BOOKS(books_page_item_count(7U, 2U) == 0U);
    CHECK_BOOKS(books_clamp_page(13U, 2U) == 2U);
    CHECK_BOOKS(books_clamp_page(7U, 2U) == 1U);
    CHECK_BOOKS(books_clamp_page(1U, 2U) == 0U);
    CHECK_BOOKS(books_clamp_page(0U, 2U) == 0U);
    CHECK_BOOKS(BOOKS_COVER_WIDTH * 3 == BOOKS_COVER_HEIGHT * 2);
    for (std::uint8_t index = 0U; index < books_view_item_capacity; ++index) {
        const display_rect slot = books_item_slot_rect(index);
        const display_rect card = books_item_card_rect(index);
        const display_rect hit = books_item_hit_rect(index);
        const display_rect redraw = books_item_redraw_rect(index);
        const display_rect cover = books_cover_rect(index);
        const display_rect name = books_file_name_rect(index);
        CHECK_BOOKS(slot.left >= 0 && slot.top >= BOOKS_TOOLBAR_HEIGHT);
        CHECK_BOOKS(slot.left + slot.width <= UI_DISPLAY_WIDTH);
        CHECK_BOOKS(slot.top + slot.height <= BOOKS_PAGER_TOP);
        CHECK_BOOKS(card.height == BOOKS_CARD_HEIGHT);
        CHECK_BOOKS(slot.height == BOOKS_CARD_HEIGHT + BOOKS_GRID_ROW_GAP);
        CHECK_BOOKS(redraw.left == card.left && redraw.top == card.top &&
                    redraw.width == card.width && redraw.height == card.height);
        CHECK_BOOKS(hit.left <= card.left && hit.top <= card.top);
        CHECK_BOOKS(hit.left + hit.width >= card.left + card.width);
        CHECK_BOOKS(hit.top + hit.height >= card.top + card.height);
        CHECK_BOOKS(cover.left >= card.left && cover.top >= card.top);
        CHECK_BOOKS(cover.left + cover.width <= card.left + card.width);
        CHECK_BOOKS(cover.top - card.top == BOOKS_CARD_TOP_PADDING);
        CHECK_BOOKS(name.top == cover.top + cover.height + BOOKS_FILE_NAME_TOP_GAP);
        CHECK_BOOKS(card.top + card.height - (name.top + name.height) ==
                    BOOKS_CARD_BOTTOM_PADDING);
    }
    CHECK_BOOKS(
        books_item_hit_rect(0U).top + books_item_hit_rect(0U).height <
        books_item_hit_rect(3U).top);
    CHECK_BOOKS(books_content_rect().top == BOOKS_TOOLBAR_HEIGHT);
    CHECK_BOOKS(books_content_rect().top + books_content_rect().height == STATUS_BAR_TOP);
    CHECK_BOOKS(books_previous_page_rect().top + books_previous_page_rect().height == STATUS_BAR_TOP);
    CHECK_BOOKS(books_next_page_rect().top + books_next_page_rect().height == STATUS_BAR_TOP);
    CHECK_BOOKS(books_modal_rect().top >= BOOKS_TOOLBAR_HEIGHT);
    CHECK_BOOKS(books_modal_rect().top + books_modal_rect().height < STATUS_BAR_TOP);
    CHECK_BOOKS(BOOKS_MODAL_TEXT_SIZE >= 2U);
    CHECK_BOOKS(BOOKS_MODAL_TITLE_TEXT_SIZE >= 3U);
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
    format_books_file_name("/nested/ABCDEFGHIJK.txt", name, layout);
    CHECK_BOOKS(name.line_count == 2U);
    CHECK_BOOKS(std::strcmp(name.lines[0], "ABCDEFGH") == 0);
    CHECK_BOOKS(std::strcmp(name.lines[1], "IJK") == 0);
    format_books_file_name("中文书名.epub", name, layout);
    CHECK_BOOKS(name.line_count == 1U);
    CHECK_BOOKS(std::strcmp(name.lines[0], "中文书名") == 0);
    format_books_file_name(std::string_view("\xff", 1U), name, layout);
    CHECK_BOOKS(name.line_count > 0U);

    books_item_view_state preview = {};
    const text_layout_profile preview_layout = {6U, 3U, test_glyph_width};
    format_books_preview("one two three four five", preview, preview_layout);
    CHECK_BOOKS(preview.preview_line_count == 3U);
    CHECK_BOOKS(std::strlen(preview.preview[2]) < books_preview_line_capacity);
}

void test_catalog_algorithms()
{
    book_file_format format = book_file_format::unknown;
    for (bool txt : {false, true}) {
        for (bool epub : {false, true}) {
            const book_catalog_settings settings = {txt, epub};
            CHECK_BOOKS(book_catalog_path_selected("/nested/BOOK.TXT", settings, format) == txt);
            CHECK_BOOKS(format == book_file_format::txt);
            CHECK_BOOKS(book_catalog_path_selected("/nested/book.EpUb", settings, format) == epub);
            CHECK_BOOKS(format == book_file_format::epub);
        }
    }
    CHECK_BOOKS(book_catalog_path_excluded("/.system/books/cache.epub"));
    CHECK_BOOKS(book_catalog_path_excluded("/nested/.cache/book.txt"));
    CHECK_BOOKS(!book_catalog_path_excluded("/nested/book.txt"));
    CHECK_BOOKS(!book_catalog_path_selected("/nested/image.png", {true, true}, format));

    char joined[BOOK_PATH_CAPACITY] = {};
    CHECK_BOOKS(book_catalog_join_path("/", "nested", joined, sizeof(joined)) &&
                std::strcmp(joined, "/nested") == 0);
    CHECK_BOOKS(book_catalog_join_path("/nested", "book.txt", joined, sizeof(joined)) &&
                std::strcmp(joined, "/nested/book.txt") == 0);
    CHECK_BOOKS(!book_catalog_join_path("/nested", "../book.txt", joined, sizeof(joined)));

    book_catalog_item items[5] = {};
    std::strcpy(items[0].path, "/z.epub");
    std::strcpy(items[1].path, "/A.txt");
    std::strcpy(items[2].path, "/nested/b.TXT");
    std::strcpy(items[3].path, "/a.TXT");
    std::strcpy(items[4].path, "/m.txt");
    std::size_t count = 5U;
    book_catalog_sort_unique(items, count);
    CHECK_BOOKS(count == 4U);
    CHECK_BOOKS(std::strcmp(items[0].path, "/A.txt") == 0);
    CHECK_BOOKS(std::strcmp(items[3].path, "/z.epub") == 0);

    char preview[BOOK_CATALOG_PREVIEW_CAPACITY] = {};
    book_catalog_preview_state state = book_catalog_preview_state::none;
    const std::string bom = "\xef\xbb\xbf中文\r\nline";
    CHECK_BOOKS(book_catalog_make_txt_preview(
        reinterpret_cast<const std::uint8_t*>(bom.data()), bom.size(), true,
        preview, sizeof(preview), state));
    CHECK_BOOKS(state == book_catalog_preview_state::ready);
    CHECK_BOOKS(std::strcmp(preview, "中文 line") == 0);
    const std::uint8_t invalid[] = {0xe4U, 0x20U};
    CHECK_BOOKS(book_catalog_make_txt_preview(
        invalid, sizeof(invalid), true, preview, sizeof(preview), state));
    CHECK_BOOKS(state == book_catalog_preview_state::invalid_utf8);
    std::string large(BOOK_CATALOG_TXT_READ_LIMIT, 'x');
    CHECK_BOOKS(book_catalog_make_txt_preview(
        reinterpret_cast<const std::uint8_t*>(large.data()), large.size(), false,
        preview, sizeof(preview), state));
    CHECK_BOOKS(std::strlen(preview) < sizeof(preview));
}

}  // namespace

void test_books_ui_support()
{
    test_entry_configuration();
    test_pagination_and_layout();
    test_settings_model();
    test_modal_capture_and_filename();
    test_catalog_algorithms();
    std::puts("PASS Books/catalog: scan policy, nested paths, exclusions, settings, sorting, preview, pagination, modal, filenames");
}
