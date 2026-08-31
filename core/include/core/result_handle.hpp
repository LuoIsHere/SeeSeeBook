#pragma once

#include <cstdint>
#include <type_traits>

struct result_handle {
    std::uint16_t index;
    std::uint32_t generation;
};

constexpr result_handle invalid_result_handle()
{
    return {UINT16_MAX, 0U};
}

constexpr bool result_handle_is_valid(const result_handle& handle)
{
    return handle.index != UINT16_MAX && handle.generation != 0U;
}

static_assert(std::is_trivially_copyable_v<result_handle>);
