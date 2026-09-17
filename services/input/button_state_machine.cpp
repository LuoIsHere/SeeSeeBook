#include "button_state_machine.hpp"

#include "input_service.hpp"

button_state_machine::key button_state_machine::sampled_key(
    const button_sample& sample)
{
    if (sample.key1_pressed == sample.key2_pressed) {
        return key::none;
    }
    return sample.key1_pressed ? key::key1 : key::key2;
}

void button_state_machine::reset()
{
    active_key_ = key::none;
    phase_ = phase::idle;
    phase_started_ms_ = 0U;
    press_started_ms_ = 0U;
}

bool button_state_machine::update(
    const button_sample& sample,
    std::uint32_t now_ms,
    navigation_event& event)
{
    const bool both_pressed = sample.key1_pressed && sample.key2_pressed;
    const bool released = !sample.key1_pressed && !sample.key2_pressed;
    const key current = sampled_key(sample);

    if (both_pressed) {
        phase_ = phase::blocked;
        active_key_ = key::none;
        return false;
    }

    switch (phase_) {
        case phase::idle:
            if (current != key::none) {
                active_key_ = current;
                phase_ = phase::debounce_press;
                phase_started_ms_ = press_started_ms_ = now_ms;
            }
            break;
        case phase::debounce_press:
            if (released) {
                reset();
            } else if (current != active_key_) {
                phase_ = phase::blocked;
                active_key_ = key::none;
            } else if (now_ms - phase_started_ms_ >= BUTTON_DEBOUNCE_TIME_MS) {
                phase_ = phase::pressed;
            }
            break;
        case phase::pressed:
            if (released) {
                phase_ = phase::debounce_release;
                phase_started_ms_ = now_ms;
            } else if (current != active_key_) {
                phase_ = phase::blocked;
                active_key_ = key::none;
            } else if (now_ms - press_started_ms_ >= BUTTON_LONG_PRESS_TIME_MS) {
                event.action = active_key_ == key::key1
                                   ? navigation_action::confirm
                                   : navigation_action::back;
                event.timestamp_ms = now_ms;
                phase_ = phase::long_emitted;
                return true;
            }
            break;
        case phase::debounce_release:
            if (current == active_key_) {
                phase_ = phase::pressed;
            } else if (!released) {
                phase_ = phase::blocked;
                active_key_ = key::none;
            } else if (now_ms - phase_started_ms_ >= BUTTON_DEBOUNCE_TIME_MS) {
                event.action = active_key_ == key::key1
                                   ? navigation_action::previous
                                   : navigation_action::next;
                event.timestamp_ms = now_ms;
                reset();
                return true;
            }
            break;
        case phase::long_emitted:
            if (released) {
                phase_ = phase::debounce_long_release;
                phase_started_ms_ = now_ms;
            } else if (current != active_key_) {
                phase_ = phase::blocked;
                active_key_ = key::none;
            }
            break;
        case phase::debounce_long_release:
            if (current == active_key_) {
                phase_ = phase::long_emitted;
            } else if (!released) {
                phase_ = phase::blocked;
                active_key_ = key::none;
            } else if (now_ms - phase_started_ms_ >= BUTTON_DEBOUNCE_TIME_MS) {
                reset();
            }
            break;
        case phase::blocked:
            if (released) {
                phase_ = phase::debounce_blocked_release;
                phase_started_ms_ = now_ms;
            }
            break;
        case phase::debounce_blocked_release:
            if (!released) {
                phase_ = phase::blocked;
            } else if (now_ms - phase_started_ms_ >= BUTTON_DEBOUNCE_TIME_MS) {
                reset();
            }
            break;
    }
    return false;
}
