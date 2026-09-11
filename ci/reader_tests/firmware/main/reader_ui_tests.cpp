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

#define CHECK_UI(condition) do { if (!(condition)) { \
    std::printf("TEST_FAILURE renderer line=%d: %s\n", __LINE__, #condition); std::fflush(stdout); \
    abort(); } } while (0)

namespace {
std::array<unsigned char, UI_DISPLAY_WIDTH * UI_DISPLAY_HEIGHT / 8> pixels{};
display_surface surface;
display_text_alignment alignment = display_text_alignment::middle_left;
display_font font = display_font::default_font;
unsigned text_scale = 1U;
unsigned reader_lines = 0U;
unsigned image_draws = 0U;
display_rect last_image_rect = {};
int last_line_bottom = 0;
struct label { std::string text; int x, y; display_text_alignment align; };
std::vector<label> labels;

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
}

std::int16_t display_surface::width() const { return UI_DISPLAY_WIDTH; }
std::int16_t display_surface::height() const { return UI_DISPLAY_HEIGHT; }
void display_surface::fill_screen(display_color c) { paint(0, 0, width(), height(), c); }
void display_surface::fill_rect(const display_rect& r, display_color c) { paint(r.left, r.top, r.width, r.height, c); }
void display_surface::fill_rect(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h, display_color c) { paint(x, y, w, h, c); }
void display_surface::draw_rect(const display_rect& r, display_color c) { paint(r.left, r.top, r.width, 1, c); }
void display_surface::draw_rect(std::int16_t x, std::int16_t y, std::int16_t w,
                                std::int16_t h, display_color c)
{ draw_rect({x, y, w, h}, c); }
void display_surface::draw_horizontal_line(std::int16_t x, std::int16_t y, std::int16_t w, display_color c) { paint(x, y, w, 1, c); }
void display_surface::draw_line(std::int16_t x0, std::int16_t y0, std::int16_t x1,
                                std::int16_t y1, display_color c)
{
    if (x0 == x1) { paint(x0, std::min(y0, y1), 1, std::abs(y1 - y0) + 1, c); }
    else if (y0 == y1) { paint(std::min(x0, x1), y0, std::abs(x1 - x0) + 1, 1, c); }
}
void display_surface::fill_triangle(std::int16_t, std::int16_t, std::int16_t, std::int16_t, std::int16_t, std::int16_t, display_color) {}
void display_surface::set_font(display_font f) { font = f; }
void display_surface::set_text_color(display_color, display_color) {}
void display_surface::set_text_alignment(display_text_alignment a) { alignment = a; }
void display_surface::set_text_size(std::uint8_t s) { text_scale = s; }
void display_surface::draw_text(const char* text, std::int16_t x, std::int16_t y)
{
    labels.push_back({text, x, y, alignment});
    const int w = std::strlen(text) * 6 * text_scale;
    const int h = 8 * text_scale;
    const int left = alignment == display_text_alignment::middle_left ? x :
                     alignment == display_text_alignment::middle_center ? x - w / 2 : x - w;
    paint(left, y - h / 2, w, h, display_color::black);
}
std::int32_t display_surface::text_width(const char* text) const
{ return static_cast<std::int32_t>(std::strlen(text) * 6U * text_scale); }
bool display_surface::draw_image(const std::uint8_t*, std::size_t,
                                 book_cover_encoding, const display_rect& rect)
{
    ++image_draws;
    last_image_rect = rect;
    return true;
}
display_surface& hal_display_surface() { return surface; }
bool hal_display_init() { return true; }
bool hal_display_sleep() { return true; }
display_refresh_result hal_display_refresh(const display_rect&, refresh_mode mode)
{ return {true, mode, 0U, display_refresh_error::none}; }

// The font rasterizer is the board adapter here; production Reader line placement
// and overlay drawing run unchanged. No claims about physical glyph rendering.
void paper_mono_draw_cjk_text(display_surface&, const char*, std::size_t, std::int16_t x, std::int16_t y)
{
    ++reader_lines;
    CHECK_UI(y - 12 >= reader_text_rect().top);
    last_line_bottom = y + 12;
    CHECK_UI(last_line_bottom <= reader_text_rect().top + reader_text_rect().height);
    paint(x, y - 12, 12, 24, display_color::black);
}

namespace paper_mono_views {
void draw_launcher_view(display_surface&, const launcher_view_state&) {}
void draw_menu_view(display_surface&, const menu_view_state&) {}
void draw_menu_entry(display_surface&, const menu_view_state&, std::uint8_t, bool) {}
void draw_test_view(display_surface&, const test_view_state&, std::uint8_t, std::int16_t) {}
void draw_front_light_bar(display_surface&, std::uint8_t, std::int16_t) {}
void draw_test_content(display_surface&, const test_view_state&) {}
void draw_rtc_view(display_surface&, const rtc_view_state&) {}
void draw_rtc_editor(display_surface&, const rtc_view_state&) {}
void draw_rtc_key(display_surface&, std::uint8_t, bool, bool) {}
bool rtc_keys_enabled(const rtc_view_state&) { return false; }
void draw_battery_view(display_surface&, const battery_view_state&) {}
void draw_battery_content(display_surface&, const battery_view_state&) {}
void draw_gray4_test_view(display_surface&, const gray4_test_view_state&) {}
void draw_file_view(display_surface&, const file_view_state&) {}
void draw_file_content(display_surface&, const file_view_state&) {}
void draw_file_row(display_surface&, const file_view_state&, std::uint8_t, bool) {}
void draw_file_page_button(display_surface&, const file_view_state&, bool, bool) {}
}

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
    draw_partial_request(next, next.update_region, rect);
    CHECK_UI(rect.top == 0 && rect.height == 80);
    CHECK_UI(pixels[31 * 60 + 3] != baseline[31 * 60 + 3]); // Covered first line.
    CHECK_UI(std::equal(pixels.begin() + 80 * 60, pixels.end(), baseline.begin() + 80 * 60));
    CHECK_UI(labels.size() == 1 && labels[0].text == "<" && labels[0].x == 24);
    next.payload.reader.menu_visible = false;
    draw_partial_request(next, next.update_region, rect);
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
    books_view_state books{};
    struct books_request_case {
        ui_update_reason reason;
        ui_control_type control;
        display_update_region region;
        refresh_mode mode;
    };
    constexpr books_request_case books_cases[] = {
        {ui_update_reason::view_opened, ui_control_type::none,
         display_update_region::full, refresh_mode::quality},
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
    paper_mono_views::draw_books_view(surface, books);
    CHECK_UI(image_draws == 1U);
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
    vQueueDelete(request_queue); request_queue = nullptr; renderer_task_handle = nullptr;
    ulTaskNotifyTake(pdTRUE, 0);
    labels.clear();
    std::puts("PASS UI rendering policy: Reader zones/overlay/debt, Books regions, EPUB image path, TXT preview path");
}
