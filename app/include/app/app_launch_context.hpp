#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Launch data crosses one deferred App Runtime update. Keep it bounded and
// owned by the runtime so callers never expose stack or heap lifetimes.
constexpr std::size_t APP_LAUNCH_PAYLOAD_CAPACITY = 544U;

struct app_launch_context {
    template <typename payload_type>
    bool set(const payload_type& value)
    {
        static_assert(std::is_trivially_copyable_v<payload_type>);
        static_assert(sizeof(payload_type) <= APP_LAUNCH_PAYLOAD_CAPACITY);
        static_assert(payload_type::launch_type != 0U);
        clear();
        type = payload_type::launch_type;
        size = sizeof(payload_type);
        std::memcpy(data, &value, sizeof(value));
        return true;
    }

    template <typename payload_type>
    bool get(payload_type& value) const
    {
        static_assert(std::is_trivially_copyable_v<payload_type>);
        static_assert(sizeof(payload_type) <= APP_LAUNCH_PAYLOAD_CAPACITY);
        if (type != payload_type::launch_type || size != sizeof(payload_type)) {
            value = {};
            return false;
        }
        std::memcpy(&value, data, sizeof(value));
        return true;
    }

    bool has_value() const { return type != 0U && size != 0U; }

    void clear()
    {
        type = 0U;
        size = 0U;
        std::memset(data, 0, sizeof(data));
    }

    std::uint32_t type = 0U;
    std::uint16_t size = 0U;
    alignas(std::max_align_t) std::uint8_t data[APP_LAUNCH_PAYLOAD_CAPACITY] = {};
};

static_assert(std::is_trivially_copyable_v<app_launch_context>);
