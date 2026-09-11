#include "books_cover.hpp"

#include <array>
#include <cstring>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

#include "books_view.hpp"

namespace {
constexpr std::size_t cover_capacity = 512U * 1024U;
constexpr std::size_t internal_fallback_capacity = 32U * 1024U;

struct cover_slot {
    std::uint8_t* active = nullptr;
    std::size_t active_size = 0U;
    std::uint32_t generation = 0U;
    std::uint16_t references = 0U;
    book_cover_encoding active_encoding = book_cover_encoding::none;
    bool retired = false;
    std::uint8_t* building = nullptr;
    std::size_t building_size = 0U;
    std::size_t used = 0U;
    book_cover_encoding building_encoding = book_cover_encoding::none;
};

std::array<cover_slot, books_view_item_capacity> slots;
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
std::uint32_t next_generation = 0U;

std::uint8_t* allocate(std::size_t size)
{
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    auto* result = static_cast<std::uint8_t*>(
        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (result != nullptr) { return result; }
#endif
    return size <= internal_fallback_capacity
               ? static_cast<std::uint8_t*>(heap_caps_malloc(
                     size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))
               : nullptr;
}
}  // namespace

bool ui_books_cover_begin(std::uint8_t slot, std::size_t size,
                          book_cover_encoding encoding)
{
    if (slot >= slots.size() || size == 0U || size > cover_capacity ||
        encoding == book_cover_encoding::none) { return false; }
    ui_books_cover_cancel(slot);
    auto* data = allocate(size);
    if (data == nullptr) { return false; }
    portENTER_CRITICAL(&guard);
    auto& target = slots[slot];
    target.building = data;
    target.building_size = size;
    target.building_encoding = encoding;
    target.used = 0U;
    portEXIT_CRITICAL(&guard);
    return true;
}

bool ui_books_cover_append(std::uint8_t slot, std::size_t offset,
                           const std::uint8_t* data, std::size_t length)
{
    if (slot >= slots.size() || (data == nullptr && length != 0U)) { return false; }
    std::uint8_t* destination = nullptr;
    portENTER_CRITICAL(&guard);
    auto& target = slots[slot];
    if (target.building != nullptr && offset == target.used &&
        length <= target.building_size - target.used) {
        destination = target.building + target.used;
        target.used += length;
    }
    portEXIT_CRITICAL(&guard);
    if (destination != nullptr && length != 0U) { std::memcpy(destination, data, length); }
    return destination != nullptr;
}

std::uint32_t ui_books_cover_commit(std::uint8_t slot)
{
    if (slot >= slots.size()) { return 0U; }
    std::uint8_t* released = nullptr;
    std::uint32_t generation = 0U;
    portENTER_CRITICAL(&guard);
    auto& target = slots[slot];
    if (target.building != nullptr && target.used == target.building_size &&
        target.references == 0U) {
        released = target.active;
        target.active = target.building;
        target.active_size = target.building_size;
        target.active_encoding = target.building_encoding;
        target.building = nullptr;
        target.building_size = target.used = 0U;
        target.building_encoding = book_cover_encoding::none;
        target.retired = false;
        if (++next_generation == 0U) { ++next_generation; }
        target.generation = next_generation;
        generation = target.generation;
    }
    portEXIT_CRITICAL(&guard);
    heap_caps_free(released);
    if (generation == 0U) { ui_books_cover_cancel(slot); }
    return generation;
}

void ui_books_cover_cancel(std::uint8_t slot)
{
    if (slot >= slots.size()) { return; }
    std::uint8_t* released = nullptr;
    portENTER_CRITICAL(&guard);
    auto& target = slots[slot];
    released = target.building;
    target.building = nullptr;
    target.building_size = target.used = 0U;
    target.building_encoding = book_cover_encoding::none;
    portEXIT_CRITICAL(&guard);
    heap_caps_free(released);
}

void ui_books_cover_clear()
{
    std::uint8_t* released[books_view_item_capacity * 2U] = {};
    std::size_t count = 0U;
    portENTER_CRITICAL(&guard);
    for (auto& slot : slots) {
        if (slot.building != nullptr) { released[count++] = slot.building; }
        slot.building = nullptr;
        slot.building_size = slot.used = 0U;
        slot.building_encoding = book_cover_encoding::none;
        if (slot.active == nullptr) { continue; }
        if (slot.references == 0U) {
            released[count++] = slot.active;
            slot.active = nullptr;
            slot.active_size = 0U;
            slot.active_encoding = book_cover_encoding::none;
            slot.generation = 0U;
        } else {
            slot.retired = true;
        }
    }
    portEXIT_CRITICAL(&guard);
    for (std::size_t index = 0U; index < count; ++index) { heap_caps_free(released[index]); }
}

bool ui_books_cover_acquire(std::uint8_t slot, std::uint32_t generation,
                            books_cover_lease& lease)
{
    lease = {};
    if (slot >= slots.size() || generation == 0U) { return false; }
    portENTER_CRITICAL(&guard);
    auto& target = slots[slot];
    if (!target.retired && target.active != nullptr && target.generation == generation &&
        target.references != UINT16_MAX) {
        ++target.references;
        lease = {target.active, target.active_size, target.generation,
                 target.active_encoding, slot};
    }
    portEXIT_CRITICAL(&guard);
    return lease.data != nullptr;
}

void ui_books_cover_release(books_cover_lease& lease)
{
    std::uint8_t* released = nullptr;
    portENTER_CRITICAL(&guard);
    if (lease.data != nullptr && lease.slot < slots.size()) {
        auto& target = slots[lease.slot];
        if (target.active == lease.data && target.generation == lease.generation &&
            target.references != 0U) {
            --target.references;
            if (target.references == 0U && target.retired) {
                released = target.active;
                target.active = nullptr;
                target.active_size = 0U;
                target.active_encoding = book_cover_encoding::none;
                target.generation = 0U;
                target.retired = false;
            }
        }
    }
    portEXIT_CRITICAL(&guard);
    heap_caps_free(released);
    lease = {};
}
