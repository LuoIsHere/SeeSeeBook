#include "ui_frame_pool.hpp"

#include <cstring>

#include <freertos/FreeRTOS.h>

namespace {

enum class ui_frame_slot_state : std::uint8_t {
    free,
    building,
    published,
};

struct ui_frame_slot {
    display_request frame;
    std::uint32_t generation;
    std::uint16_t reference_count;
    ui_frame_slot_state state;
};

portMUX_TYPE pool_lock = portMUX_INITIALIZER_UNLOCKED;
ui_frame_slot slots[UI_FRAME_POOL_CAPACITY] = {};
std::uint8_t active_count = 0U;
std::uint8_t peak_active_count = 0U;

bool handle_matches_locked(
    const ui_frame_handle& handle,
    const ui_frame_slot& slot)
{
    return ui_frame_handle_is_valid(handle) &&
           slot.state != ui_frame_slot_state::free &&
           slot.generation == handle.generation &&
           slot.reference_count > 0U;
}

}  // namespace

bool ui_frame_pool_acquire(
    ui_frame_handle& handle,
    display_request*& frame)
{
    handle = invalid_ui_frame_handle();
    frame = nullptr;
    portENTER_CRITICAL(&pool_lock);
    for (std::uint8_t index = 0U; index < UI_FRAME_POOL_CAPACITY; ++index) {
        ui_frame_slot& slot = slots[index];
        if (slot.state != ui_frame_slot_state::free) {
            continue;
        }
        ++slot.generation;
        if (slot.generation == 0U) {
            ++slot.generation;
        }
        slot.reference_count = 1U;
        slot.state = ui_frame_slot_state::building;
        ++active_count;
        if (active_count > peak_active_count) {
            peak_active_count = active_count;
        }
        handle = {slot.generation, index};
        frame = &slot.frame;
        break;
    }
    portEXIT_CRITICAL(&pool_lock);
    if (frame != nullptr) {
        // The producer has exclusive access while the slot is building.
        std::memset(frame, 0, sizeof(*frame));
    }
    return frame != nullptr;
}

bool ui_frame_pool_publish(const ui_frame_handle& handle)
{
    bool published = false;
    portENTER_CRITICAL(&pool_lock);
    if (ui_frame_handle_is_valid(handle)) {
        ui_frame_slot& slot = slots[handle.index];
        if (handle_matches_locked(handle, slot) &&
            slot.state == ui_frame_slot_state::building) {
            slot.state = ui_frame_slot_state::published;
            published = true;
        }
    }
    portEXIT_CRITICAL(&pool_lock);
    return published;
}

bool ui_frame_pool_retain(const ui_frame_handle& handle)
{
    bool retained = false;
    portENTER_CRITICAL(&pool_lock);
    if (ui_frame_handle_is_valid(handle)) {
        ui_frame_slot& slot = slots[handle.index];
        if (handle_matches_locked(handle, slot) &&
            slot.state == ui_frame_slot_state::published &&
            slot.reference_count < UINT16_MAX) {
            ++slot.reference_count;
            retained = true;
        }
    }
    portEXIT_CRITICAL(&pool_lock);
    return retained;
}

bool ui_frame_pool_resolve(
    const ui_frame_handle& handle,
    const display_request*& frame)
{
    frame = nullptr;
    portENTER_CRITICAL(&pool_lock);
    if (ui_frame_handle_is_valid(handle)) {
        const ui_frame_slot& slot = slots[handle.index];
        if (handle_matches_locked(handle, slot) &&
            slot.state == ui_frame_slot_state::published) {
            frame = &slot.frame;
        }
    }
    portEXIT_CRITICAL(&pool_lock);
    return frame != nullptr;
}

bool ui_frame_pool_release(ui_frame_handle& handle)
{
    bool released = false;
    portENTER_CRITICAL(&pool_lock);
    if (ui_frame_handle_is_valid(handle)) {
        ui_frame_slot& slot = slots[handle.index];
        if (handle_matches_locked(handle, slot)) {
            --slot.reference_count;
            if (slot.reference_count == 0U) {
                slot.state = ui_frame_slot_state::free;
                if (active_count > 0U) {
                    --active_count;
                }
            }
            released = true;
        }
    }
    portEXIT_CRITICAL(&pool_lock);
    if (released) {
        handle = invalid_ui_frame_handle();
    }
    return released;
}

ui_frame_pool_stats ui_frame_pool_get_stats()
{
    ui_frame_pool_stats stats = {};
    portENTER_CRITICAL(&pool_lock);
    stats.active_count = active_count;
    stats.peak_active_count = peak_active_count;
    portEXIT_CRITICAL(&pool_lock);
    return stats;
}
