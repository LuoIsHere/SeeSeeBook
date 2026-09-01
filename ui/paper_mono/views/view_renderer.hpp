#pragma once

#include <cstdint>

#include "battery_view.hpp"
#include "display.hpp"
#include "file_view.hpp"
#include "menu_view.hpp"
#include "rtc_view.hpp"
#include "test_view.hpp"

namespace paper_mono_views {

void draw_menu_view(
    display_surface& surface,
    const menu_view_state& state);
void draw_menu_entry(
    display_surface& surface,
    const menu_view_state& state,
    std::uint8_t index,
    bool pressed);

void draw_test_view(
    display_surface& surface,
    const test_view_state& state,
    std::uint8_t selected_light,
    std::int16_t pressed_light);
void draw_front_light_bar(
    display_surface& surface,
    std::uint8_t selected,
    std::int16_t pressed);
void draw_test_content(
    display_surface& surface,
    const test_view_state& state);

void draw_rtc_view(
    display_surface& surface,
    const rtc_view_state& state);
void draw_rtc_editor(
    display_surface& surface,
    const rtc_view_state& state);
void draw_rtc_key(
    display_surface& surface,
    std::uint8_t index,
    bool pressed,
    bool enabled);
bool rtc_keys_enabled(const rtc_view_state& state);

void draw_battery_view(
    display_surface& surface,
    const battery_view_state& state);
void draw_battery_content(
    display_surface& surface,
    const battery_view_state& state);

void draw_file_view(
    display_surface& surface,
    const file_view_state& state);
void draw_file_content(
    display_surface& surface,
    const file_view_state& state);
void draw_file_row(
    display_surface& surface,
    const file_view_state& state,
    std::uint8_t index,
    bool pressed);
void draw_file_page_button(
    display_surface& surface,
    const file_view_state& state,
    bool next,
    bool pressed);

}  // namespace paper_mono_views
