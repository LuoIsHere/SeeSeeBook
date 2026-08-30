#pragma once

#include <cstdint>

#define FRONT_LIGHT_LEVEL_COUNT 5U
#define FRONT_LIGHT_DEFAULT_LEVEL_INDEX 2U

bool front_light_service_set_level(std::uint8_t level_index);
std::uint8_t front_light_service_get_level();
