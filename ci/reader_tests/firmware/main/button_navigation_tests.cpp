#include <cstdio>
#include <cstdlib>

#include "button_state_machine.hpp"
#include "input_service.hpp"
#include "selection_controller.hpp"

namespace {

#define CHECK_BUTTON(condition) do { if (!(condition)) { \
    std::printf("TEST_FAILURE button line=%d: %s\n", __LINE__, #condition); \
    std::fflush(stdout); std::abort(); \
} } while (0)

bool update(
    button_state_machine& state,
    bool key1,
    bool key2,
    std::uint32_t now_ms,
    navigation_event& event)
{
    return state.update({key1, key2}, now_ms, event);
}

void test_short_and_long_press()
{
    navigation_event event = {};
    button_state_machine state;
    CHECK_BUTTON(!update(state, true, false, 0U, event));
    CHECK_BUTTON(!update(state, true, false, BUTTON_DEBOUNCE_TIME_MS, event));
    CHECK_BUTTON(!update(state, false, false, 100U, event));
    CHECK_BUTTON(update(state, false, false, 100U + BUTTON_DEBOUNCE_TIME_MS, event));
    CHECK_BUTTON(event.action == navigation_action::previous);

    CHECK_BUTTON(!update(state, false, true, 200U, event));
    CHECK_BUTTON(!update(state, false, true, 200U + BUTTON_DEBOUNCE_TIME_MS, event));
    CHECK_BUTTON(!update(state, false, false, 300U, event));
    CHECK_BUTTON(update(state, false, false, 300U + BUTTON_DEBOUNCE_TIME_MS, event));
    CHECK_BUTTON(event.action == navigation_action::next);

    CHECK_BUTTON(!update(state, true, false, 400U, event));
    CHECK_BUTTON(!update(state, true, false, 400U + BUTTON_DEBOUNCE_TIME_MS, event));
    CHECK_BUTTON(update(state, true, false, 400U + BUTTON_LONG_PRESS_TIME_MS, event));
    CHECK_BUTTON(event.action == navigation_action::confirm);
    CHECK_BUTTON(!update(state, true, false, 1400U, event));
    CHECK_BUTTON(!update(state, false, false, 1410U, event));
    CHECK_BUTTON(!update(state, false, false, 1410U + BUTTON_DEBOUNCE_TIME_MS, event));

    CHECK_BUTTON(!update(state, false, true, 1500U, event));
    CHECK_BUTTON(!update(state, false, true, 1500U + BUTTON_DEBOUNCE_TIME_MS, event));
    CHECK_BUTTON(update(state, false, true, 1500U + BUTTON_LONG_PRESS_TIME_MS, event));
    CHECK_BUTTON(event.action == navigation_action::back);
    CHECK_BUTTON(!update(state, false, true, 2500U, event));
}

void test_bounce_and_overlap()
{
    navigation_event event = {};
    button_state_machine state;
    CHECK_BUTTON(!update(state, true, false, 0U, event));
    CHECK_BUTTON(!update(state, false, false, 10U, event));
    CHECK_BUTTON(!update(state, false, false, 40U, event));

    CHECK_BUTTON(!update(state, true, false, 100U, event));
    CHECK_BUTTON(!update(state, true, false, 120U, event));
    CHECK_BUTTON(!update(state, true, true, 130U, event));
    CHECK_BUTTON(!update(state, false, true, 200U, event));
    CHECK_BUTTON(!update(state, false, false, 220U, event));
    CHECK_BUTTON(!update(state, false, false, 240U, event));

    CHECK_BUTTON(!update(state, false, true, 300U, event));
    CHECK_BUTTON(!update(state, false, true, 320U, event));
    CHECK_BUTTON(!update(state, false, false, 400U, event));
    CHECK_BUTTON(update(state, false, false, 420U, event));
    CHECK_BUTTON(event.action == navigation_action::next);

    button_state_machine reverse;
    CHECK_BUTTON(!update(reverse, false, true, 500U, event));
    CHECK_BUTTON(!update(reverse, false, true, 520U, event));
    CHECK_BUTTON(!update(reverse, true, true, 530U, event));
    CHECK_BUTTON(!update(reverse, true, false, 600U, event));
    CHECK_BUTTON(!update(reverse, false, false, 620U, event));
    CHECK_BUTTON(!update(reverse, false, false, 640U, event));
}

void test_selection_controller()
{
    selection_controller selection;
    CHECK_BUTTON(!selection.selected(0U, 3U));
    CHECK_BUTTON(selection.move_next(0U, 3U) && selection.index() == 0U);
    CHECK_BUTTON(selection.move_previous(0U, 3U) && selection.index() == 2U);
    CHECK_BUTTON(selection.move_next(0U, 3U) && selection.index() == 0U);
    CHECK_BUTTON(selection.move_previous(1U, 2U) && selection.index() == 1U);
    CHECK_BUTTON(selection.move_next(1U, 2U) && selection.index() == 1U);
    CHECK_BUTTON(!selection.move_next(1U, 1U));
    CHECK_BUTTON(!selection.selected(0U, 1U));
}

}  // namespace

void test_button_navigation()
{
    test_short_and_long_press();
    test_bounce_and_overlap();
    test_selection_controller();
    std::puts("PASS buttons: debounce, short/long mapping, overlap lockout, selection wrap");
}
