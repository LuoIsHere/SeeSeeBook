#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <esp_err.h>

#include "result_handle.hpp"
#include "storage_state.hpp"

#define STORAGE_RESULT_POOL_SIZE 4U
#define STORAGE_MAX_DIRECTORY_ENTRIES 1024U
#define STORAGE_INITIAL_ENTRY_CAPACITY 64U
#define STORAGE_MAX_PATH_LENGTH 512U
#define STORAGE_MAX_FILE_NAME_LENGTH 255U
#define STORAGE_FILE_CHUNK_CAPACITY 2048U

enum class file_entry_type : std::uint8_t {
    directory,
    file,
};

struct file_entry {
    std::string name;
    file_entry_type type;
    std::uint64_t size;
};

enum class storage_result_code : std::uint8_t {
    ok,
    no_card,
    cancelled,
    invalid_path,
    too_many_entries,
    io_error,
    no_memory,
    file_not_found,
};

struct storage_directory_result {
    std::uint32_t request_id;
    std::uint32_t session_id;
    std::uint32_t media_generation;
    storage_result_code code;
    std::string path;
    std::vector<file_entry> entries;
};

struct storage_file_chunk_result {
    std::uint32_t request_id;
    std::uint32_t session_id;
    std::uint32_t media_generation;
    storage_result_code code;
    std::uint64_t offset;
    std::uint64_t file_size;
    std::int64_t modified_time;
    std::uint16_t length;
    bool end_of_file;
    char data[STORAGE_FILE_CHUNK_CAPACITY];
};

static_assert(std::is_trivially_copyable_v<storage_file_chunk_result>);

esp_err_t storage_service_init();
storage_state storage_service_get_state();
std::uint32_t storage_service_get_media_generation();

bool storage_service_list_directory(
    const char* path,
    std::uint32_t request_id,
    std::uint32_t session_id);

bool storage_service_read_file_chunk(
    const char* path,
    std::uint64_t offset,
    std::uint32_t request_id,
    std::uint32_t session_id,
    std::uint32_t media_generation);

// Resolve returns task-local access only; the pointer must never enter a queue.
bool storage_service_resolve_result(
    const result_handle& handle,
    const storage_directory_result*& result);
bool storage_service_resolve_file_result(
    const result_handle& handle,
    const storage_file_chunk_result*& result);
bool storage_service_retain_result(const result_handle& handle);
// Release is non-blocking and never depends on the result-pool mutex.
bool storage_service_release_result(result_handle& handle);
