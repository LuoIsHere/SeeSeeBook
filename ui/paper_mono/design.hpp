#pragma once

#include <cstdint>

namespace paper_ui {

inline constexpr std::int16_t space_xs = 4;
inline constexpr std::int16_t space_sm = 8;
inline constexpr std::int16_t space_md = 12;
inline constexpr std::int16_t space_lg = 16;
inline constexpr std::int16_t space_xl = 24;

inline constexpr std::int16_t page_margin = 24;
inline constexpr std::int16_t control_padding = 8;
inline constexpr std::int16_t list_padding = 12;

inline constexpr std::int16_t radius_small = 4;
inline constexpr std::int16_t radius_control = 8;
inline constexpr std::int16_t radius_card = 8;
inline constexpr std::int16_t radius_dialog = 12;

inline constexpr std::int16_t stroke_normal = 1;
inline constexpr std::int16_t stroke_focus = 2;

inline constexpr std::uint8_t text_caption = 1U;
inline constexpr std::uint8_t text_body = 2U;
inline constexpr std::uint8_t text_title = 3U;
inline constexpr std::uint8_t text_display = 4U;

inline constexpr std::int16_t icon_small = 16;
inline constexpr std::int16_t icon_medium = 24;
inline constexpr std::int16_t icon_large = 32;
inline constexpr std::int16_t icon_stroke = 2;

}  // namespace paper_ui
