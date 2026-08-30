#include "front_light_service.hpp"

#include <atomic>

#include <esp_log.h>

#include "display.hpp"

namespace {

constexpr char log_tag[] = "service_front_light";
constexpr std::uint8_t levels[FRONT_LIGHT_LEVEL_COUNT] = {
    0U,
    64U,
    128U,
    192U,
    255U,
};
std::atomic_uint8_t current_level{FRONT_LIGHT_DEFAULT_LEVEL_INDEX};

}  // namespace

bool front_light_service_set_level(std::uint8_t level_index)
{
    if (level_index >= FRONT_LIGHT_LEVEL_COUNT ||
        !hal_display_set_front_light(levels[level_index])) {
        return false;
    }
    current_level.store(level_index);
    ESP_LOGI(log_tag, "level applied index=%u", static_cast<unsigned>(level_index));
    return true;
}

std::uint8_t front_light_service_get_level()
{
    return current_level.load();
}
