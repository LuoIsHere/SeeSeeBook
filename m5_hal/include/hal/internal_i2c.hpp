#pragma once

#include <cstdint>

#define INTERNAL_I2C_TOUCH_TIMEOUT_MS 2U
#define INTERNAL_I2C_FRONT_LIGHT_TIMEOUT_MS 50U
#define INTERNAL_I2C_RTC_TIMEOUT_MS 100U
#define INTERNAL_I2C_BATTERY_TIMEOUT_MS 100U
#define INTERNAL_I2C_STORAGE_TIMEOUT_MS 20U

bool hal_internal_i2c_init();
bool hal_internal_i2c_lock(std::uint32_t timeout_ms);
void hal_internal_i2c_unlock();

class internal_i2c_guard final {
public:
    explicit internal_i2c_guard(std::uint32_t timeout_ms);
    ~internal_i2c_guard();

    internal_i2c_guard(const internal_i2c_guard&) = delete;
    internal_i2c_guard& operator=(const internal_i2c_guard&) = delete;

    bool locked() const;

private:
    bool locked_ = false;
};
