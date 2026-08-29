#pragma once

#include <cstdint>

#define INTERNAL_I2C_TOUCH_TIMEOUT_MS 2U
#define INTERNAL_I2C_FRONT_LIGHT_TIMEOUT_MS 50U
#define INTERNAL_I2C_RTC_TIMEOUT_MS 100U
#define INTERNAL_I2C_BATTERY_TIMEOUT_MS 100U

// Initializes the process-wide mutex for the PaperMono internal I2C bus.
bool hal_internal_i2c_init();

// Acquires the internal I2C bus within a finite timeout.
bool hal_internal_i2c_lock(std::uint32_t timeout_ms);

// Releases a previously acquired internal I2C bus lock.
void hal_internal_i2c_unlock();

// Provides scope-bound release for every internal I2C transaction group.
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
