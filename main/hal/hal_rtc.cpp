#include "hal_rtc.hpp"

#include <M5Unified.h>
#include <esp_log.h>
#include <freertos/queue.h>

#include "hal.hpp"

namespace {

constexpr char log_tag[] = "hal_rtc";
constexpr std::uint32_t bootstrap_request_id = 0U;

struct rtc_request {
    rtc_operation operation;
    std::uint32_t request_id;
    rtc_datetime datetime;
    bool publish_event;
};

QueueHandle_t request_queue = nullptr;
QueueHandle_t event_queue = nullptr;
SemaphoreHandle_t internal_i2c_mutex = nullptr;

portMUX_TYPE cache_mutex = portMUX_INITIALIZER_UNLOCKED;
rtc_datetime cached_datetime = {};
std::uint32_t cached_tick_ms = 0;
std::uint32_t cache_generation = 0;
bool cache_valid = false;

bool is_leap_year(std::uint16_t year)
{
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month)
{
    constexpr std::uint8_t month_days[] = {
        31U,
        28U,
        31U,
        30U,
        31U,
        30U,
        31U,
        31U,
        30U,
        31U,
        30U,
        31U,
    };
    if (month < 1U || month > 12U) {
        return 0U;
    }
    if (month == 2U && is_leap_year(year)) {
        return 29U;
    }
    return month_days[month - 1U];
}

bool datetime_is_valid(const rtc_datetime& datetime)
{
    return datetime.date.year >= 2000U && datetime.date.year <= 2099U &&
           datetime.date.month >= 1U && datetime.date.month <= 12U &&
           datetime.date.day >= 1U &&
           datetime.date.day <= days_in_month(datetime.date.year, datetime.date.month) &&
           datetime.time.hour <= 23U && datetime.time.minute <= 59U &&
           datetime.time.second <= 59U;
}

std::uint8_t calculate_weekday(const rtc_date& date)
{
    std::int32_t year = date.year;
    std::int32_t month = date.month;
    if (month < 3) {
        --year;
        month += 12;
    }
    const std::int32_t century = year / 100;
    return static_cast<std::uint8_t>(
        (year + year / 4 - century + century / 4 + (13 * month + 8) / 5 + date.day) % 7);
}

void advance_one_day(rtc_date& date)
{
    if (date.day < days_in_month(date.year, date.month)) {
        ++date.day;
        return;
    }

    date.day = 1U;
    if (date.month < 12U) {
        ++date.month;
        return;
    }

    date.month = 1U;
    date.year = date.year < 2099U ? date.year + 1U : 2000U;
}

void advance_datetime(rtc_datetime& datetime, std::uint32_t elapsed_seconds)
{
    const std::uint32_t current_seconds =
        static_cast<std::uint32_t>(datetime.time.hour) * 3600U +
        static_cast<std::uint32_t>(datetime.time.minute) * 60U +
        datetime.time.second;
    const std::uint32_t total_seconds = current_seconds + elapsed_seconds;
    std::uint32_t elapsed_days = total_seconds / 86400U;
    const std::uint32_t seconds_in_day = total_seconds % 86400U;

    datetime.time.hour = static_cast<std::uint8_t>(seconds_in_day / 3600U);
    datetime.time.minute = static_cast<std::uint8_t>((seconds_in_day % 3600U) / 60U);
    datetime.time.second = static_cast<std::uint8_t>(seconds_in_day % 60U);
    while (elapsed_days > 0U) {
        advance_one_day(datetime.date);
        --elapsed_days;
    }
}

void update_cache(const rtc_datetime& datetime)
{
    portENTER_CRITICAL(&cache_mutex);
    cached_datetime = datetime;
    cached_tick_ms = hal_get_tick_ms();
    ++cache_generation;
    cache_valid = true;
    portEXIT_CRITICAL(&cache_mutex);
}

bool read_hardware_datetime(rtc_datetime& datetime)
{
    m5::rtc_datetime_t hardware_datetime;
    if (!M5.Rtc.isEnabled() || !M5.Rtc.getDateTime(&hardware_datetime)) {
        return false;
    }

    datetime.date.year = static_cast<std::uint16_t>(hardware_datetime.date.year);
    datetime.date.month = static_cast<std::uint8_t>(hardware_datetime.date.month);
    datetime.date.day = static_cast<std::uint8_t>(hardware_datetime.date.date);
    datetime.time.hour = static_cast<std::uint8_t>(hardware_datetime.time.hours);
    datetime.time.minute = static_cast<std::uint8_t>(hardware_datetime.time.minutes);
    datetime.time.second = static_cast<std::uint8_t>(hardware_datetime.time.seconds);
    return datetime_is_valid(datetime);
}

bool write_hardware_datetime(const rtc_datetime& datetime)
{
    if (!M5.Rtc.isEnabled() || !datetime_is_valid(datetime)) {
        return false;
    }

    m5::rtc_date_t hardware_date(
        static_cast<std::int16_t>(datetime.date.year),
        static_cast<std::int8_t>(datetime.date.month),
        static_cast<std::int8_t>(datetime.date.day),
        static_cast<std::int8_t>(calculate_weekday(datetime.date)));
    m5::rtc_time_t hardware_time(
        static_cast<std::int8_t>(datetime.time.hour),
        static_cast<std::int8_t>(datetime.time.minute),
        static_cast<std::int8_t>(datetime.time.second));
    auto* rtc_instance = M5.Rtc.getRtcInstancePtr();
    return rtc_instance != nullptr && rtc_instance->setDateTime(&hardware_date, &hardware_time);
}

void publish_event(
    const rtc_request& request,
    bool success,
    const rtc_datetime& datetime)
{
    if (!request.publish_event) {
        return;
    }

    rtc_event event = {};
    event.operation = request.operation;
    event.request_id = request.request_id;
    event.datetime = datetime;
    event.success = success;
    xQueueOverwrite(event_queue, &event);
}

void rtc_task(void*)
{
    for (;;) {
        rtc_request request = {};
        xQueueReceive(request_queue, &request, portMAX_DELAY);

        rtc_datetime result_datetime = request.datetime;
        bool success = false;
        xSemaphoreTake(internal_i2c_mutex, portMAX_DELAY);
        if (request.operation == rtc_operation::read) {
            success = read_hardware_datetime(result_datetime);
        } else {
            success = write_hardware_datetime(request.datetime) &&
                      read_hardware_datetime(result_datetime);
        }
        xSemaphoreGive(internal_i2c_mutex);

        if (success) {
            update_cache(result_datetime);
        }
        publish_event(request, success, result_datetime);
        ESP_LOGI(
            log_tag,
            "operation=%s request_id=%lu success=%u",
            request.operation == rtc_operation::read ? "read" : "write",
            static_cast<unsigned long>(request.request_id),
            success ? 1U : 0U);
    }
}

bool submit_request(const rtc_request& request)
{
    return request_queue != nullptr && xQueueSend(request_queue, &request, 0) == pdTRUE;
}

}  // namespace

bool hal_rtc_start(SemaphoreHandle_t i2c_mutex, TaskHandle_t& task_handle)
{
    request_queue = xQueueCreate(RTC_REQUEST_QUEUE_LENGTH, sizeof(rtc_request));
    event_queue = xQueueCreate(RTC_EVENT_QUEUE_LENGTH, sizeof(rtc_event));
    internal_i2c_mutex = i2c_mutex;
    if (request_queue == nullptr || event_queue == nullptr || internal_i2c_mutex == nullptr) {
        return false;
    }

    const bool started = xTaskCreate(
                             rtc_task,
                             "rtc_task",
                             RTC_TASK_STACK_SIZE,
                             nullptr,
                             RTC_TASK_PRIORITY,
                             &task_handle) == pdPASS;
    if (!started) {
        return false;
    }

    rtc_request bootstrap_request = {};
    bootstrap_request.operation = rtc_operation::read;
    bootstrap_request.request_id = bootstrap_request_id;
    bootstrap_request.publish_event = false;
    submit_request(bootstrap_request);
    ESP_LOGI(log_tag, "RTC task started enabled=%u", M5.Rtc.isEnabled() ? 1U : 0U);
    return true;
}

bool hal_submit_rtc_read(std::uint32_t request_id)
{
    rtc_request request = {};
    request.operation = rtc_operation::read;
    request.request_id = request_id;
    request.publish_event = true;
    return submit_request(request);
}

bool hal_submit_rtc_write(
    const rtc_datetime& datetime,
    std::uint32_t request_id)
{
    rtc_request request = {};
    request.operation = rtc_operation::write;
    request.request_id = request_id;
    request.datetime = datetime;
    request.publish_event = true;
    return submit_request(request);
}

bool hal_try_get_rtc_event(rtc_event& event)
{
    return event_queue != nullptr && xQueueReceive(event_queue, &event, 0) == pdTRUE;
}

bool hal_get_cached_datetime(rtc_datetime& datetime)
{
    rtc_datetime base_datetime = {};
    std::uint32_t base_tick_ms = 0;
    std::uint32_t generation = 0;
    bool valid = false;

    portENTER_CRITICAL(&cache_mutex);
    base_datetime = cached_datetime;
    base_tick_ms = cached_tick_ms;
    generation = cache_generation;
    valid = cache_valid;
    portEXIT_CRITICAL(&cache_mutex);
    if (!valid) {
        return false;
    }

    const std::uint32_t now_ms = hal_get_tick_ms();
    const std::uint32_t elapsed_seconds = (now_ms - base_tick_ms) / 1000U;
    advance_datetime(base_datetime, elapsed_seconds);
    datetime = base_datetime;

    if (elapsed_seconds > 0U) {
        portENTER_CRITICAL(&cache_mutex);
        if (cache_valid && cache_generation == generation) {
            cached_datetime = base_datetime;
            cached_tick_ms += elapsed_seconds * 1000U;
        }
        portEXIT_CRITICAL(&cache_mutex);
    }
    return true;
}
