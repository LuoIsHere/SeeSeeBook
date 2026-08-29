#pragma once

#include <cstdint>

#include "hal_touch.hpp"

enum class app_event_type : std::uint8_t {
    touch,
};

struct app_event {
    app_event_type type;
    touch_event touch;
};
