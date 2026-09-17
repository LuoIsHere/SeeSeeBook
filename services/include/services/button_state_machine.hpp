#pragma once

#include <cstdint>

#include "buttons.hpp"
#include "navigation_event.hpp"

class button_state_machine {
public:
    bool update(
        const button_sample& sample,
        std::uint32_t now_ms,
        navigation_event& event);

private:
    enum class key : std::uint8_t {
        none,
        key1,
        key2,
    };

    enum class phase : std::uint8_t {
        idle,
        debounce_press,
        pressed,
        debounce_release,
        long_emitted,
        debounce_long_release,
        blocked,
        debounce_blocked_release,
    };

    static key sampled_key(const button_sample& sample);
    void reset();

    key active_key_ = key::none;
    phase phase_ = phase::idle;
    std::uint32_t phase_started_ms_ = 0U;
    std::uint32_t press_started_ms_ = 0U;
};
