#pragma once

#include <cstdint>
#include <type_traits>

enum class navigation_action : std::uint8_t {
    previous,
    next,
    confirm,
    back,
};

struct navigation_event {
    navigation_action action;
    std::uint32_t timestamp_ms;
};

static_assert(std::is_trivially_copyable_v<navigation_event>);
