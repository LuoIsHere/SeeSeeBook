#include "ui_frame_pool.hpp"

#include <cassert>
#include <cstring>

#include <esp_attr.h>
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
DRAM_ATTR ui_frame_slot slots[UI_FRAME_POOL_CAPACITY] = {};
ui_frame_pool_stats pool_stats = {};

void increment_counter(std::uint32_t& counter)
{
    if (counter < UINT32_MAX) {
        ++counter;
    }
}

void validate_pool_locked()
{
    std::uint8_t calculated_active = 0U;
    for (const ui_frame_slot& slot : slots) {
        if (slot.state == ui_frame_slot_state::free) {
            assert(slot.reference_count == 0U);
            continue;
        }
        assert(slot.reference_count > 0U);
        assert(slot.generation != 0U);
        ++calculated_active;
    }
    assert(calculated_active == pool_stats.active_count);
    assert(pool_stats.active_count <= UI_FRAME_POOL_CAPACITY);
    assert(pool_stats.peak_active_count <= UI_FRAME_POOL_CAPACITY);
}

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
        ++pool_stats.active_count;
        if (pool_stats.active_count > pool_stats.peak_active_count) {
            pool_stats.peak_active_count = pool_stats.active_count;
        }
        increment_counter(pool_stats.acquire_count);
        handle = {slot.generation, index};
        frame = &slot.frame;
        break;
    }
    if (frame == nullptr) {
        increment_counter(pool_stats.acquire_failure_count);
    }
    validate_pool_locked();
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
    if (!ui_frame_handle_is_valid(handle)) {
        increment_counter(pool_stats.stale_handle_count);
    } else {
        ui_frame_slot& slot = slots[handle.index];
        if (!handle_matches_locked(handle, slot)) {
            increment_counter(pool_stats.stale_handle_count);
        } else if (slot.state == ui_frame_slot_state::building) {
            slot.state = ui_frame_slot_state::published;
            increment_counter(pool_stats.publish_count);
            published = true;
        } else {
            increment_counter(pool_stats.invalid_transition_count);
        }
    }
    validate_pool_locked();
    portEXIT_CRITICAL(&pool_lock);
    return published;
}

bool ui_frame_pool_retain(const ui_frame_handle& handle)
{
    bool retained = false;
    portENTER_CRITICAL(&pool_lock);
    if (!ui_frame_handle_is_valid(handle)) {
        increment_counter(pool_stats.stale_handle_count);
    } else {
        ui_frame_slot& slot = slots[handle.index];
        if (!handle_matches_locked(handle, slot)) {
            increment_counter(pool_stats.stale_handle_count);
        } else if (slot.state == ui_frame_slot_state::published &&
                   slot.reference_count < UINT16_MAX) {
            ++slot.reference_count;
            increment_counter(pool_stats.retain_count);
            retained = true;
        } else {
            increment_counter(pool_stats.invalid_transition_count);
        }
    }
    validate_pool_locked();
    portEXIT_CRITICAL(&pool_lock);
    return retained;
}

bool ui_frame_pool_resolve(
    const ui_frame_handle& handle,
    const display_request*& frame)
{
    frame = nullptr;
    portENTER_CRITICAL(&pool_lock);
    if (!ui_frame_handle_is_valid(handle)) {
        increment_counter(pool_stats.stale_handle_count);
    } else {
        const ui_frame_slot& slot = slots[handle.index];
        if (!handle_matches_locked(handle, slot)) {
            increment_counter(pool_stats.stale_handle_count);
        } else if (slot.state == ui_frame_slot_state::published) {
            frame = &slot.frame;
        } else {
            increment_counter(pool_stats.invalid_transition_count);
        }
    }
    portEXIT_CRITICAL(&pool_lock);
    return frame != nullptr;
}

bool ui_frame_pool_release(ui_frame_handle& handle)
{
    bool released = false;
    portENTER_CRITICAL(&pool_lock);
    if (!ui_frame_handle_is_valid(handle)) {
        increment_counter(pool_stats.stale_handle_count);
    } else {
        ui_frame_slot& slot = slots[handle.index];
        if (handle_matches_locked(handle, slot)) {
            --slot.reference_count;
            if (slot.reference_count == 0U) {
                slot.state = ui_frame_slot_state::free;
                if (pool_stats.active_count > 0U) {
                    --pool_stats.active_count;
                }
            }
            increment_counter(pool_stats.release_count);
            released = true;
        } else {
            increment_counter(pool_stats.stale_handle_count);
        }
    }
    validate_pool_locked();
    portEXIT_CRITICAL(&pool_lock);
    if (released) {
        handle = invalid_ui_frame_handle();
    }
    return released;
}

void ui_frame_pool_record_queue_reclaim()
{
    portENTER_CRITICAL(&pool_lock);
    increment_counter(pool_stats.queue_reclaim_count);
    portEXIT_CRITICAL(&pool_lock);
}

ui_frame_pool_stats ui_frame_pool_get_stats()
{
    portENTER_CRITICAL(&pool_lock);
    const ui_frame_pool_stats stats = pool_stats;
    portEXIT_CRITICAL(&pool_lock);
    return stats;
}
