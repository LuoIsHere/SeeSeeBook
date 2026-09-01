#pragma once

#include <cstdint>
#include <type_traits>

#include "renderer_internal.hpp"

#define UI_FRAME_POOL_CAPACITY 4U

struct ui_frame_handle {
    std::uint32_t generation;
    std::uint8_t index;
};

struct ui_frame_pool_stats {
    std::uint8_t active_count;
    std::uint8_t peak_active_count;
    std::uint32_t acquire_count;
    std::uint32_t acquire_failure_count;
    std::uint32_t publish_count;
    std::uint32_t retain_count;
    std::uint32_t release_count;
    std::uint32_t queue_reclaim_count;
    std::uint32_t stale_handle_count;
    std::uint32_t invalid_transition_count;
};

static_assert(std::is_trivially_copyable_v<ui_frame_handle>);
static_assert(sizeof(ui_frame_handle) == 8U);

constexpr ui_frame_handle invalid_ui_frame_handle()
{
    return {0U, UINT8_MAX};
}

constexpr bool ui_frame_handle_is_valid(const ui_frame_handle& handle)
{
    return handle.generation != 0U && handle.index < UI_FRAME_POOL_CAPACITY;
}

constexpr bool ui_frame_handles_equal(
    const ui_frame_handle& left,
    const ui_frame_handle& right)
{
    return left.generation == right.generation && left.index == right.index;
}

bool ui_frame_pool_acquire(
    ui_frame_handle& handle,
    display_request*& frame);
bool ui_frame_pool_publish(const ui_frame_handle& handle);
bool ui_frame_pool_retain(const ui_frame_handle& handle);
bool ui_frame_pool_resolve(
    const ui_frame_handle& handle,
    const display_request*& frame);
bool ui_frame_pool_release(ui_frame_handle& handle);
void ui_frame_pool_record_queue_reclaim();
ui_frame_pool_stats ui_frame_pool_get_stats();
