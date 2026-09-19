#pragma once

#include <cstdint>

#include "books_view.hpp"
#include "design.hpp"
#include "geometry.hpp"
#include "layout.hpp"

inline constexpr std::int16_t BOOKS_TOOLBAR_HEIGHT = 80;
inline constexpr std::int16_t BOOKS_TOOLBAR_BUTTON_WIDTH = 80;
inline constexpr std::int16_t BOOKS_TOOLBAR_TITLE_CENTER_Y = 40;
inline constexpr std::uint8_t BOOKS_TOOLBAR_TEXT_SIZE = 3U;

inline constexpr std::uint8_t BOOKS_GRID_COLUMNS = 3U;
inline constexpr std::uint8_t BOOKS_GRID_ROWS = 2U;
inline constexpr std::int16_t BOOKS_GRID_LEFT = 12;
inline constexpr std::int16_t BOOKS_GRID_TOP = 96;
inline constexpr std::int16_t BOOKS_GRID_COLUMN_GAP = 12;
inline constexpr std::int16_t BOOKS_GRID_CELL_WIDTH = 144;

inline constexpr std::int16_t BOOKS_COVER_WIDTH = 120;
inline constexpr std::int16_t BOOKS_COVER_HEIGHT = 180;
inline constexpr std::int16_t BOOKS_CARD_TOP_PADDING = paper_ui::space_sm;
inline constexpr std::int16_t BOOKS_CARD_BOTTOM_PADDING = paper_ui::space_sm;
inline constexpr std::int16_t BOOKS_FILE_NAME_SIDE_PADDING = paper_ui::space_sm;
inline constexpr std::int16_t BOOKS_FILE_NAME_TOP_GAP = paper_ui::space_sm;
inline constexpr std::int16_t BOOKS_FILE_NAME_HEIGHT = 48;
inline constexpr std::int16_t BOOKS_FILE_NAME_LINE_HEIGHT = 24;
inline constexpr std::uint8_t BOOKS_FILE_NAME_TEXT_SIZE = paper_ui::text_caption;
inline constexpr std::int16_t BOOKS_FILE_NAME_TEXT_INSET = paper_ui::space_xs;
inline constexpr std::int16_t BOOKS_FILE_NAME_TEXT_WIDTH =
    BOOKS_GRID_CELL_WIDTH - BOOKS_FILE_NAME_SIDE_PADDING * 2 -
    BOOKS_FILE_NAME_TEXT_INSET * 2;
inline constexpr std::int16_t BOOKS_CARD_HEIGHT =
    BOOKS_CARD_TOP_PADDING + BOOKS_COVER_HEIGHT + BOOKS_FILE_NAME_TOP_GAP +
    BOOKS_FILE_NAME_HEIGHT + BOOKS_CARD_BOTTOM_PADDING;
inline constexpr std::int16_t BOOKS_GRID_ROW_GAP = 40;
inline constexpr std::int16_t BOOKS_GRID_SLOT_HEIGHT =
    BOOKS_CARD_HEIGHT + BOOKS_GRID_ROW_GAP;
inline constexpr std::int16_t BOOKS_HIT_VERTICAL_EXPANSION = paper_ui::space_sm;
inline constexpr std::int16_t BOOKS_TYPE_LABEL_WIDTH = 44;
inline constexpr std::int16_t BOOKS_TYPE_LABEL_HEIGHT = 24;
inline constexpr std::uint8_t BOOKS_TYPE_LABEL_TEXT_SIZE = paper_ui::text_caption;
inline constexpr std::int16_t BOOKS_PREVIEW_MARGIN = 6;
inline constexpr std::int16_t BOOKS_PREVIEW_LINE_HEIGHT = 26;

inline constexpr std::int16_t BOOKS_PAGER_TOP = 680;
inline constexpr std::int16_t BOOKS_PAGER_HEIGHT = STATUS_BAR_TOP - BOOKS_PAGER_TOP;
inline constexpr std::int16_t BOOKS_PAGER_BUTTON_WIDTH = 92;
inline constexpr std::uint8_t BOOKS_PAGER_TEXT_SIZE = 3U;

inline constexpr std::int16_t BOOKS_MODAL_LEFT = 40;
inline constexpr std::int16_t BOOKS_MODAL_TOP = 190;
inline constexpr std::int16_t BOOKS_MODAL_WIDTH = 400;
inline constexpr std::int16_t BOOKS_MODAL_HEIGHT = 360;
inline constexpr std::int16_t BOOKS_MODAL_TITLE_CENTER_Y = 230;
inline constexpr std::uint8_t BOOKS_MODAL_TITLE_TEXT_SIZE = 3U;
inline constexpr std::int16_t BOOKS_MODAL_ROW_LEFT = 72;
inline constexpr std::int16_t BOOKS_MODAL_ROW_WIDTH = 336;
inline constexpr std::int16_t BOOKS_MODAL_ROW_HEIGHT = 64;
inline constexpr std::int16_t BOOKS_MODAL_TXT_ROW_TOP = 270;
inline constexpr std::int16_t BOOKS_MODAL_EPUB_ROW_TOP = 346;
inline constexpr std::int16_t BOOKS_CHECKBOX_SIZE = 28;
inline constexpr std::uint8_t BOOKS_MODAL_TEXT_SIZE = 2U;
inline constexpr std::int16_t BOOKS_MODAL_BUTTON_TOP = 470;
inline constexpr std::int16_t BOOKS_MODAL_BUTTON_WIDTH = 120;
inline constexpr std::int16_t BOOKS_MODAL_BUTTON_HEIGHT = 56;
inline constexpr std::int16_t BOOKS_MODAL_CONFIRM_LEFT = 96;
inline constexpr std::int16_t BOOKS_MODAL_CANCEL_LEFT = 264;

static_assert(BOOKS_GRID_COLUMNS * BOOKS_GRID_ROWS == books_view_item_capacity);
static_assert(BOOKS_COVER_WIDTH * 3 == BOOKS_COVER_HEIGHT * 2);
static_assert(
    BOOKS_GRID_LEFT * 2 + BOOKS_GRID_CELL_WIDTH * BOOKS_GRID_COLUMNS +
        BOOKS_GRID_COLUMN_GAP * (BOOKS_GRID_COLUMNS - 1U) ==
    UI_DISPLAY_WIDTH);
static_assert(
    BOOKS_GRID_TOP + BOOKS_CARD_HEIGHT * BOOKS_GRID_ROWS +
        BOOKS_GRID_ROW_GAP * BOOKS_GRID_ROWS <=
    BOOKS_PAGER_TOP);
static_assert(BOOKS_PAGER_TOP + BOOKS_PAGER_HEIGHT == STATUS_BAR_TOP);

constexpr display_rect books_toolbar_rect()
{
    return {0, 0, UI_DISPLAY_WIDTH, BOOKS_TOOLBAR_HEIGHT};
}

constexpr display_rect books_back_rect()
{
    return {0, 0, BOOKS_TOOLBAR_BUTTON_WIDTH, BOOKS_TOOLBAR_HEIGHT};
}

constexpr display_rect books_settings_rect()
{
    return {
        static_cast<std::int16_t>(UI_DISPLAY_WIDTH - BOOKS_TOOLBAR_BUTTON_WIDTH),
        0,
        BOOKS_TOOLBAR_BUTTON_WIDTH,
        BOOKS_TOOLBAR_HEIGHT,
    };
}

constexpr display_rect books_content_rect()
{
    return {
        0,
        BOOKS_TOOLBAR_HEIGHT,
        UI_DISPLAY_WIDTH,
        static_cast<std::int16_t>(STATUS_BAR_TOP - BOOKS_TOOLBAR_HEIGHT),
    };
}

constexpr display_rect books_item_slot_rect(std::uint8_t index)
{
    const std::uint8_t row = index / BOOKS_GRID_COLUMNS;
    const std::uint8_t column = index % BOOKS_GRID_COLUMNS;
    return {
        static_cast<std::int16_t>(
            BOOKS_GRID_LEFT + column *
                (BOOKS_GRID_CELL_WIDTH + BOOKS_GRID_COLUMN_GAP)),
        static_cast<std::int16_t>(
            BOOKS_GRID_TOP + row *
                BOOKS_GRID_SLOT_HEIGHT),
        BOOKS_GRID_CELL_WIDTH,
        BOOKS_GRID_SLOT_HEIGHT,
    };
}

constexpr display_rect books_item_card_rect(std::uint8_t index)
{
    const display_rect slot = books_item_slot_rect(index);
    return {slot.left, slot.top, slot.width, BOOKS_CARD_HEIGHT};
}

constexpr display_rect books_item_hit_rect(std::uint8_t index)
{
    const display_rect card = books_item_card_rect(index);
    return {
        card.left,
        static_cast<std::int16_t>(card.top - BOOKS_HIT_VERTICAL_EXPANSION),
        card.width,
        static_cast<std::int16_t>(
            card.height + BOOKS_HIT_VERTICAL_EXPANSION * 2),
    };
}

constexpr display_rect books_item_redraw_rect(std::uint8_t index)
{
    return books_item_card_rect(index);
}

// Compatibility alias for callers that only need the interactive item area.
constexpr display_rect books_item_rect(std::uint8_t index)
{
    return books_item_hit_rect(index);
}

constexpr display_rect books_cover_rect(std::uint8_t index)
{
    const display_rect card = books_item_card_rect(index);
    return {
        static_cast<std::int16_t>(
            card.left + (card.width - BOOKS_COVER_WIDTH) / 2),
        static_cast<std::int16_t>(card.top + BOOKS_CARD_TOP_PADDING),
        BOOKS_COVER_WIDTH,
        BOOKS_COVER_HEIGHT,
    };
}

constexpr display_rect books_file_name_rect(std::uint8_t index)
{
    const display_rect cover = books_cover_rect(index);
    return {
        static_cast<std::int16_t>(
            books_item_card_rect(index).left + BOOKS_FILE_NAME_SIDE_PADDING),
        static_cast<std::int16_t>(
            cover.top + cover.height + BOOKS_FILE_NAME_TOP_GAP),
        static_cast<std::int16_t>(
            BOOKS_GRID_CELL_WIDTH - BOOKS_FILE_NAME_SIDE_PADDING * 2),
        BOOKS_FILE_NAME_HEIGHT,
    };
}

constexpr display_rect books_type_label_rect(std::uint8_t index)
{
    const display_rect cover = books_cover_rect(index);
    return {
        static_cast<std::int16_t>(
            cover.left + cover.width - BOOKS_TYPE_LABEL_WIDTH),
        static_cast<std::int16_t>(
            cover.top + cover.height - BOOKS_TYPE_LABEL_HEIGHT),
        BOOKS_TYPE_LABEL_WIDTH,
        BOOKS_TYPE_LABEL_HEIGHT,
    };
}

constexpr display_rect books_previous_page_rect()
{
    return {
        BOOKS_GRID_LEFT,
        BOOKS_PAGER_TOP,
        BOOKS_PAGER_BUTTON_WIDTH,
        BOOKS_PAGER_HEIGHT,
    };
}

constexpr display_rect books_next_page_rect()
{
    return {
        static_cast<std::int16_t>(
            UI_DISPLAY_WIDTH - BOOKS_GRID_LEFT - BOOKS_PAGER_BUTTON_WIDTH),
        BOOKS_PAGER_TOP,
        BOOKS_PAGER_BUTTON_WIDTH,
        BOOKS_PAGER_HEIGHT,
    };
}

constexpr display_rect books_modal_rect()
{
    return {
        BOOKS_MODAL_LEFT,
        BOOKS_MODAL_TOP,
        BOOKS_MODAL_WIDTH,
        BOOKS_MODAL_HEIGHT,
    };
}

constexpr display_rect books_setting_row_rect(bool epub)
{
    return {
        BOOKS_MODAL_ROW_LEFT,
        epub ? BOOKS_MODAL_EPUB_ROW_TOP : BOOKS_MODAL_TXT_ROW_TOP,
        BOOKS_MODAL_ROW_WIDTH,
        BOOKS_MODAL_ROW_HEIGHT,
    };
}

constexpr display_rect books_setting_confirm_rect()
{
    return {
        BOOKS_MODAL_CONFIRM_LEFT,
        BOOKS_MODAL_BUTTON_TOP,
        BOOKS_MODAL_BUTTON_WIDTH,
        BOOKS_MODAL_BUTTON_HEIGHT,
    };
}

constexpr display_rect books_setting_cancel_rect()
{
    return {
        BOOKS_MODAL_CANCEL_LEFT,
        BOOKS_MODAL_BUTTON_TOP,
        BOOKS_MODAL_BUTTON_WIDTH,
        BOOKS_MODAL_BUTTON_HEIGHT,
    };
}
