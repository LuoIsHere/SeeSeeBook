// Exercise the production renderer policy and Reader drawing with a RAM surface.
// Rename the exported entry points to coexist with the App harness's frame adapter.
#define ui_renderer_init tested_ui_renderer_init
#define ui_render_launcher tested_ui_render_launcher
#define ui_render_books tested_ui_render_books
#define ui_render_menu tested_ui_render_menu
#define ui_render_test tested_ui_render_test
#define ui_render_rtc tested_ui_render_rtc
#define ui_render_battery tested_ui_render_battery
#define ui_render_gray4_test tested_ui_render_gray4_test
#define ui_write_file_frame tested_ui_write_file_frame
#define ui_write_reader_frame tested_ui_write_reader_frame
#define ui_render_control tested_ui_render_control
#define ui_renderer_notify_status_bar tested_ui_renderer_notify_status_bar
#include "../../../../ui/paper_mono/ui_renderer.cpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "books_cover.hpp"
#include "reader_cover.hpp"

#define CHECK_UI(condition) do { if (!(condition)) { \
    std::printf("TEST_FAILURE renderer line=%d: %s\n", __LINE__, #condition); std::fflush(stdout); \
    abort(); } } while (0)

namespace {
std::array<unsigned char, UI_DISPLAY_WIDTH * UI_DISPLAY_HEIGHT / 8> pixels{};
display_surface surface;
display_text_alignment alignment = display_text_alignment::middle_left;
display_font font = display_font::default_font;
display_color text_foreground = display_color::black;
display_color text_background = display_color::white;
unsigned text_scale = 1U;
unsigned reader_lines = 0U;
unsigned image_draws = 0U;
display_rect last_image_rect = {};
display_image_mode last_image_mode = display_image_mode::gray4;
int last_line_bottom = 0;
struct label { std::string text; int x, y; display_text_alignment align; };
std::vector<label> labels;
struct cjk_draw { int x, y; std::size_t length; };
std::vector<cjk_draw> cjk_draws;

void paint(int x, int y, int width, int height, display_color color)
{
    CHECK_UI(x >= 0 && y >= 0 && width >= 0 && height >= 0);
    CHECK_UI(x + width <= UI_DISPLAY_WIDTH && y + height <= UI_DISPLAY_HEIGHT);
    for (int row = y; row < y + height; ++row) {
        for (int column = x; column < x + width; ++column) {
            const auto index = row * UI_DISPLAY_WIDTH + column;
            const auto mask = 1U << (index % 8);
            if (color == display_color::black) { pixels[index / 8] |= mask; }
            else { pixels[index / 8] &= ~mask; }
        }
    }
}

bool round_contains(
    const display_rect& rect,
    int radius,
    int x,
    int y)
{
    if (rect.width <= 0 || rect.height <= 0 ||
        x < rect.left || x >= rect.left + rect.width ||
        y < rect.top || y >= rect.top + rect.height) {
        return false;
    }
    radius = std::max(0, std::min(radius, std::min(rect.width / 2, rect.height / 2)));
    if (radius == 0) {
        return true;
    }
    const int local_x = x - rect.left;
    const int local_y = y - rect.top;
    if (local_x >= radius && local_x < rect.width - radius) {
        return true;
    }
    if (local_y >= radius && local_y < rect.height - radius) {
        return true;
    }
    const int center_x = local_x < radius ? radius - 1 : rect.width - radius;
    const int center_y = local_y < radius ? radius - 1 : rect.height - radius;
    const int dx = local_x - center_x;
    const int dy = local_y - center_y;
    return dx * dx + dy * dy <= (radius - 1) * (radius - 1);
}

void paint_round(
    const display_rect& rect,
    int radius,
    display_color color,
    bool filled)
{
    const display_rect inner = {
        static_cast<std::int16_t>(rect.left + 1),
        static_cast<std::int16_t>(rect.top + 1),
        static_cast<std::int16_t>(std::max(0, rect.width - 2)),
        static_cast<std::int16_t>(std::max(0, rect.height - 2)),
    };
    for (int y = rect.top; y < rect.top + rect.height; ++y) {
        for (int x = rect.left; x < rect.left + rect.width; ++x) {
            if (round_contains(rect, radius, x, y) &&
                (filled || !round_contains(inner, std::max(0, radius - 1), x, y))) {
                paint(x, y, 1, 1, color);
            }
        }
    }
}
}

std::int16_t display_surface::width() const { return UI_DISPLAY_WIDTH; }
std::int16_t display_surface::height() const { return UI_DISPLAY_HEIGHT; }
void display_surface::fill_screen(display_color c) { paint(0, 0, width(), height(), c); }
void display_surface::fill_rect(const display_rect& r, display_color c) { paint(r.left, r.top, r.width, r.height, c); }
void display_surface::fill_rect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, display_color c) { paint(x, y, w, h, c); }
void display_surface::draw_rect(const display_rect& r, display_color c)
{
    if (r.width <= 0 || r.height <= 0) { return; }
    paint(r.left, r.top, r.width, 1, c);
    paint(r.left, r.top + r.height - 1, r.width, 1, c);
    paint(r.left, r.top, 1, r.height, c);
    paint(r.left + r.width - 1, r.top, 1, r.height, c);
}
void display_surface::draw_rect(std::int16_t x, std::int16_t y, std::int16_t w,
                                std::int16_t h, display_color c)
{ draw_rect({x, y, w, h}, c); }
void display_surface::fill_round_rect(
    const display_rect& r, std::int16_t radius, display_color c)
{ paint_round(r, radius, c, true); }
void display_surface::draw_round_rect(
    const display_rect& r, std::int16_t radius, display_color c)
{ paint_round(r, radius, c, false); }
void display_surface::draw_horizontal_line(std::int16_t x, std::int16_t y, std::int16_t w, display_color c) { paint(x, y, w, 1, c); }
void display_surface::draw_line(std::int16_t x0, std::int16_t y0, std::int16_t x1,
                                std::int16_t y1, display_color c)
{
    int x = x0;
    int y = y0;
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        paint(x, y, 1, 1, c);
        if (x == x1 && y == y1) { break; }
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x += sx; }
        if (doubled <= dx) { error += dx; y += sy; }
    }
}
void display_surface::fill_triangle(std::int16_t, std::int16_t, std::int16_t, std::int16_t, std::int16_t, std::int16_t, display_color) {}
void display_surface::set_font(display_font f) { font = f; }
void display_surface::set_text_color(display_color foreground, display_color background)
{ text_foreground = foreground; text_background = background; }
void display_surface::set_text_alignment(display_text_alignment a) { alignment = a; }
void display_surface::set_text_size(std::uint8_t s) { text_scale = s; }
void display_surface::draw_text(const char* text, std::int16_t x, std::int16_t y)
{
    labels.push_back({text, x, y, alignment});
    const int w = std::strlen(text) * 6 * text_scale;
    const int h = 8 * text_scale;
    const int left = alignment == display_text_alignment::middle_left ? x :
                     alignment == display_text_alignment::middle_center ? x - w / 2 : x - w;
    paint(left, y - h / 2, w, h, text_background);
    for (int cursor = left; cursor < left + w; cursor += 6 * text_scale) {
        paint(cursor, y - h / 2, std::max(1U, text_scale), h, text_foreground);
        paint(cursor, y - h / 2, std::min<int>(4 * text_scale, left + w - cursor),
              std::max(1U, text_scale), text_foreground);
        paint(cursor, y + h / 2 - text_scale,
              std::min<int>(4 * text_scale, left + w - cursor),
              std::max(1U, text_scale), text_foreground);
    }
}
std::int32_t display_surface::text_width(const char* text) const
{ return static_cast<std::int32_t>(std::strlen(text) * 6U * text_scale); }
bool display_surface::draw_image(const std::uint8_t*, std::size_t,
                                 book_cover_encoding, const display_rect& rect,
                                 display_image_mode mode)
{
    ++image_draws;
    last_image_rect = rect;
    last_image_mode = mode;
    for (int y = rect.top + 1; y < rect.top + rect.height - 1; y += 6) {
        for (int x = rect.left + 1; x < rect.left + rect.width - 1; x += 6) {
            if (((x + y) / 6) % 2 == 0) {
                paint(x, y, std::min(4, rect.left + rect.width - 1 - x),
                      std::min(4, rect.top + rect.height - 1 - y),
                      display_color::black);
            }
        }
    }
    return true;
}
bool display_surface::has_intermediate_gray() const { return false; }
display_surface& hal_display_surface() { return surface; }
bool hal_display_init() { return true; }
bool hal_display_sleep() { return true; }
display_refresh_result hal_display_refresh(const display_rect&, refresh_mode mode)
{ return {true, mode, 0U, display_refresh_error::none}; }

// The font rasterizer is the board adapter here; production Reader line placement
// and overlay drawing run unchanged. No claims about physical glyph rendering.
void paper_mono_draw_cjk_text(
    display_surface&,
    const char* text,
    std::size_t length,
    std::int16_t x,
    std::int16_t y)
{
    ++reader_lines;
    cjk_draws.push_back({x, y, length});
    CHECK_UI(y - 12 >= reader_text_rect().top);
    last_line_bottom = y + 12;
    CHECK_UI(last_line_bottom <= reader_text_rect().top + reader_text_rect().height);
    int cursor = x;
    for (std::size_t offset = 0U; offset < length;) {
        const auto lead = static_cast<unsigned char>(text[offset]);
        std::size_t bytes = 1U;
        if ((lead & 0xf0U) == 0xe0U) { bytes = 3U; }
        else if ((lead & 0xe0U) == 0xc0U) { bytes = 2U; }
        else if ((lead & 0xf8U) == 0xf0U) { bytes = 4U; }
        const int glyph_width = bytes == 1U ? 7 : 13;
        if (cursor + glyph_width > UI_DISPLAY_WIDTH) { break; }
        paint(cursor, y - 12, glyph_width, 24, text_background);
        paint(cursor, y - 12, 2, 24, text_foreground);
        paint(cursor, y - 12, glyph_width - 2, 2, text_foreground);
        paint(cursor, y + 10, glyph_width - 2, 2, text_foreground);
        cursor += glyph_width;
        offset += std::min(bytes, length - offset);
    }
}

namespace paper_mono_views {
void draw_gray4_test_view(display_surface&, const gray4_test_view_state&) {}
}

namespace {

bool pixel_is_black(int x, int y)
{
    const std::size_t index = static_cast<std::size_t>(y) * UI_DISPLAY_WIDTH + x;
    return (pixels[index / 8U] & (1U << (index % 8U))) != 0U;
}

void emit_preview(const char* name)
{
    std::printf("UI_PREVIEW_BEGIN %s\n", name);
    for (int y = 0; y < UI_DISPLAY_HEIGHT; ++y) {
        const std::size_t offset = static_cast<std::size_t>(y) * UI_DISPLAY_WIDTH / 8U;
        for (int x = 0; x < UI_DISPLAY_WIDTH / 8; ++x) {
            std::printf("%02x", pixels[offset + x]);
        }
        std::putchar('\n');
    }
    std::printf("UI_PREVIEW_END %s\n", name);
    std::fflush(stdout);
}

books_view_state preview_books_state()
{
    books_view_state state = {};
    state.item_count = books_view_item_capacity;
    state.page_index = 0U;
    state.page_count = 2U;
    state.selected_index = 1U;
    const char* lines[][2] = {
        {"Quiet Reading", ""},
        {"A Very Long", "Book Title"},
        {"\xe4\xb8\xad\xe6\x96\x87\xe4\xb9\xa6\xe5\x90\x8d", ""},
        {"No Cover", ""},
        {"Field Notes", "Volume Two"},
        {"Last Book", ""},
    };
    for (std::uint8_t index = 0U; index < books_view_item_capacity; ++index) {
        auto& item = state.items[index];
        item.occupied = true;
        item.enabled = true;
        item.format = index % 2U == 0U ? book_file_format::txt
                                       : book_file_format::epub;
        item.file_name.line_count = lines[index][1][0] == '\0' ? 1U : 2U;
        std::strcpy(item.file_name.lines[0], lines[index][0]);
        std::strcpy(item.file_name.lines[1], lines[index][1]);
        if (item.format == book_file_format::txt) {
            item.preview_line_count = 3U;
            std::strcpy(item.preview[0], "A quiet page");
            std::strcpy(item.preview[1], "for reading");
            std::strcpy(item.preview[2], "without noise");
        }
    }
    state.pending_settings = {true, false};
    return state;
}

void draw_preview_status(ui_view_id view, const char* text)
{
    status_bar_view_state status = {};
    status.foreground_app = view;
    status.time_valid = true;
    status.hour = 10U;
    status.minute = 24U;
    status.battery.level_valid = true;
    status.battery.percent = 76U;
    status.center_kind = status_bar_center_kind::text;
    std::strncpy(status.center_text, text, sizeof(status.center_text) - 1U);
    draw_status_bar(status);
}

void render_visual_previews()
{
    books_view_state books = preview_books_state();
    surface.fill_screen(display_color::white);
    labels.clear();
    paper_mono_views::draw_books_view(surface, books);
    draw_preview_status(ui_view_id::books, "Library");
    emit_preview("books");

    menu_view_state menu = {};
    menu.entry_count = 4U;
    menu.selected_index = 1U;
    std::strcpy(menu.entries[0].label, "Display Test");
    std::strcpy(menu.entries[1].label, "RTC Setting");
    std::strcpy(menu.entries[2].label, "Battery");
    std::strcpy(menu.entries[3].label, "Gray4 Test");
    surface.fill_screen(display_color::white);
    labels.clear();
    paper_mono_views::draw_menu_view(surface, menu);
    draw_preview_status(ui_view_id::menu, "Settings");
    emit_preview("menu");

    file_view_state file = {};
    std::strcpy(file.path, "/books/notes");
    file.status = file_view_status::ready;
    file.row_count = 6U;
    file.page_count = 3U;
    file.page_index = 1U;
    file.selected_index = 2U;
    const char* names[] = {
        "..", "Essays", "reading-list.txt", "notes.epub",
        "archive", "winter.txt",
    };
    for (std::uint8_t index = 0U; index < file.row_count; ++index) {
        std::strcpy(file.rows[index].name, names[index]);
        file.rows[index].enabled = true;
        file.rows[index].directory = index == 0U || index == 1U || index == 4U;
        file.rows[index].parent = index == 0U;
    }
    surface.fill_screen(display_color::white);
    labels.clear();
    paper_mono_views::draw_file_view(surface, file);
    draw_preview_status(ui_view_id::file, "SD Card");
    emit_preview("file");

    books.settings_visible = true;
    surface.fill_screen(display_color::white);
    labels.clear();
    paper_mono_views::draw_books_view(surface, books);
    draw_preview_status(ui_view_id::books, "Library");
    emit_preview("books_settings");
}

void test_visual_states()
{
    surface.fill_screen(display_color::white);
    const display_rect round = {20, 20, 40, 28};
    surface.fill_round_rect(round, 8, display_color::black);
    CHECK_UI(!pixel_is_black(round.left, round.top));
    CHECK_UI(pixel_is_black(round.left + round.width / 2, round.top));
    CHECK_UI(pixel_is_black(round.left + round.width / 2,
                            round.top + round.height / 2));

    books_view_state previous = preview_books_state();
    previous.selected_index = 0U;
    books_view_state next = previous;
    next.selected_index = 1U;
    surface.fill_screen(display_color::white);
    paper_mono_views::draw_books_view(surface, previous);
    const display_rect old_card = books_item_card_rect(0U);
    const display_rect new_card = books_item_card_rect(1U);
    CHECK_UI(pixel_is_black(old_card.left + old_card.width / 2, old_card.top));
    display_request previous_request = {};
    previous_request.view = ui_view_id::books;
    previous_request.payload.books = previous;
    display_request next_request = previous_request;
    next_request.payload.books = next;
    display_rect dirty = {};
    draw_focus_request(next_request, &previous_request, dirty);
    CHECK_UI(!pixel_is_black(old_card.left + old_card.width / 2, old_card.top));
    CHECK_UI(pixel_is_black(new_card.left + new_card.width / 2, new_card.top));
    CHECK_UI(dirty.left == old_card.left);
    CHECK_UI(dirty.left + dirty.width == new_card.left + new_card.width);

    const display_rect cover = books_cover_rect(1U);
    const display_rect name = books_file_name_rect(1U);
    paper_mono_views::draw_books_item(surface, next, 1U, true);
    CHECK_UI(!pixel_is_black(cover.left + cover.width - 4,
                             cover.top + cover.height / 2));
    CHECK_UI(pixel_is_black(name.left + 2, name.top + 2));
    CHECK_UI(pixel_is_black(new_card.left + new_card.width / 2, new_card.top));
    paper_mono_views::draw_books_item(surface, next, 1U, false);
    CHECK_UI(!pixel_is_black(name.left + 2, name.top + 2));
    CHECK_UI(pixel_is_black(new_card.left + new_card.width / 2, new_card.top));

    cjk_draws.clear();
    paper_mono_views::draw_books_item(surface, next, 0U, false);
    CHECK_UI(!cjk_draws.empty());
    CHECK_UI(cjk_draws.back().y ==
             books_file_name_rect(0U).top + BOOKS_FILE_NAME_HEIGHT / 2);
    cjk_draws.clear();
    paper_mono_views::draw_books_item(surface, next, 1U, false);
    CHECK_UI(cjk_draws.size() == 2U);
    CHECK_UI(cjk_draws[0].y ==
             books_file_name_rect(1U).top + BOOKS_FILE_NAME_LINE_HEIGHT / 2);
    CHECK_UI(cjk_draws[1].y ==
             books_file_name_rect(1U).top +
                 BOOKS_FILE_NAME_LINE_HEIGHT + BOOKS_FILE_NAME_LINE_HEIGHT / 2);

    books_view_state empty = {};
    surface.fill_screen(display_color::white);
    paper_mono_views::draw_books_item(surface, empty, 5U, false);
    const display_rect empty_cover = books_cover_rect(5U);
    CHECK_UI(!pixel_is_black(
        empty_cover.left + empty_cover.width / 2, empty_cover.top));

    menu_view_state menu = {};
    menu.entry_count = 1U;
    menu.selected_index = 0U;
    std::strcpy(menu.entries[0].label, "Menu Item");
    surface.fill_screen(display_color::white);
    paper_mono_views::draw_menu_entry(surface, menu, 0U, true);
    const display_rect menu_rect = menu_entry_rect(0U);
    CHECK_UI(!pixel_is_black(menu_rect.left, menu_rect.top));
    CHECK_UI(pixel_is_black(menu_rect.left + menu_rect.width / 2, menu_rect.top));
    CHECK_UI(pixel_is_black(menu_rect.left + 6, menu_rect.top + menu_rect.height / 2));

    file_view_state file = {};
    file.row_count = 1U;
    std::strcpy(file.rows[0].name, "Unavailable");
    file.rows[0].enabled = false;
    surface.fill_screen(display_color::white);
    paper_mono_views::draw_file_row(surface, file, 0U, true);
    const display_rect file_rect = file_row_rect(0U);
    CHECK_UI(!pixel_is_black(
        file_rect.left + file_rect.width / 2, file_rect.top + 3));

    books_view_state settings = preview_books_state();
    settings.settings_visible = true;
    settings.pending_settings.auto_scan_txt = true;
    surface.fill_screen(display_color::white);
    paper_mono_views::draw_books_setting_row(surface, settings, false);
    paper_mono_views::draw_books_control(
        surface, settings, ui_control_type::books_setting_toggle_txt, 0U, true);
    const display_rect setting_row = books_setting_row_rect(false);
    CHECK_UI(pixel_is_black(
        setting_row.left + setting_row.width / 2,
        setting_row.top + setting_row.height / 2));

    rtc_view_state rtc = {};
    rtc.selected_field = rtc_edit_field::year;
    rtc.rtc_available = false;
    surface.fill_screen(display_color::white);
    paper_mono_views::draw_rtc_editor(surface, rtc);
    const display_rect year = rtc_field_rect(rtc_edit_field::year);
    CHECK_UI(pixel_is_black(year.left + year.width / 2, year.top));
    CHECK_UI(!pixel_is_black(year.left + 4, year.top + year.height / 2));
    paper_mono_views::draw_rtc_key(surface, 0U, true, false);
    const display_rect key = rtc_key_rect(0U);
    CHECK_UI(!pixel_is_black(key.left + key.width / 2, key.top + 4));

    surface.fill_screen(display_color::white);
    paper_mono_views::draw_front_light_bar(surface, 2U, 2);
    const display_rect light = paper_mono_views::inset_rect(
        front_light_button_rect(2U), paper_ui::space_xs);
    CHECK_UI(pixel_is_black(light.left + light.width / 2, light.top + 3));
    CHECK_UI(!surface.has_intermediate_gray());

}

}  // namespace

void test_reader_rendering()
{
    CHECK_UI(READER_LINE_COUNT == 24 && reader_text_rect().width == 432);
    CHECK_UI(reader_content_rect().height == 760 && reader_menu_rect().height == 80);
    for (int y = 0; y < UI_DISPLAY_HEIGHT; ++y) {
        for (int x = 0; x < UI_DISPLAY_WIDTH; ++x) {
            unsigned hits = 0;
            for (unsigned zone = 0; zone < 3; ++zone) {
                hits += point_in_rect(x, y, reader_touch_zone_rect(zone));
            }
            CHECK_UI(hits == (y < STATUS_BAR_TOP ? 1U : 0U));
        }
    }
    display_request previous{};
    previous.view = ui_view_id::reader;
    auto& view = previous.payload.reader;
    view.status = reader_view_status::ready;
    view.progress_persistent = true;
    view.page.line_count = READER_LINE_COUNT;
    view.page.text_length = 1;
    view.page.text[0] = 'A';
    for (auto& line : view.page.lines) { line = {0, 1}; }
    status_bar_view_state status{};
    status.foreground_app = ui_view_id::reader;
    status.center_kind = status_bar_center_kind::page;
    status.center_current_page = 19;
    status.center_total_pages = 120;
    surface.fill_screen(display_color::white);
    draw_status_bar(status);
    CHECK_UI(std::any_of(labels.begin(), labels.end(), [](const label& l) {
        return l.text == "/" && l.x == 240 && l.y == 780 && l.align == display_text_alignment::middle_center;
    }));
    labels.clear();
    paper_mono_views::draw_reader_view(surface, view);
    CHECK_UI(reader_lines == 24 && last_line_bottom == 733);
    CHECK_UI(labels.empty()); // No permanent Back/previous/next labels.
    const auto baseline = std::vector<unsigned char>(pixels.begin(), pixels.end());
    display_request next = previous;
    next.update_region = display_update_region::reader_menu;
    next.payload.reader.menu_visible = true;
    CHECK_UI(resolve_request_region(next, &previous, true, false) == display_update_region::reader_menu);
    display_rect rect{};
    draw_partial_request(next, &previous, next.update_region, rect);
    CHECK_UI(rect.top == 0 && rect.height == 80);
    CHECK_UI(pixels[31 * 60 + 3] != baseline[31 * 60 + 3]); // Covered first line.
    CHECK_UI(std::equal(pixels.begin() + 80 * 60, pixels.end(), baseline.begin() + 80 * 60));
    CHECK_UI(labels.size() == 1 && labels[0].text == "Back" && labels[0].x == 48);
    paper_mono_views::draw_back_button(surface, ui_view_id::reader, true);
    CHECK_UI(pixel_is_black(24, 40));
    CHECK_UI(!pixel_is_black(150, 40));
    next.payload.reader.menu_visible = false;
    draw_partial_request(next, &previous, next.update_region, rect);
    CHECK_UI(std::equal(pixels.begin(), pixels.end(), baseline.begin())); // Hide restores text exactly.
    for (unsigned change = 0; change < 4; ++change) {
        next.payload.reader = previous.payload.reader;
        if (change == 0) { ++next.payload.reader.page.current_page_start_offset; }
        if (change == 1) { next.payload.reader.page.text[0] = 'B'; }
        if (change == 2) { next.payload.reader.progress_persistent = false; }
        if (change == 3) { next.payload.reader.status = reader_view_status::loading; }
        CHECK_UI(resolve_request_region(next, &previous, true, false) == display_update_region::reader_content);
    }
    CHECK_UI(resolve_request_region(next, nullptr, false, false) == display_update_region::reader_content);
    CHECK_UI(resolve_request_region(next, &previous, true, true) == display_update_region::full);
    ghost_debt debt{};
    for (unsigned count = 0; count < 19; ++count) {
        const auto region = count % 2 ? display_update_region::reader_menu : display_update_region::reader_content;
        CHECK_UI(resolve_mode(refresh_mode::text, region, true, debt) == refresh_mode::text);
        record_refresh(debt, refresh_mode::text, region);
    }
    CHECK_UI(resolve_mode(refresh_mode::text, display_update_region::reader_menu, true, debt) == refresh_mode::quality);
    record_refresh(debt, refresh_mode::quality, display_update_region::reader_menu);
    CHECK_UI(debt.reader_content.text_count == 0);

    request_queue = xQueueCreate(DISPLAY_REQUEST_QUEUE_LENGTH, sizeof(ui_frame_handle));
    renderer_task_handle = xTaskGetCurrentTaskHandle();
    for (auto reason : {ui_update_reason::view_opened, ui_update_reason::content_changed, ui_update_reason::popup_changed}) {
        CHECK_UI(tested_ui_write_reader_frame(reason, [](reader_view_state& target, const void* source) {
            target = *static_cast<const reader_view_state*>(source); return true;
        }, &view));
        ui_frame_handle handle{};
        CHECK_UI(xQueueReceive(request_queue, &handle, 0) == pdTRUE);
        const display_request* frame = nullptr;
        CHECK_UI(ui_frame_pool_resolve(handle, frame));
        CHECK_UI(frame->update_region == (reason == ui_update_reason::view_opened ? display_update_region::full :
            reason == ui_update_reason::popup_changed ? display_update_region::reader_menu : display_update_region::reader_content));
        CHECK_UI(frame->mode == (reason == ui_update_reason::view_opened ? refresh_mode::quality : refresh_mode::text));
        CHECK_UI(ui_frame_pool_release(handle));
    }
    const std::uint8_t reader_cover[] = {
        0xffU, 0xd8U, 0xffU, 0xc0U, 0x00U, 0x07U,
        0x08U, 0x00U, 0x01U, 0x00U, 0x01U,
    };
    CHECK_UI(ui_reader_cover_begin(
        sizeof(reader_cover), book_cover_encoding::jpeg));
    CHECK_UI(ui_reader_cover_append(
        0U, reader_cover, sizeof(reader_cover)));
    view.showing_cover = true;
    view.cover_generation = ui_reader_cover_commit();
    image_draws = 0U;
    last_image_mode = display_image_mode::mono_dither;
    paper_mono_views::draw_reader_view(surface, view);
    CHECK_UI(image_draws == 1U);
    CHECK_UI(last_image_mode == display_image_mode::gray4);
    ui_reader_cover_clear();
    view.showing_cover = false;

    books_view_state books{};
    struct books_request_case {
        ui_update_reason reason;
        ui_control_type control;
        display_update_region region;
        refresh_mode mode;
    };
    constexpr books_request_case books_cases[] = {
        {ui_update_reason::view_opened, ui_control_type::none,
         display_update_region::full, refresh_mode::fastest},
        {ui_update_reason::content_changed, ui_control_type::none,
         display_update_region::books_content, refresh_mode::text},
        {ui_update_reason::popup_changed, ui_control_type::none,
         display_update_region::books_modal, refresh_mode::fastest},
        {ui_update_reason::selection_changed,
         ui_control_type::books_setting_toggle_txt,
         display_update_region::books_setting_txt, refresh_mode::fastest},
        {ui_update_reason::selection_changed,
         ui_control_type::books_setting_toggle_epub,
         display_update_region::books_setting_epub, refresh_mode::fastest},
        {ui_update_reason::selection_changed,
         ui_control_type::books_select_item,
         display_update_region::focus, refresh_mode::fastest},
    };
    for (const books_request_case& test : books_cases) {
        CHECK_UI(tested_ui_render_books(books, test.reason, test.control));
        ui_frame_handle handle{};
        CHECK_UI(xQueueReceive(request_queue, &handle, 0) == pdTRUE);
        const display_request* frame = nullptr;
        CHECK_UI(ui_frame_pool_resolve(handle, frame));
        CHECK_UI(frame->update_region == test.region);
        CHECK_UI(frame->mode == test.mode);
        CHECK_UI(ui_frame_pool_release(handle));
    }
    display_control_request books_control{};
    books_control.view = ui_view_id::books;
    books_control.control = ui_control_type::books_page_next;
    display_request books_frame{};
    books_frame.view = ui_view_id::books;
    CHECK_UI(control_replaced_by_frame(
        books_control, books_frame, refresh_mode::text,
        display_update_region::books_content));
    books_control.control = ui_control_type::books_setting_confirm;
    CHECK_UI(control_replaced_by_frame(
        books_control, books_frame, refresh_mode::fastest,
        display_update_region::books_modal));
    books_control.control = ui_control_type::books_settings;
    CHECK_UI(!control_replaced_by_frame(
        books_control, books_frame, refresh_mode::fastest,
        display_update_region::books_modal));
    launcher_view_state launcher{};
    CHECK_UI(tested_ui_render_launcher(launcher, ui_update_reason::view_opened));
    ui_frame_handle launcher_handle{};
    CHECK_UI(xQueueReceive(request_queue, &launcher_handle, 0) == pdTRUE);
    const display_request* launcher_frame = nullptr;
    CHECK_UI(ui_frame_pool_resolve(launcher_handle, launcher_frame));
    CHECK_UI(launcher_frame->update_region == display_update_region::full);
    CHECK_UI(launcher_frame->mode == refresh_mode::fastest);
    CHECK_UI(ui_frame_pool_release(launcher_handle));

    display_request previous_focus{};
    previous_focus.view = ui_view_id::launcher;
    previous_focus.payload.launcher.entry_count = 3U;
    previous_focus.payload.launcher.selected_index = 0U;
    display_request next_focus = previous_focus;
    next_focus.payload.launcher.selected_index = 2U;
    display_rect focus_rect{};
    draw_focus_request(next_focus, &previous_focus, focus_rect);
    const auto first_focus = launcher_entry_rect(0U);
    const auto last_focus = launcher_entry_rect(2U);
    CHECK_UI(focus_rect.top == first_focus.top);
    CHECK_UI(focus_rect.top + focus_rect.height ==
             last_focus.top + last_focus.height);

    gray4_test_view_state gray4{};
    CHECK_UI(tested_ui_render_gray4_test(gray4, ui_update_reason::view_opened));
    ui_frame_handle gray4_handle{};
    CHECK_UI(xQueueReceive(request_queue, &gray4_handle, 0) == pdTRUE);
    const display_request* gray4_frame = nullptr;
    CHECK_UI(ui_frame_pool_resolve(gray4_handle, gray4_frame));
    CHECK_UI(gray4_frame->update_region == display_update_region::full);
    CHECK_UI(gray4_frame->mode == refresh_mode::quality);
    CHECK_UI(ui_frame_pool_release(gray4_handle));

    const std::uint8_t compressed_cover[] = {0xffU, 0xd8U, 0xffU, 0xd9U};
    CHECK_UI(ui_books_cover_begin(0U, sizeof(compressed_cover), book_cover_encoding::jpeg));
    CHECK_UI(ui_books_cover_append(0U, 0U, compressed_cover, sizeof(compressed_cover)));
    books = {};
    books.item_count = 1U;
    books.page_count = 1U;
    books.items[0].occupied = books.items[0].enabled = true;
    books.items[0].format = book_file_format::epub;
    books.items[0].cover_generation = ui_books_cover_commit(0U);
    image_draws = 0U;
    last_image_mode = display_image_mode::gray4;
    paper_mono_views::draw_books_view(surface, books);
    CHECK_UI(image_draws == 1U);
    CHECK_UI(last_image_mode == display_image_mode::mono_dither);
    const auto expected_cover = books_cover_rect(0U);
    CHECK_UI(last_image_rect.left == expected_cover.left &&
             last_image_rect.top == expected_cover.top &&
             last_image_rect.width == expected_cover.width &&
             last_image_rect.height == expected_cover.height);
    ui_books_cover_clear();
    books.items[0].format = book_file_format::txt;
    books.items[0].cover_generation = 0U;
    books.items[0].preview_line_count = 1U;
    std::strcpy(books.items[0].preview[0], "Preview");
    image_draws = 0U;
    paper_mono_views::draw_books_view(surface, books);
    CHECK_UI(image_draws == 0U);
    paper_mono_views::draw_books_control(
        surface, books, ui_control_type::books_back, 0U, true);
    const auto back = books_back_rect();
    const auto pressed_pixel =
        std::size_t(back.top + 20) * UI_DISPLAY_WIDTH + back.left + 20;
    CHECK_UI((pixels[pressed_pixel / 8U] & (1U << (pressed_pixel % 8U))) != 0U);
    paper_mono_views::draw_books_control(
        surface, books, ui_control_type::books_back, 0U, false);
    CHECK_UI((pixels[pressed_pixel / 8U] & (1U << (pressed_pixel % 8U))) == 0U);

    control_queue = xQueueCreate(
        DISPLAY_CONTROL_QUEUE_LENGTH, sizeof(display_control_request));
    CHECK_UI(tested_ui_render_control(
        ui_view_id::books, ui_control_type::books_back, 0U, true));
    display_control_request control{};
    CHECK_UI(xQueueReceive(control_queue, &control, 0) == pdTRUE);
    CHECK_UI(control.view == ui_view_id::books &&
             control.control == ui_control_type::books_back &&
             control.pressed);
    vQueueDelete(control_queue); control_queue = nullptr;
    vQueueDelete(request_queue); request_queue = nullptr; renderer_task_handle = nullptr;
    ulTaskNotifyTake(pdTRUE, 0);
    labels.clear();
    test_visual_states();
    render_visual_previews();
    std::puts("PASS UI rendering policy: Reader quality entry, mono Books entry/covers, Gray4 diagnostic entry, control feedback");
}
