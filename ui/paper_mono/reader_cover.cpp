#include "reader_cover.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

namespace {
constexpr std::size_t cover_capacity = 512U * 1024U;
constexpr std::size_t internal_fallback_capacity = 32U * 1024U;
constexpr std::size_t image_header_scan_capacity = 64U * 1024U;
constexpr std::uint32_t image_dimension_limit = 4096U;

struct cover_slot {
    std::uint8_t* data = nullptr;
    std::size_t size = 0U;
    std::size_t used = 0U;
    std::uint32_t generation = 0U;
    std::uint16_t references = 0U;
    book_cover_encoding encoding = book_cover_encoding::none;
    bool retired = false;
};

std::array<cover_slot, 2U> slots;
portMUX_TYPE guard = portMUX_INITIALIZER_UNLOCKED;
std::int8_t active_slot = -1;
std::int8_t building_slot = -1;
std::uint32_t next_generation = 0U;

std::uint16_t big_u16(const std::uint8_t* data)
{
    return (std::uint16_t(data[0]) << 8U) | data[1];
}

std::uint32_t big_u32(const std::uint8_t* data)
{
    return (std::uint32_t(data[0]) << 24U) | (std::uint32_t(data[1]) << 16U) |
           (std::uint32_t(data[2]) << 8U) | data[3];
}

bool dimensions_valid(std::uint32_t width, std::uint32_t height)
{
    return width != 0U && height != 0U && width <= image_dimension_limit &&
           height <= image_dimension_limit &&
           std::uint64_t(width) * height <= std::uint64_t(image_dimension_limit) * image_dimension_limit;
}

bool png_valid(const std::uint8_t* data, std::size_t size)
{
    constexpr std::uint8_t signature[] = {0x89U, 'P', 'N', 'G', 0x0dU, 0x0aU, 0x1aU, 0x0aU};
    return size >= 24U && std::memcmp(data, signature, sizeof(signature)) == 0 &&
           big_u32(data + 8U) == 13U && std::memcmp(data + 12U, "IHDR", 4U) == 0 &&
           dimensions_valid(big_u32(data + 16U), big_u32(data + 20U));
}

bool jpeg_valid(const std::uint8_t* data, std::size_t size)
{
    if (size < 4U || data[0] != 0xffU || data[1] != 0xd8U) { return false; }
    const auto limit = std::min(size, image_header_scan_capacity);
    std::size_t cursor = 2U;
    while (cursor < limit) {
        if (data[cursor++] != 0xffU) { return false; }
        while (cursor < limit && data[cursor] == 0xffU) { ++cursor; }
        if (cursor == limit) { return false; }
        const std::uint8_t marker = data[cursor++];
        if (marker == 0xd8U || marker == 0x01U || (marker >= 0xd0U && marker <= 0xd7U)) { continue; }
        if (marker == 0xd9U || marker == 0xdaU || cursor + 2U > limit) { return false; }
        const std::size_t segment = big_u16(data + cursor);
        if (segment < 2U || segment > limit - cursor) { return false; }
        const bool start_of_frame = marker >= 0xc0U && marker <= 0xcfU &&
                                    marker != 0xc4U && marker != 0xc8U && marker != 0xccU;
        if (start_of_frame) {
            return segment >= 7U && dimensions_valid(big_u16(data + cursor + 5U),
                                                      big_u16(data + cursor + 3U));
        }
        cursor += segment;
    }
    return false;
}

bool image_valid(const cover_slot& slot)
{
    if (slot.data == nullptr || slot.used != slot.size) { return false; }
    return slot.encoding == book_cover_encoding::png ? png_valid(slot.data, slot.size) :
           slot.encoding == book_cover_encoding::jpeg ? jpeg_valid(slot.data, slot.size) : false;
}
}

bool ui_reader_cover_begin(std::size_t size, book_cover_encoding encoding)
{
    if (size == 0U || size > cover_capacity || encoding == book_cover_encoding::none) { return false; }
    ui_reader_cover_cancel();
    std::int8_t selected = -1;
    std::uint8_t* previous = nullptr;
    portENTER_CRITICAL(&guard);
    for (std::size_t i = 0U; i < slots.size(); ++i) {
        if (static_cast<std::int8_t>(i) != active_slot && slots[i].references == 0U) {
            selected = static_cast<std::int8_t>(i);
            previous = slots[i].data;
            slots[i] = {};
            break;
        }
    }
    portEXIT_CRITICAL(&guard);
    if (selected < 0) { return false; }
    heap_caps_free(previous);
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    auto* data = static_cast<std::uint8_t*>(
        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    // Host/QEMU and boards without external RAM can still display small covers
    // without allowing a large image to consume the internal heap.
    auto* data = size <= internal_fallback_capacity
                     ? static_cast<std::uint8_t*>(heap_caps_malloc(
                           size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))
                     : nullptr;
#endif
    if (data == nullptr) { return false; }
    portENTER_CRITICAL(&guard);
    auto& slot = slots[static_cast<std::size_t>(selected)];
    slot.data = data; slot.size = size; slot.encoding = encoding;
    building_slot = selected;
    portEXIT_CRITICAL(&guard);
    return true;
}

bool ui_reader_cover_append(std::size_t offset, const std::uint8_t* data, std::size_t length)
{
    if (data == nullptr && length != 0U) { return false; }
    std::uint8_t* destination = nullptr;
    portENTER_CRITICAL(&guard);
    if (building_slot >= 0) {
        auto& slot = slots[static_cast<std::size_t>(building_slot)];
        if (offset == slot.used && length <= slot.size - slot.used) {
            destination = slot.data + slot.used;
            slot.used += length;
        }
    }
    portEXIT_CRITICAL(&guard);
    // Only the App task builds or cancels a cover. The renderer cannot acquire
    // a building slot, so the copy does not need to hold the interrupt lock.
    if (destination != nullptr && length != 0U) { std::memcpy(destination, data, length); }
    return destination != nullptr;
}

std::uint32_t ui_reader_cover_commit()
{
    std::int8_t candidate = -1;
    cover_slot snapshot = {};
    portENTER_CRITICAL(&guard);
    if (building_slot >= 0) {
        candidate = building_slot;
        snapshot = slots[static_cast<std::size_t>(candidate)];
    }
    portEXIT_CRITICAL(&guard);
    if (candidate < 0 || !image_valid(snapshot)) {
        ui_reader_cover_cancel();
        return 0U;
    }
    std::uint32_t generation = 0U;
    portENTER_CRITICAL(&guard);
    if (building_slot == candidate) {
        auto& slot = slots[static_cast<std::size_t>(building_slot)];
        if (slot.data == snapshot.data && slot.used == slot.size) {
            if (++next_generation == 0U) { ++next_generation; }
            slot.generation = next_generation;
            active_slot = building_slot;
            building_slot = -1;
            generation = slot.generation;
        }
    }
    portEXIT_CRITICAL(&guard);
    return generation;
}

void ui_reader_cover_cancel()
{
    std::uint8_t* data = nullptr;
    portENTER_CRITICAL(&guard);
    if (building_slot >= 0) {
        auto& slot = slots[static_cast<std::size_t>(building_slot)];
        data = slot.data;
        slot = {};
        building_slot = -1;
    }
    portEXIT_CRITICAL(&guard);
    heap_caps_free(data);
}

void ui_reader_cover_clear()
{
    ui_reader_cover_cancel();
    std::uint8_t* released[2] = {};
    std::size_t released_count = 0U;
    portENTER_CRITICAL(&guard);
    active_slot = -1;
    for (auto& slot : slots) {
        if (slot.data == nullptr) { continue; }
        if (slot.references == 0U) {
            released[released_count++] = slot.data;
            slot = {};
        } else {
            slot.retired = true;
        }
    }
    portEXIT_CRITICAL(&guard);
    for (std::size_t index = 0U; index < released_count; ++index) {
        heap_caps_free(released[index]);
    }
}

bool ui_reader_cover_acquire(std::uint32_t generation, reader_cover_lease& lease)
{
    lease = {};
    portENTER_CRITICAL(&guard);
    if (active_slot >= 0) {
        auto& slot = slots[static_cast<std::size_t>(active_slot)];
        if (slot.generation == generation && slot.data != nullptr && slot.references != UINT16_MAX) {
            ++slot.references;
            lease = {slot.data, slot.size, slot.generation, slot.encoding,
                     static_cast<std::uint8_t>(active_slot)};
        }
    }
    portEXIT_CRITICAL(&guard);
    return lease.data != nullptr;
}

void ui_reader_cover_release(reader_cover_lease& lease)
{
    std::uint8_t* released = nullptr;
    portENTER_CRITICAL(&guard);
    if (lease.data != nullptr && lease.slot < slots.size()) {
        auto& slot = slots[lease.slot];
        if (slot.data == lease.data && slot.generation == lease.generation && slot.references != 0U) {
            --slot.references;
            if (slot.references == 0U && slot.retired) {
                released = slot.data;
                slot = {};
            }
        }
    }
    portEXIT_CRITICAL(&guard);
    heap_caps_free(released);
    lease = {};
}
