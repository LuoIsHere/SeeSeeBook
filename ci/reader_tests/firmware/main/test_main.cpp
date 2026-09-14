// QEMU integration harness. Production sources are compiled unchanged; only
// board storage and physical display are replaced with controlled adapters.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <map>
#include <string>
#include <vector>

#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "app.hpp"
#include "app_registry.hpp"
#include "app_descriptor.hpp"
#include "books_app.hpp"
#include "books_layout.hpp"
#include "file_app.hpp"
#include "file_name.hpp"
#include "launcher_app.hpp"
#include "launcher_layout.hpp"
#include "menu_app.hpp"
#include "menu_layout.hpp"
#include "reader_app.hpp"
#include "text_paginator.hpp"
#include "book_service.hpp"
#include "book_catalog.hpp"
#include "service_event_source.hpp"
#include "storage.hpp"
#include "storage_service.hpp"
#include "system_tick_service.hpp"
#include "text_layout_provider.hpp"
#include "ui_frame_pool.hpp"
#include "ui_interaction_router.hpp"
#include "ui_presentation.hpp"
#include "utf8.hpp"

void test_book_formats_and_engine();
void test_epub_support();
void test_reader_rendering();
void test_gray4_support();
void test_books_ui_support();
std::string make_epub_fixture(bool cover, char filler);

#define CHECK(condition) do { if (!(condition)) { \
    std::printf("TEST_FAILURE line=%d: %s\n", __LINE__, #condition); std::fflush(stdout); \
    abort(); } } while (0)

namespace {
std::string book;
std::string epub_book;
std::map<std::string, std::string, std::less<>> system_files;
std::atomic_bool inserted{false};
std::atomic_bool pause_reads{false};
std::atomic_bool in_read{false};
std::atomic_bool mounted{false};
std::atomic_int64_t modified_time{100};
std::atomic_int64_t epub_modified_time{200};
SemaphoreHandle_t filesystem_lock;
SemaphoreHandle_t read_gate;
TaskHandle_t monitor_task;
std::uint32_t extra_time = 0;
ui_frame_handle visible = invalid_ui_frame_handle();
ui_frame_handle pending = invalid_ui_frame_handle();
bool defer_display = false;
bool reject_reader_frames = false;
unsigned control_feedback = 0;
unsigned frame_submissions = 0;
app_record test_records[6];
constexpr app_descriptor descriptors[] = {
    {app_kind::launcher, ui_view_id::launcher, "Launcher"},
    {app_kind::books, ui_view_id::books, "Books"},
    {app_kind::menu, ui_view_id::menu, "Menu"},
    {app_kind::test, ui_view_id::test, "Test"},
    {app_kind::file, ui_view_id::file, "File"},
    {app_kind::reader, ui_view_id::reader, "Reader"},
};

std::uint16_t measure(std::uint32_t cp) { return cp < 128U ? 1U : 2U; }
const text_layout_profile test_layout{12U, 3U, measure};

class test_child final : public app_base {
public:
    void handle_app_event(const app_event& event) override
    {
        if (event.type == app_event_type::ui_action &&
            event.action.control == ui_control_type::navigate_back &&
            event.action.input.gesture == input_gesture_type::click) {
            app_request_back();
        }
    }

protected:
    void on_open() override
    {
        ui_render_test({}, ui_update_reason::view_opened);
    }
};

const display_request* shown()
{
    const display_request* frame = nullptr;
    CHECK(ui_frame_pool_resolve(visible, frame));
    return frame;
}

void commit_display()
{
    if (defer_display || !ui_frame_handle_is_valid(pending)) { return; }
    CHECK(ui_presentation_commit_frame(pending, false));
    CHECK(!ui_frame_handle_is_valid(visible) || ui_frame_pool_release(visible));
    visible = pending;
    pending = invalid_ui_frame_handle();
}

void pump(bool deliver_results = true)
{
    if (monitor_task) { xTaskNotifyGive(monitor_task); }
    vTaskDelay(1);
    storage_status_event status{};
    while (!app_switch_pending() && storage_service_try_get_status_event(status)) {
        app_event event{};
        event.type = app_event_type::storage_status;
        event.storage_status = {status.state, status.media_generation, status.error};
        app_dispatch_event(event);
    }
    storage_result_event result{};
    while (deliver_results && !app_switch_pending() && storage_service_try_get_result_event(result)) {
        app_event event{};
        event.type = app_event_type::storage_result;
        event.storage_result.handle = result.handle;
        app_dispatch_event(event);
        CHECK(storage_service_release_result(result.handle));
    }
    book_service_event book_event{};
    while (deliver_results && !app_switch_pending() && book_service_try_get_event(book_event)) {
        app_event event{};
        event.type = app_event_type::book;
        event.book = book_event;
        app_dispatch_event(event);
    }
    book_result_event book_result{};
    while (deliver_results && !app_switch_pending() && book_service_try_get_result_event(book_result)) {
        app_event event{};
        event.type = app_event_type::book_result;
        event.book_result = book_result.handle;
        app_dispatch_event(event);
        CHECK(book_service_release_result(book_result.handle));
    }
    app_update();
    commit_display();
}

template<typename predicate>
void until(predicate done, bool results = true)
{
    for (unsigned attempt = 0; attempt < 15000; ++attempt) {
        pump(results);
        if (done()) { return; }
    }
    CHECK(false && "timeout");
}

void wait_reader(reader_view_status status)
{
    until([=] { return ui_frame_handle_is_valid(visible) && shown()->view == ui_view_id::reader &&
                        shown()->payload.reader.status == status &&
                        ui_presentation_input_ready(ui_view_id::reader); });
}

void wait_file(const char* path)
{
    until([=] { return ui_frame_handle_is_valid(visible) && shown()->view == ui_view_id::file &&
                        shown()->payload.file.status == file_view_status::ready &&
                        std::strcmp(shown()->payload.file.path, path) == 0; });
}

void wait_view(ui_view_id view)
{
    until([=] {
        return ui_frame_handle_is_valid(visible) && shown()->view == view &&
               ui_presentation_input_ready(view);
    });
}

void click(int x, int y)
{
    input_event input{};
    input.start_x = input.end_x = x;
    input.start_y = input.end_y = y;
    input.gesture = input_gesture_type::press;
    ui_action_event action{};
    const bool pressed = ui_interaction_process(input, action);
    CHECK(pressed == (shown()->view != ui_view_id::reader));
    app_event event{};
    event.type = app_event_type::ui_action;
    event.action = action;
    if (pressed) { app_dispatch_event(event); }
    input.gesture = input_gesture_type::click;
    CHECK(ui_interaction_process(input, action));
    event.action = action;
    app_dispatch_event(event);
    pump();
}

void reader_back()
{
    if (!shown()->payload.reader.menu_visible) { click(240, 360); }
    CHECK(shown()->payload.reader.menu_visible);
    click(80, 40);
}

void ignored_reader_click(int x, int y, int end_x = -1, int end_y = -1)
{
    input_event input{};
    input.start_x = x; input.start_y = y;
    input.end_x = end_x < 0 ? x : end_x;
    input.end_y = end_y < 0 ? y : end_y;
    ui_action_event action{};
    input.gesture = input_gesture_type::press;
    CHECK(!ui_interaction_process(input, action));
    input.gesture = input_gesture_type::click;
    CHECK(!ui_interaction_process(input, action));
}

void test_reader_menu()
{
    CHECK(!shown()->payload.reader.menu_visible);
    const auto body = shown()->payload.reader;
    const auto frames = frame_submissions;
    const auto feedback = control_feedback;
    ignored_reader_click(80, 360); // First page cannot underflow.
    ignored_reader_click(400, 780); // Status bar is outside all zones.
    ignored_reader_click(400, 360, 430, 360); // Drag inside the same large zone.
    ignored_reader_click(319, 360, 320, 360); // Crossing a semantic boundary.
    input_event input{};
    input.start_x = input.end_x = 400; input.start_y = input.end_y = 360;
    ui_action_event action{};
    for (auto gesture : {input_gesture_type::press, input_gesture_type::long_press_start,
                         input_gesture_type::long_press_repeat, input_gesture_type::long_press_end,
                         input_gesture_type::click}) {
        input.gesture = gesture;
        CHECK(!ui_interaction_process(input, action));
    }
    CHECK(frame_submissions == frames && control_feedback == feedback);
    click(240, 360);
    CHECK(shown()->payload.reader.menu_visible);
    CHECK(reader_body_matches(body, shown()->payload.reader));
    ignored_reader_click(400, 360);
    ignored_reader_click(80, 360);
    ignored_reader_click(240, 40); // Menu blank area cannot close/click through.
    ignored_reader_click(400, 40);
    app_event direct{};
    direct.type = app_event_type::ui_action;
    direct.action.control = ui_control_type::reader_next_zone;
    direct.action.input.gesture = input_gesture_type::click;
    app_dispatch_event(direct); // App also enforces modality independently of UI.
    CHECK(shown()->payload.reader.menu_visible && reader_body_matches(body, shown()->payload.reader));
    click(240, 360);
    CHECK(!shown()->payload.reader.menu_visible);
    CHECK(control_feedback == feedback);
    reject_reader_frames = true;
    click(240, 360);
    CHECK(!shown()->payload.reader.menu_visible); // Rejected frame cannot alter presented hit targets.
    reject_reader_frames = false; pump();
    CHECK(shown()->payload.reader.menu_visible);
    click(240, 360);
    CHECK(!shown()->payload.reader.menu_visible);
    std::puts("PASS Reader menu: hidden by default, modal zones, click-only, drag rejection, no press feedback, frame retry");
}

void wait_changed(std::uint64_t old)
{
    until([=] { return shown()->view == ui_view_id::reader &&
                        shown()->payload.reader.status == reader_view_status::ready &&
                        shown()->payload.reader.page.current_page_start_offset != old; });
}

void set_card(bool present)
{
    inserted = present;
    until([=] { return storage_service_get_state() ==
                        (present ? storage_state::ready : storage_state::no_card); });
}

bool request_reader(const char* path, std::uint32_t media_generation, book_file_format format)
{
    app_launch_context context = {};
    return reader_make_launch_context(context, path, media_generation, format) &&
           app_request_launch(app_kind::reader, context);
}

void test_launch_context()
{
    app_launch_context context = {};
    reader_launch_parameters parameters = {};
    CHECK(!context.has_value());
    CHECK(!reader_read_launch_context(context, parameters));
    CHECK(!reader_make_launch_context(context, "books/book.txt", 7U, book_file_format::txt));
    CHECK(!context.has_value());
    CHECK(reader_make_launch_context(context, "/books/book.epub", 7U,
                                     book_file_format::epub));
    CHECK(context.has_value());
    CHECK(reader_read_launch_context(context, parameters));
    CHECK(std::strcmp(parameters.file_path, "/books/book.epub") == 0);
    CHECK(parameters.media_generation == 7U);
    CHECK(parameters.format == book_file_format::epub);
    ++context.type;
    CHECK(!reader_read_launch_context(context, parameters));
    context.clear();
    CHECK(!context.has_value());
    std::puts("PASS generic launch context: validation, owned payload, type rejection, clear");
}

void open_reader()
{
    CHECK(request_reader("/books/book.TXT", storage_service_get_media_generation(), book_file_format::txt));
    pump();
    wait_reader(reader_view_status::ready);
}

std::string flatten(const reader_page& page)
{
    CHECK(page.text_length < READER_PAGE_TEXT_CAPACITY);
    CHECK(page.text[page.text_length] == '\0');
    CHECK(page.line_count <= test_layout.line_count);
    std::string text;
    for (unsigned line = 0; line < page.line_count; ++line) {
        const auto& range = page.lines[line];
        CHECK(range.offset + range.length <= page.text_length);
        unsigned width = 0;
        for (std::size_t i = range.offset; i < range.offset + range.length;) {
            std::uint32_t cp = 0;
            std::size_t size = 0;
            CHECK(utf8_decode(page.text + i, range.offset + range.length - i, cp, size) ==
                  utf8_decode_result::complete);
            width += measure(cp);
            i += size;
        }
        CHECK(width <= test_layout.line_width);
        text.append(page.text + range.offset, range.length);
    }
    return text;
}

std::vector<std::uint64_t> paginate(const std::string& input, std::size_t block, std::string& output)
{
    reader_paginator parser;
    std::vector<std::uint64_t> starts;
    std::uint64_t offset = 0;
    output.clear();
    do {
        starts.push_back(offset);
        parser.reset(offset, test_layout);
        reader_parse_status status;
        do {
            const auto at = parser.read_offset();
            const auto size = std::min<std::size_t>(block, input.size() - at);
            status = parser.feed(input.data() + at, size, at + size == input.size());
        } while (status == reader_parse_status::need_data);
        CHECK(status == reader_parse_status::page_ready);
        output += flatten(parser.page());
        CHECK(parser.page().current_page_start_offset == offset);
        if (parser.page().end_of_file) { break; }
        CHECK(parser.page().next_page_start_offset > offset);
        offset = parser.page().next_page_start_offset;
    } while (true);
    return starts;
}

void test_pagination()
{
    const std::vector<std::string> fixtures = {
        "", "a", std::string(36, 'a'), std::string(37, 'a'),
        "\n", "\r\n", "\n\n\n", "\r\n\r\n\r\n", "\r\n\r\n\r\nX",
        "中文混排 English words\r\n\n第二段内容\nend",
        "\xef\xbb\xbf中文", "\xef\xbb\xbf", std::string(35, 'a') + "中X",
        std::string(2047, 'x') + "中😀文\n" + std::string(2100, 'y'),
        "one\ttwo\n\n三四五\r六七八\r\nend"
    };
    for (const auto& fixture : fixtures) {
        std::string expected;
        std::size_t i = fixture.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
        for (; i < fixture.size(); ++i) {
            const char c = fixture[i];
            if (c == '\r' || c == '\n') { continue; }
            expected += c == '\t' ? ' ' : c;
        }
        std::string baseline;
        auto starts = paginate(fixture, 4096, baseline);
        CHECK(baseline == expected);
        for (const auto block : {1U, 2U, 3U, 4U, 7U, 31U, 2048U}) {
            std::string text;
            CHECK(paginate(fixture, block, text) == starts);
            CHECK(text == expected);
        }
    }
    std::string output;
    CHECK(paginate(std::string(36, 'a'), 1, output).size() == 1);
    CHECK(paginate(std::string(37, 'a'), 1, output) == std::vector<std::uint64_t>({0, 36}));
    CHECK(paginate("\r\n\r\n\r\nX", 1, output) == std::vector<std::uint64_t>({0, 6}));
    for (const std::string& bad : {std::string("\x80"), std::string("\xc0\xaf"),
         std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
         std::string("\xe4\xb8"), std::string("\xe4X"), std::string("\xff\xfe")}) {
        for (std::size_t split = 0; split <= bad.size(); ++split) {
            reader_paginator parser;
            parser.reset(0, test_layout);
            auto status = parser.feed(bad.data(), split, false);
            if (status != reader_parse_status::invalid_utf8) {
                status = parser.feed(bad.data() + split, bad.size() - split, true);
            }
            CHECK(status == reader_parse_status::invalid_utf8);
        }
    }
    // Deterministic mixed-text stress, checking conservation and chunk invariance.
    std::uint32_t random = 17;
    const std::string tokens[] = {"a", "中", "😀", "\r\n", "\n", " ", "文", "0123"};
    for (unsigned run = 0; run < 100; ++run) {
        std::string sample;
        for (unsigned i = 0; i < 100; ++i) {
            random = random * 1664525U + 1013904223U;
            sample += tokens[(random >> 24U) % 8U];
        }
        std::string first, second;
        CHECK(paginate(sample, 1, first) == paginate(sample, 127, second));
        CHECK(first == second);
    }
    reader_page_history history;
    for (unsigned i = 0; i < 1000; ++i) { history.push(i); }
    CHECK(history.size() == READER_HISTORY_CAPACITY);
    for (unsigned i = 1000; i > 936; --i) {
        std::uint64_t previous = 0;
        CHECK(history.previous(previous) && previous == i - 1);
        history.pop();
    }
    CHECK(history.size() == 0);
    std::puts("PASS pagination: Unicode, block boundaries, exact pages, CRLF, malformed input, bounded history");
}

void test_names()
{
    const std::vector<std::string> names = {"file", "file.txt", "short.txt", "longlonglonglong.txt",
        "中文中文中文中文中文.txt", "archive.tar.gz", ".gitignore", "a.", "....", "no_extension",
        "abc.def.txt", "a." + std::string(100, 'x'), "a.超长中文扩展名超长中文扩展名"};
    for (const auto& name : names) {
        for (bool directory : {false, true}) {
            for (std::size_t capacity = 0; capacity <= 70; ++capacity) {
                for (unsigned width = 0; width <= 40; ++width) {
                    char guarded[74];
                    std::memset(guarded, 0x5a, sizeof(guarded));
                    auto layout = test_layout;
                    layout.line_width = width;
                    format_file_name(name, directory, guarded + 1, capacity, layout);
                    CHECK(guarded[0] == 0x5a && guarded[capacity + 1] == 0x5a);
                    if (!capacity) { continue; }
                    CHECK(std::memchr(guarded + 1, 0, capacity));
                    const std::string text(guarded + 1);
                    unsigned used = 0;
                    for (std::size_t i = 0; i < text.size();) {
                        std::uint32_t cp = 0;
                        std::size_t length = 0;
                        CHECK(utf8_decode(text.data() + i, text.size() - i, cp, length) ==
                              utf8_decode_result::complete);
                        used += measure(cp);
                        i += length;
                    }
                    CHECK(used <= width);
                    const auto dot = name.find_last_of('.');
                    if (!directory && dot != std::string::npos && dot != 0 && dot + 1 < name.size()) {
                        const auto extension = name.substr(dot);
                        unsigned ext_width = 0;
                        for (std::size_t at = 0; at < extension.size();) {
                            std::uint32_t cp; std::size_t size;
                            utf8_decode(extension.data() + at, extension.size() - at, cp, size);
                            ext_width += measure(cp); at += size;
                        }
                        if (extension.size() < capacity && ext_width <= width) {
                            CHECK(text.size() >= extension.size());
                            CHECK(text.compare(text.size() - extension.size(), extension.size(), extension) == 0);
                        }
                    }
                }
            }
        }
    }
    CHECK(file_name_is_txt("book.txt") && file_name_is_txt("book.TXT") && file_name_is_txt("book.Txt"));
    CHECK(!file_name_is_txt(".txt") && !file_name_is_txt("book.txt.bak") && !file_name_is_txt("file"));
    CHECK(file_name_book_format("book.epub") == book_file_format::epub &&
          file_name_book_format("BOOK.EPUB") == book_file_format::epub &&
          file_name_book_format("book.EpUb") == book_file_format::epub);
    CHECK(file_name_book_format(".epub") == book_file_format::unknown &&
          file_name_book_format("book.epub.zip") == book_file_format::unknown);
    std::puts("PASS filenames: capacity/width matrix, extension retention, UTF-8, guard bytes");
}

void test_app_navigation()
{
    wait_view(ui_view_id::launcher);
    CHECK(shown()->payload.launcher.entry_count == 3U);
    CHECK(std::strcmp(shown()->payload.launcher.entries[0].label, "Books") == 0);
    CHECK(std::strcmp(shown()->payload.launcher.entries[1].label, "File") == 0);
    CHECK(std::strcmp(shown()->payload.launcher.entries[2].label, "Menu") == 0);

    const auto books_entry = launcher_entry_rect(0U);
    click(
        books_entry.left + books_entry.width / 2,
        books_entry.top + books_entry.height / 2);
    wait_view(ui_view_id::books);
    CHECK(shown()->payload.books.page_index == 0U);
    CHECK(shown()->payload.books.page_count == 0U);
    CHECK(ui_status_bar_get_state().center_kind == status_bar_center_kind::page);
    CHECK(ui_status_bar_get_state().center_current_page == 0U);
    CHECK(ui_status_bar_get_state().center_total_pages == 0U);

    const auto settings = books_settings_rect();
    click(settings.left + settings.width / 2, settings.top + settings.height / 2);
    CHECK(shown()->payload.books.settings_visible);
    CHECK(shown()->payload.books.pending_settings.auto_scan_txt);
    CHECK(!shown()->payload.books.pending_settings.auto_scan_epub);
    const auto epub = books_setting_row_rect(true);
    click(epub.left + 4, epub.top + 4);
    CHECK(shown()->payload.books.pending_settings.auto_scan_epub);
    const auto cancel = books_setting_cancel_rect();
    click(cancel.left + 4, cancel.top + 4);
    CHECK(!shown()->payload.books.settings_visible);
    click(settings.left + settings.width / 2, settings.top + settings.height / 2);
    CHECK(!shown()->payload.books.pending_settings.auto_scan_epub);
    click(epub.left + 4, epub.top + 4);
    const auto confirm = books_setting_confirm_rect();
    click(confirm.left + 4, confirm.top + 4);
    click(settings.left + settings.width / 2, settings.top + settings.height / 2);
    CHECK(shown()->payload.books.pending_settings.auto_scan_epub);
    click(cancel.left + 4, cancel.top + 4);

    const auto back = books_back_rect();
    click(back.left + back.width / 2, back.top + back.height / 2);
    wait_view(ui_view_id::launcher);
    const auto launcher_menu = launcher_entry_rect(2U);
    click(
        launcher_menu.left + launcher_menu.width / 2,
        launcher_menu.top + launcher_menu.height / 2);
    wait_view(ui_view_id::menu);
    CHECK(shown()->payload.menu.entry_count == 4U);
    CHECK(std::strcmp(shown()->payload.menu.entries[0].label, "Screen Setting") == 0);
    const auto child = menu_entry_rect(0U);
    click(child.left + child.width / 2, child.top + child.height / 2);
    wait_view(ui_view_id::test);
    const auto child_back = test_back_button_rect();
    click(
        child_back.left + child_back.width / 2,
        child_back.top + child_back.height / 2);
    wait_view(ui_view_id::menu);
    const auto menu_back = menu_back_button_rect();
    click(
        menu_back.left + menu_back.width / 2,
        menu_back.top + menu_back.height / 2);
    wait_view(ui_view_id::launcher);
    CHECK(ui_status_bar_set_center_text("Context"));
    CHECK(ui_status_bar_get_state().center_kind == status_bar_center_kind::text);
    CHECK(std::strcmp(ui_status_bar_get_state().center_text, "Context") == 0);
    CHECK(ui_status_bar_clear_center());
    CHECK(ui_status_bar_get_state().center_kind == status_bar_center_kind::none);
    std::puts("PASS App navigation: Launcher -> Books -> back, Launcher -> Menu -> child -> back");
}

void test_catalog_service_persistence()
{
    const auto generation = storage_service_get_media_generation();
    CHECK(book_catalog_service_activate(generation));
    book_catalog_snapshot snapshot = {};
    until([&] {
        return book_catalog_service_snapshot(snapshot) &&
               snapshot.media_generation == generation &&
               snapshot.state == book_catalog_state::ready &&
               snapshot.item_count == 1U;
    });
    book_catalog_item item = {};
    std::size_t copied = 0U;
    CHECK(book_catalog_service_copy_page(generation, 0U, &item, 1U, copied));
    CHECK(copied == 1U && std::strcmp(item.path, "/books/book.TXT") == 0);
    CHECK(item.format == book_file_format::txt);
    CHECK(item.preview_state == book_catalog_preview_state::ready);
    CHECK(std::strlen(item.preview) < BOOK_CATALOG_PREVIEW_CAPACITY);
    CHECK(system_files.count("/.system/books/catalog_v1.bin") == 1U);

    CHECK(book_catalog_service_update_settings(generation, {false, false}));
    until([&] {
        return book_catalog_service_snapshot(snapshot) &&
               snapshot.state == book_catalog_state::ready &&
               !snapshot.settings.auto_scan_txt &&
               !snapshot.settings.auto_scan_epub && snapshot.item_count == 0U;
    });
    book_catalog_service_pause();
    until([] { return book_catalog_service_idle(); });

    set_card(false);
    set_card(true);
    const auto reloaded_generation = storage_service_get_media_generation();
    CHECK(reloaded_generation != generation);
    CHECK(book_catalog_service_activate(reloaded_generation));
    until([&] {
        return book_catalog_service_snapshot(snapshot) &&
               snapshot.media_generation == reloaded_generation &&
               snapshot.state == book_catalog_state::ready &&
               !snapshot.settings.auto_scan_txt &&
               !snapshot.settings.auto_scan_epub && snapshot.item_count == 0U;
    });
    CHECK(book_catalog_service_update_settings(reloaded_generation, {true, false}));
    until([&] {
        return book_catalog_service_snapshot(snapshot) &&
               snapshot.state == book_catalog_state::ready &&
               snapshot.settings.auto_scan_txt && snapshot.item_count == 1U;
    });
    book_catalog_service_pause();
    until([] { return book_catalog_service_idle(); });
    std::puts("PASS BookCatalogService: recursive discovery fixture, bounded TXT preview, atomic catalog, settings reload, media generation");
}

void test_books_reader_return()
{
    app_request_switch(app_kind::books);
    wait_view(ui_view_id::books);
    until([] {
        return shown()->view == ui_view_id::books &&
               shown()->payload.books.item_count == 1U &&
               shown()->payload.books.page_count == 1U;
    });
    const auto item = books_item_rect(0U);
    click(item.left + item.width / 2, item.top + item.height / 2);
    wait_reader(reader_view_status::ready);
    CHECK(shown()->payload.reader.page.current_page_start_offset > 0U);
    reader_back();
    wait_view(ui_view_id::books);
    CHECK(shown()->payload.books.page_index == 0U);
    CHECK(shown()->payload.books.item_count == 1U);
    CHECK(ui_status_bar_get_state().center_current_page == 1U);
    CHECK(ui_status_bar_get_state().center_total_pages == 1U);

    set_card(false);
    until([] {
        return shown()->view == ui_view_id::books &&
               shown()->payload.books.item_count == 0U &&
               shown()->payload.books.page_count == 0U;
    });
    CHECK(ui_status_bar_get_state().center_current_page == 0U);
    CHECK(ui_status_bar_get_state().center_total_pages == 0U);
    set_card(true);
    until([] {
        return shown()->view == ui_view_id::books &&
               shown()->payload.books.item_count == 1U;
    });
    const auto back = books_back_rect();
    click(back.left + back.width / 2, back.top + back.height / 2);
    wait_file("/");
    std::puts("PASS Books integration: scan -> Reader restore -> Books return, page preservation, SD removal/reload");
}

void test_reader_flow()
{
    book.clear();
    for (int i = 0; i < 150; ++i) { book += "中文mixed text 0123456789\r\n"; }
    CHECK(storage_service_init() == ESP_OK);
    CHECK(book_service_init() == ESP_OK);
    CHECK(book_catalog_service_init() == ESP_OK);
    CHECK(app_init() == ESP_OK);
    pump();
    test_app_navigation();
    set_card(true);
    test_catalog_service_persistence();
    app_request_switch(app_kind::file);
    wait_file("/");
    click(80, 240); // root row 1: books directory
    wait_file("/books");
    click(80, 240); // row 1: book.TXT
    wait_reader(reader_view_status::ready);
    CHECK(shown()->payload.reader.page.current_page_start_offset == 0);
    test_reader_menu();
    auto feedback_before = control_feedback;
    auto frames_before = frame_submissions;
    click(400, 360);
    wait_changed(0);
    CHECK(control_feedback == feedback_before);
    CHECK(frame_submissions >= frames_before + 1); // index readiness can also update persistence state
    const auto saved = shown()->payload.reader.page.current_page_start_offset;
    CHECK(saved > 0);
    click(80, 360);
    wait_changed(saved);
    CHECK(shown()->payload.reader.page.current_page_start_offset == 0);
    click(400, 360);
    wait_changed(0);
    reader_back();
    wait_file("/books");
    click(80, 240);
    wait_reader(reader_view_status::ready);
    CHECK(!shown()->payload.reader.menu_visible);
    until([=] { return shown()->payload.reader.status == reader_view_status::ready &&
                       shown()->payload.reader.page.current_page_start_offset == saved; });
    click(80, 360); // fresh session: rescan to previous page
    wait_changed(saved);
    CHECK(shown()->payload.reader.page.current_page_start_offset == 0);

    // Advance beyond the bounded history and then walk back through a rebuild.
    std::vector<std::uint64_t> offsets{0};
    for (unsigned i = 0; i < 70; ++i) {
        const auto previous = shown()->payload.reader.page.current_page_start_offset;
        CHECK(shown()->payload.reader.next_enabled);
        click(400, 360); wait_changed(previous);
        offsets.push_back(shown()->payload.reader.page.current_page_start_offset);
    }
    for (unsigned i = 70; i > 0; --i) {
        click(80, 360); wait_changed(offsets[i]);
        CHECK(shown()->payload.reader.page.current_page_start_offset == offsets[i - 1]);
    }
    until([] {
        return ui_status_bar_get_state().center_kind ==
               status_bar_center_kind::page;
    });
    CHECK(ui_status_bar_get_state().center_current_page == 1U);
    CHECK(ui_status_bar_get_state().center_total_pages > 70U);
    const auto status_before = ui_status_bar_get_state();
    click(240, 360);
    CHECK(shown()->payload.reader.menu_visible);
    CHECK(ui_status_bar_get_state().center_current_page ==
          status_before.center_current_page);
    CHECK(ui_status_bar_get_state().center_total_pages ==
          status_before.center_total_pages);
    click(240, 360);
    CHECK(ui_status_bar_get_state().center_current_page ==
          status_before.center_current_page);
    CHECK(ui_status_bar_get_state().center_total_pages ==
          status_before.center_total_pages);
    app_event stale_book{};
    stale_book.type = app_event_type::book;
    stale_book.book.type = book_event_type::ready;
    stale_book.book.session_id = UINT32_MAX;
    stale_book.book.media_generation = storage_service_get_media_generation();
    stale_book.book.index_valid = true;
    stale_book.book.page_count = 999U;
    app_dispatch_event(stale_book);
    CHECK(ui_status_bar_get_state().center_total_pages ==
          status_before.center_total_pages);

    // Toggle during an in-flight page: show the stable body, then accept the new
    // completed page under the same menu. No partial paginator contents escape.
    pause_reads = true;
    const auto before_loading = shown()->payload.reader;
    click(400, 360);
    until([] { return in_read.load(); });
    click(240, 360);
    CHECK(shown()->payload.reader.menu_visible);
    CHECK(reader_body_matches(before_loading, shown()->payload.reader));
    pause_reads = false; xSemaphoreGive(read_gate);
    wait_changed(before_loading.page.current_page_start_offset);
    CHECK(shown()->payload.reader.menu_visible);
    click(240, 360);

    // A published page is not a committed hit-test frame.
    defer_display = true;
    click(400, 360);
    until([] { return ui_frame_handle_is_valid(pending); });
    CHECK(!ui_presentation_input_ready(ui_view_id::reader));
    input_event press{}; press.gesture = input_gesture_type::press;
    press.start_x = 400; press.start_y = 720;
    ui_action_event action{};
    CHECK(!ui_interaction_process(press, action));
    defer_display = false; commit_display();
    CHECK(ui_presentation_input_ready(ui_view_id::reader));
    {
        ui_presentation_read_guard guard(ui_view_id::reader);
        CHECK(guard.valid());
        const auto old = guard.reader_view()->page.current_page_start_offset;
        click(400, 360); wait_changed(old);
        CHECK(guard.reader_view()->page.current_page_start_offset == old);
    }
    reader_back(); wait_file("/books");
    CHECK(ui_status_bar_get_state().center_kind == status_bar_center_kind::none);
    click(60, 44); pump();
    // Actual Runtime returned to Menu, so File can be opened normally again.
    app_request_switch(app_kind::file); wait_file("/");
    test_books_reader_return();
    std::puts("PASS reader lifecycle: Files launch, next/previous, 64-page history overflow, restore, return navigation, presentation ownership");
}

void test_storage_lifecycle()
{
    open_reader();
    const auto old_generation = storage_service_get_media_generation();
    pause_reads = true;
    CHECK(storage_service_read_file_chunk("/books/book.TXT", 0, 9001, 8001, old_generation));
    until([] { return in_read.load(); }, false);
    inserted = false;
    for (unsigned i = 0; i < 10U; ++i) { pump(false); }
    CHECK(in_read); // monitor did not unmount under the locked read
    pause_reads = false;
    xSemaphoreGive(read_gate);
    until([] { return storage_service_get_state() == storage_state::no_card; }, false);
    storage_result_event late{};
    until([&] { return storage_service_try_get_result_event(late); }, false);
    const storage_file_chunk_result* result = nullptr;
    CHECK(storage_service_resolve_file_result(late.handle, result));
    CHECK(result->code == storage_result_code::cancelled);
    CHECK(result->media_generation == old_generation);
    wait_reader(reader_view_status::no_card);
    set_card(true);
    CHECK(shown()->payload.reader.status == reader_view_status::no_card);
    CHECK(!storage_service_read_file_chunk("/books/book.TXT", 0, 1, 1, old_generation));
    reader_back(); wait_file("/");
    open_reader();
    const auto position = shown()->payload.reader.page.current_page_start_offset;
    app_event late_event{};
    late_event.type = app_event_type::storage_result;
    late_event.storage_result.handle = late.handle;
    app_dispatch_event(late_event);
    CHECK(shown()->payload.reader.page.current_page_start_offset == position);
    const auto stale = late.handle;
    CHECK(storage_service_release_result(late.handle));
    CHECK(!storage_service_resolve_file_result(stale, result));

    // Delay a real Reader result, close/reopen, then deliver its old session.
    click(400, 360); // request queued by on_running
    storage_result_event previous_session{};
    until([&] { return storage_service_try_get_result_event(previous_session); }, false);
    app_request_back(); pump(false); wait_file("/");
    open_reader();
    const auto restored = shown()->payload.reader.page.current_page_start_offset;
    late_event.storage_result.handle = previous_session.handle;
    app_dispatch_event(late_event);
    CHECK(shown()->payload.reader.page.current_page_start_offset == restored);
    CHECK(storage_service_release_result(previous_session.handle));

    // Deliberately suppress dispatch to exercise the dropped-result timeout.
    click(400, 360);
    extra_time += 10001U;
    pump(false);
    CHECK(shown()->payload.reader.status == reader_view_status::storage_error);
    pump();
    CHECK(shown()->payload.reader.status == reader_view_status::storage_error);
    reader_back(); wait_file("/");
    std::puts("PASS storage lifecycle: removal during read, reinsertion latch, stale generation/session, released handles, request timeout");
}

void test_progress_and_errors()
{
    // Changing content and mtime invalidates its previous page mapping.
    book.replace(0U, 3U, "abc");
    modified_time = 101;
    open_reader();
    CHECK(shown()->payload.reader.page.current_page_start_offset == 0);
    reader_back(); wait_file("/");
    book.clear(); modified_time = 102;
    CHECK(request_reader("/books/book.TXT", storage_service_get_media_generation(), book_file_format::txt));
    pump(); wait_reader(reader_view_status::empty_file);
    reader_back(); wait_file("/");
    book = "\xef\xbb\xbf"; modified_time = 103;
    CHECK(request_reader("/books/book.TXT", storage_service_get_media_generation(), book_file_format::txt));
    pump(); wait_reader(reader_view_status::empty_file);
    reader_back(); wait_file("/");
    book = "\n\n\n"; modified_time = 104;
    open_reader();
    CHECK(!shown()->payload.reader.page.empty);
    reader_back(); wait_file("/");
    book = "\xff\xfe"; modified_time = 105;
    CHECK(request_reader("/books/book.TXT", storage_service_get_media_generation(), book_file_format::txt));
    pump(); wait_reader(reader_view_status::invalid_utf8);
    reader_back(); wait_file("/");
    CHECK(request_reader("/missing.txt", storage_service_get_media_generation(), book_file_format::txt));
    pump(); wait_reader(reader_view_status::file_not_found);
    reader_back(); wait_file("/");

    std::puts("PASS errors: empty/BOM/invalid/missing and changed content");
}

void test_epub_reader_flow()
{
    epub_book = make_epub_fixture(true, 'a');
    epub_modified_time = 200;
    CHECK(request_reader("/books/book.epub", storage_service_get_media_generation(),
                                  book_file_format::epub));
    pump();
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      shown()->payload.reader.showing_cover; });
    CHECK(shown()->payload.reader.next_enabled);
    click(400, 360);
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      !shown()->payload.reader.showing_cover &&
                      shown()->payload.reader.page.current_page_start_offset == 0U; });

    bool crossed_spine = false;
    for (unsigned page = 0U; page < 100U; ++page) {
        if (flatten(shown()->payload.reader.page).find("World") != std::string::npos) {
            crossed_spine = true;
            break;
        }
        CHECK(shown()->payload.reader.next_enabled);
        const auto previous = shown()->payload.reader.page.current_page_start_offset;
        click(400, 360);
        wait_changed(previous);
    }
    CHECK(crossed_spine);
    const auto saved = shown()->payload.reader.page.current_page_start_offset;
    CHECK(saved > 0U);
    reader_back(); wait_file("/");

    CHECK(request_reader("/books/book.epub", storage_service_get_media_generation(),
                                  book_file_format::epub));
    pump();
    until([=] { return shown()->view == ui_view_id::reader &&
                        shown()->payload.reader.status == reader_view_status::ready &&
                        !shown()->payload.reader.showing_cover &&
                        shown()->payload.reader.page.current_page_start_offset == saved; });

    // A resumed body session loads page 0 lazily when the user turns back from
    // the first body page. The cover is logical page 0 and hides the page count.
    for (unsigned page = 0U;
         page < 100U && shown()->payload.reader.page.current_page_start_offset != 0U;
         ++page) {
        const auto previous = shown()->payload.reader.page.current_page_start_offset;
        CHECK(shown()->payload.reader.previous_enabled);
        click(80, 360);
        wait_changed(previous);
    }
    CHECK(shown()->payload.reader.page.current_page_start_offset == 0U);
    until([] {
        return ui_status_bar_get_state().center_kind ==
               status_bar_center_kind::page;
    });
    click(80, 360);
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      shown()->payload.reader.showing_cover; });
    CHECK(ui_status_bar_get_state().center_kind == status_bar_center_kind::none);
    reader_back(); wait_file("/");

    // Closing on the cover restores the cover. Closing on body page 1 restores
    // body page 1 instead of inferring the cover from byte offset zero.
    CHECK(request_reader("/books/book.epub", storage_service_get_media_generation(),
                                  book_file_format::epub));
    pump();
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      shown()->payload.reader.showing_cover; });
    click(400, 360);
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      !shown()->payload.reader.showing_cover &&
                      shown()->payload.reader.page.current_page_start_offset == 0U; });
    reader_back(); wait_file("/");
    CHECK(request_reader("/books/book.epub", storage_service_get_media_generation(),
                                  book_file_format::epub));
    pump();
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      !shown()->payload.reader.showing_cover &&
                      shown()->payload.reader.page.current_page_start_offset == 0U; });
    reader_back(); wait_file("/");

    // A changed source with the same size and FAT timestamp ignores old progress.
    epub_book = make_epub_fixture(true, 'b');
    CHECK(request_reader("/books/book.epub", storage_service_get_media_generation(),
                                  book_file_format::epub));
    pump();
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      shown()->payload.reader.showing_cover; });
    click(400, 360);
    until([] { return shown()->view == ui_view_id::reader &&
                      shown()->payload.reader.status == reader_view_status::ready &&
                      !shown()->payload.reader.showing_cover &&
                      shown()->payload.reader.page.current_page_start_offset == 0U; });
    reader_back(); wait_file("/");
    std::puts("PASS EPUB Reader flow: page-0 cover, lazy back navigation, exact restore, changed-source reset");
}

void test_last_page()
{
    book = "short text"; modified_time = 106;
    open_reader();
    CHECK(!shown()->payload.reader.menu_visible);
    CHECK(shown()->payload.reader.page.current_page_start_offset == 0);
    CHECK(!shown()->payload.reader.previous_enabled && !shown()->payload.reader.next_enabled);
    const auto frames = frame_submissions;
    ignored_reader_click(80, 720);
    ignored_reader_click(400, 720);
    CHECK(frame_submissions == frames);
    reader_back(); wait_file("/");
    std::puts("PASS page bounds: one-page book cannot underflow/overflow; Back returns to Files");
}
} // namespace

// Board adapters. The lock lifetime matches the production chunk HAL contract.
struct hal_storage_directory { unsigned index; bool root; };
bool hal_storage_card_inserted(bool& value) { value = inserted.load(); return true; }
esp_err_t hal_storage_mount() { mounted = true; return ESP_OK; }
esp_err_t hal_storage_unmount()
{
    if (xSemaphoreTake(filesystem_lock, pdMS_TO_TICKS(2)) != pdTRUE) { return ESP_ERR_TIMEOUT; }
    mounted = false; xSemaphoreGive(filesystem_lock); return ESP_OK;
}
esp_err_t hal_storage_open_directory(const char* path, hal_storage_directory*& directory)
{
    if (!mounted) { return ESP_ERR_INVALID_STATE; }
    directory = new hal_storage_directory{0U, std::strcmp(path, "/") == 0};
    return ESP_OK;
}
esp_err_t hal_storage_read_directory(hal_storage_directory* directory, hal_storage_entry& entry, bool& end)
{
    const auto row = directory->index++;
    if (directory->root && row == 0U) {
        end = false; std::strcpy(entry.name, ".system"); entry.directory = true; entry.size = 0U; return ESP_OK;
    }
    end = row >= (directory->root ? 2U : 1U);
    if (!end) {
        std::strcpy(entry.name, directory->root ? "books" : "book.TXT");
        entry.directory = directory->root; entry.size = book.size();
    }
    return ESP_OK;
}
void hal_storage_close_directory(hal_storage_directory*& directory) { delete directory; directory = nullptr; }
esp_err_t hal_storage_read_file_chunk(const char* path, std::uint64_t offset, char* data,
    std::size_t capacity, std::size_t& length, std::uint64_t& size, std::int64_t& time)
{
    CHECK(xSemaphoreTake(filesystem_lock, portMAX_DELAY) == pdTRUE);
    if (!mounted) { xSemaphoreGive(filesystem_lock); return ESP_ERR_INVALID_STATE; }
    const bool foreground = std::strcmp(pcTaskGetName(nullptr), "storage_worker") == 0;
    if (foreground) { in_read = true; }
    if (foreground && pause_reads) { xSemaphoreTake(read_gate, portMAX_DELAY); }
    esp_err_t error = ESP_OK;
    if (std::strncmp(path, "/.system/", 9U) == 0) {
        auto item = system_files.find(path);
        if (item == system_files.end()) { error = ESP_ERR_NOT_FOUND; }
        else if (offset > item->second.size()) { error = ESP_ERR_INVALID_SIZE; }
        else {
            size = item->second.size(); time = 1;
            length = std::min<std::size_t>(capacity, size - offset);
            std::memcpy(data, item->second.data() + offset, length);
        }
    }
    else if (std::strcmp(path, "/books/book.TXT") != 0 &&
             std::strcmp(path, "/books/book.epub") != 0) { error = ESP_ERR_NOT_FOUND; }
    else {
        const bool epub = std::strcmp(path, "/books/book.epub") == 0;
        const auto& source = epub ? epub_book : book;
        if (offset > source.size()) { xSemaphoreGive(filesystem_lock); return ESP_ERR_INVALID_SIZE; }
        size = source.size(); time = epub ? epub_modified_time.load() : modified_time.load();
        length = std::min<std::size_t>(capacity, size - offset);
        std::memcpy(data, source.data() + offset, length);
    }
    if (foreground) { in_read = false; }
    xSemaphoreGive(filesystem_lock); return error;
}

esp_err_t hal_storage_ensure_system_directory(const char*) { return mounted ? ESP_OK : ESP_ERR_INVALID_STATE; }
esp_err_t hal_storage_write_system_file(const char* path, std::uint64_t offset,
    const void* data, std::size_t length, bool truncate)
{
    CHECK(xSemaphoreTake(filesystem_lock, portMAX_DELAY) == pdTRUE);
    if (!mounted) { xSemaphoreGive(filesystem_lock); return ESP_ERR_INVALID_STATE; }
    auto& file = system_files[path];
    if (truncate) { file.clear(); }
    file.resize(std::max<std::size_t>(file.size(), offset + length));
    std::memcpy(file.data() + offset, data, length);
    xSemaphoreGive(filesystem_lock); return ESP_OK;
}
esp_err_t hal_storage_replace_system_file(const char* from, const char* to)
{
    CHECK(xSemaphoreTake(filesystem_lock, portMAX_DELAY) == pdTRUE);
    if (!mounted) { xSemaphoreGive(filesystem_lock); return ESP_ERR_INVALID_STATE; }
    auto found = system_files.find(from);
    if (found == system_files.end()) { xSemaphoreGive(filesystem_lock); return ESP_ERR_NOT_FOUND; }
    system_files[to] = std::move(found->second); system_files.erase(found);
    xSemaphoreGive(filesystem_lock); return ESP_OK;
}
void ui_renderer_notify_status_bar() {}
bool system_tick_service_register_task(TaskHandle_t handle, std::uint32_t) { monitor_task = handle; return true; }
std::uint32_t system_tick_now_ms() { return esp_timer_get_time() / 1000 + extra_time; }
text_layout_profile ui_reader_text_layout() { return test_layout; }
text_layout_profile ui_file_name_text_layout() { return {32U, 1U, measure}; }
text_layout_profile ui_books_file_name_text_layout() { return {12U, 2U, measure}; }
text_layout_profile ui_books_preview_text_layout() { return {10U, 6U, measure}; }

template<typename state_type>
bool submit_test_view(
    ui_view_id id,
    ui_update_reason reason,
    const state_type& state)
{
    ui_frame_handle handle = invalid_ui_frame_handle();
    display_request* frame = nullptr;
    if (!ui_frame_pool_acquire(handle, frame)) { return false; }
    frame->view = id;
    frame->view_generation = ui_presentation_prepare_frame(
        id, reason == ui_update_reason::view_opened);
    if constexpr (std::is_same_v<state_type, launcher_view_state>) {
        frame->payload.launcher = state;
    } else if constexpr (std::is_same_v<state_type, books_view_state>) {
        frame->payload.books = state;
    } else if constexpr (std::is_same_v<state_type, menu_view_state>) {
        frame->payload.menu = state;
    } else if constexpr (std::is_same_v<state_type, test_view_state>) {
        frame->payload.test = state;
    }
    CHECK(ui_frame_pool_publish(handle));
    CHECK(!ui_frame_handle_is_valid(pending) || ui_frame_pool_release(pending));
    pending = handle;
    ++frame_submissions;
    return true;
}

bool ui_render_launcher(
    const launcher_view_state& state,
    ui_update_reason reason)
{
    return submit_test_view(ui_view_id::launcher, reason, state);
}

bool ui_render_books(
    const books_view_state& state,
    ui_update_reason reason,
    ui_control_type)
{
    return submit_test_view(ui_view_id::books, reason, state);
}

bool ui_render_menu(const menu_view_state& state, ui_update_reason reason)
{
    return submit_test_view(ui_view_id::menu, reason, state);
}

bool ui_render_test(const test_view_state& state, ui_update_reason reason)
{
    return submit_test_view(ui_view_id::test, reason, state);
}

template<typename writer_type>
bool submit_test_frame(ui_view_id id, ui_update_reason reason, writer_type writer, const void* context)
{
    if (id == ui_view_id::reader && reject_reader_frames) { return false; }
    ui_frame_handle handle = invalid_ui_frame_handle(); display_request* frame = nullptr;
    if (!ui_frame_pool_acquire(handle, frame)) { return false; }
    frame->view = id;
    frame->view_generation = ui_presentation_prepare_frame(id, reason == ui_update_reason::view_opened);
    bool written;
    if constexpr (std::is_same_v<writer_type, file_frame_writer>) { written = writer(frame->payload.file, context); }
    else { written = writer(frame->payload.reader, context); }
    CHECK(written && ui_frame_pool_publish(handle));
    CHECK(!ui_frame_handle_is_valid(pending) || ui_frame_pool_release(pending));
    pending = handle; ++frame_submissions;
    return true;
}
bool ui_write_file_frame(ui_update_reason reason, file_frame_writer writer, const void* context)
{ return submit_test_frame(ui_view_id::file, reason, writer, context); }
bool ui_write_reader_frame(ui_update_reason reason, reader_frame_writer writer, const void* context)
{ return submit_test_frame(ui_view_id::reader, reason, writer, context); }
bool ui_render_control(ui_view_id, ui_control_type, std::uint8_t, bool)
{ ++control_feedback; return true; }

const app_descriptor* app_descriptor_find(app_kind kind)
{
    for (const auto& descriptor : descriptors) { if (descriptor.kind == kind) { return &descriptor; } }
    return nullptr;
}
app_record* app_registry_find(app_kind kind)
{
    for (auto& record : test_records) { if (record.kind == kind) { return &record; } }
    return nullptr;
}
bool app_registry_install_all(mooncake::Mooncake& runtime)
{
    std::unique_ptr<app_base> apps[6];
    apps[0] = std::make_unique<launcher_app>();
    apps[1] = std::make_unique<books_app>();
    apps[2] = std::make_unique<menu_app>();
    apps[3] = std::make_unique<test_child>();
    apps[4] = std::make_unique<file_app>();
    apps[5] = std::make_unique<reader_app>();
    for (unsigned i = 0; i < 6; ++i) {
        test_records[i].kind = descriptors[i].kind;
        test_records[i].instance = apps[i].get();
        test_records[i].mooncake_id = runtime.installApp(std::move(apps[i]));
    }
    return true;
}

extern "C" void app_main()
{
    std::printf("SIZES reader_app=%u reader_page=%u reader_view=%u display_request=%u storage_chunk=%u\n",
        unsigned(sizeof(reader_app)), unsigned(sizeof(reader_page)), unsigned(sizeof(reader_view_state)),
        unsigned(sizeof(display_request)), unsigned(sizeof(storage_file_chunk_result)));
    test_launch_context();
    test_reader_rendering();
    test_gray4_support();
    test_books_ui_support();
    test_pagination(); test_names();
    test_book_formats_and_engine(); test_epub_support();
    filesystem_lock = xSemaphoreCreateMutex(); read_gate = xSemaphoreCreateBinary();
    test_reader_flow(); test_epub_reader_flow(); test_storage_lifecycle(); test_progress_and_errors(); test_last_page();
    const auto stats = ui_frame_pool_get_stats();
    CHECK(stats.invalid_transition_count == 0);
    std::printf("FRAME_POOL active=%u peak=%u invalid=%lu\n", stats.active_count, stats.peak_active_count,
                static_cast<unsigned long>(stats.invalid_transition_count));
    std::puts("ALL_READER_TESTS_PASSED"); std::fflush(stdout);
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}

