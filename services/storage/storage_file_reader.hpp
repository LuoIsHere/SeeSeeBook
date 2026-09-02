#pragma once

#include "storage_service.hpp"

storage_result_code storage_read_file_chunk(
    const char* path, storage_file_chunk_result& result);
