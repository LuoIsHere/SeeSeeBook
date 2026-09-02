#include "storage_file_reader.hpp"

#include "storage.hpp"

storage_result_code storage_read_file_chunk(
    const char* path, storage_file_chunk_result& result)
{
    if (storage_service_get_state() != storage_state::ready ||
        storage_service_get_media_generation() != result.media_generation) {
        return storage_result_code::cancelled;
    }
    std::size_t length = 0U;
    const esp_err_t error = hal_storage_read_file_chunk(
        path, result.offset, result.data, sizeof(result.data), length,
        result.file_size, result.modified_time);
    if (storage_service_get_state() != storage_state::ready ||
        storage_service_get_media_generation() != result.media_generation) {
        return storage_result_code::cancelled;
    }
    if (error != ESP_OK) {
        return error == ESP_ERR_NOT_FOUND ? storage_result_code::file_not_found
             : error == ESP_ERR_INVALID_STATE ? storage_result_code::no_card
                                              : storage_result_code::io_error;
    }
    result.length = static_cast<std::uint16_t>(length);
    result.end_of_file = result.offset + length >= result.file_size;
    return storage_result_code::ok;
}
