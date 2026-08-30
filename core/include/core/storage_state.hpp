#pragma once

#include <cstdint>
#include <type_traits>

#include <esp_err.h>

enum class storage_state : std::uint8_t {
    no_card,
    mounting,
    ready,
    error,
};

struct storage_status_event {
    storage_state state;
    std::uint32_t media_generation;
    esp_err_t error;
};

static_assert(std::is_trivially_copyable_v<storage_status_event>);
