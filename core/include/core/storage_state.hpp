#pragma once

#include <cstdint>

enum class storage_state : std::uint8_t {
    no_card,
    mounting,
    ready,
    error,
};
