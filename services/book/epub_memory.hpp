#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

#include <esp_heap_caps.h>

struct epub_heap_deleter {
    template<typename value_type>
    void operator()(value_type* value) const
    {
        heap_caps_free(value);
    }
};

template<typename value_type>
using epub_unique_array = std::unique_ptr<value_type[], epub_heap_deleter>;

template<typename value_type>
epub_unique_array<value_type> epub_allocate_array(std::size_t count)
{
    static_assert(std::is_trivial_v<value_type>);
    if (count == 0U || count > SIZE_MAX / sizeof(value_type)) { return {}; }
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void* allocation = heap_caps_calloc(count, sizeof(value_type),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* allocation = heap_caps_calloc(count, sizeof(value_type),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    return epub_unique_array<value_type>(static_cast<value_type*>(allocation));
}
