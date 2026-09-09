#pragma once

#include <cstdint>
#include <type_traits>

struct gray4_test_view_state {
    std::uint8_t reserved;
};

static_assert(std::is_trivially_copyable_v<gray4_test_view_state>);
