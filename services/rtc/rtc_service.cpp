#include "rtc_service.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "rtc.hpp"
#include "system_tick_service.hpp"

namespace {

constexpr char log_tag[] = "service_rtc";
constexpr std::uint32_t request_queue_length = 4U;
constexpr std::uint32_t event_queue_length = 4U;
constexpr std::uint32_t task_stack_size = 4096U;
constexpr UBaseType_t task_priority = 4U;

struct rtc_request {
    rtc_service_operation operation;
    std::uint32_t request_id;
    rtc_datetime datetime;
    bool publish_event;
};

QueueHandle_t request_queue = nullptr;
QueueHandle_t event_queue = nullptr;
portMUX_TYPE cache_mutex = portMUX_INITIALIZER_UNLOCKED;
rtc_datetime cached_datetime = {};
std::uint32_t cached_tick_ms = 0U;
std::uint32_t cache_generation = 0U;
bool cache_valid = false;

bool is_leap_year(std::uint16_t year)
{
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month)
{
    constexpr std::uint8_t month_days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month < 1U || month > 12U) {
        return 0U;
    }
    return month == 2U && is_leap_year(year) ? 29U : month_days[month - 1U];
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

void advance_datetime(
    rtc_datetime& datetime,
    std::uint32_t elapsed_seconds)
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
    cached_tick_ms = system_tick_now_ms();
    ++cache_generation;
    cache_valid = true;
    portEXIT_CRITICAL(&cache_mutex);
}

void publish_event(
    const rtc_request& request,
    bool success,
    const rtc_datetime& datetime)
{
    if (!request.publish_event) {
        return;
    }
    rtc_service_event event = {};
    event.operation = request.operation;
    event.request_id = request.request_id;
    event.datetime = datetime;
    event.success = success;
    if (xQueueSend(event_queue, &event, 0) != pdTRUE) {
        rtc_service_event discarded = {};
        xQueueReceive(event_queue, &discarded, 0);
        xQueueSend(event_queue, &event, 0);
    }
}

void rtc_task(void*)
{
    for (;;) {
        rtc_request request = {};
        if (xQueueReceive(request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        rtc_datetime result = request.datetime;
        bool success = false;
        if (request.operation == rtc_service_operation::read) {
            success = hal_rtc_read(result);
        } else {
            success = hal_rtc_write(request.datetime) && hal_rtc_read(result);
        }
        if (success) {
            update_cache(result);
        }
        publish_event(request, success, result);
        ESP_LOGI(
            log_tag,
            "operation=%u request=%lu success=%u",
            static_cast<unsigned>(request.operation),
            static_cast<unsigned long>(request.request_id),
            success ? 1U : 0U);
    }
}

bool submit_request(const rtc_request& request)
{
    return request_queue != nullptr && xQueueSend(request_queue, &request, 0) == pdTRUE;
}

}  // namespace

esp_err_t rtc_service_init()
{
    request_queue = xQueueCreate(request_queue_length, sizeof(rtc_request));
    event_queue = xQueueCreate(event_queue_length, sizeof(rtc_service_event));
    if (request_queue == nullptr || event_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            rtc_task,
            "rtc_service",
            task_stack_size,
            nullptr,
            task_priority,
            nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    rtc_request bootstrap = {};
    bootstrap.operation = rtc_service_operation::read;
    bootstrap.publish_event = false;
    submit_request(bootstrap);
    ESP_LOGI(log_tag, "RTC service started");
    return ESP_OK;
}

bool rtc_service_submit_read(std::uint32_t request_id)
{
    rtc_request request = {};
    request.operation = rtc_service_operation::read;
    request.request_id = request_id;
    request.publish_event = true;
    return submit_request(request);
}

bool rtc_service_submit_write(
    const rtc_datetime& datetime,
    std::uint32_t request_id)
{
    rtc_request request = {};
    request.operation = rtc_service_operation::write;
    request.request_id = request_id;
    request.datetime = datetime;
    request.publish_event = true;
    return submit_request(request);
}

bool rtc_service_try_get_event(rtc_service_event& event)
{
    return event_queue != nullptr && xQueueReceive(event_queue, &event, 0) == pdTRUE;
}

bool rtc_service_get_cached_datetime(rtc_datetime& datetime)
{
    rtc_datetime base = {};
    std::uint32_t base_tick_ms = 0U;
    std::uint32_t generation = 0U;
    bool valid = false;
    portENTER_CRITICAL(&cache_mutex);
    base = cached_datetime;
    base_tick_ms = cached_tick_ms;
    generation = cache_generation;
    valid = cache_valid;
    portEXIT_CRITICAL(&cache_mutex);
    if (!valid) {
        return false;
    }

    const std::uint32_t elapsed_seconds =
        (system_tick_now_ms() - base_tick_ms) / 1000U;
    advance_datetime(base, elapsed_seconds);
    datetime = base;
    if (elapsed_seconds > 0U) {
        portENTER_CRITICAL(&cache_mutex);
        if (cache_valid && generation == cache_generation) {
            cached_datetime = base;
            cached_tick_ms += elapsed_seconds * 1000U;
        }
        portEXIT_CRITICAL(&cache_mutex);
    }
    return true;
}
