#pragma once

#include "book_types.hpp"

constexpr std::size_t BOOK_METADATA_CAPACITY = 4096U;
constexpr std::size_t BOOK_INDEX_HEADER_SIZE = 48U;
constexpr std::size_t BOOK_INDEX_OFFSET_WIDTH = 8U;
constexpr char BOOK_FINGERPRINT_ALGORITHM[] = "crc32-4k-3point-v1";
// JSON numbers must be exact in cJSON's double representation.
constexpr std::uint64_t BOOK_JSON_INTEGER_MAX = 9007199254740991ULL;

struct book_metadata {
    char canonical_path[BOOK_PATH_CAPACITY];
    book_file_identity file;
    std::uint32_t index_format_version;
    std::uint32_t pagination_version;
    std::uint32_t page_count;
    std::uint32_t offsets_crc32;
    book_progress progress;
    bool index_complete;
};

struct book_index_header {
    std::uint32_t page_count;
    std::uint64_t file_size;
    std::uint32_t offsets_crc32;
};

struct book_sample_window {
    std::uint64_t offset;
    std::size_t length;
};

enum class book_cache_decision { rebuild, reuse, fingerprint };

bool book_canonical_path(const char* source, char* destination, std::size_t capacity);
bool book_metadata_decode(const char* json, std::size_t length, book_metadata& output);
bool book_metadata_encode(const book_metadata& value, char* json, std::size_t capacity);
std::uint32_t book_crc32(const void* data, std::size_t length, std::uint32_t previous = 0U);
book_sample_window book_fingerprint_window(std::uint64_t size, unsigned sample);
bool book_fingerprint_equal(const book_fingerprint& left, const book_fingerprint& right);
book_cache_decision book_cache_check(const book_metadata& metadata, const char* path,
                                   std::uint64_t size, std::int64_t mtime);
void book_encode_u64(std::uint8_t* destination, std::uint64_t value);
std::uint64_t book_decode_u64(const std::uint8_t* source);
void book_index_encode(const book_index_header& value, std::uint8_t* bytes);
bool book_index_decode(const std::uint8_t* bytes, std::size_t length,
                       std::uint64_t index_size, std::uint64_t text_size,
                       book_index_header& header);
bool book_index_offset_valid(std::uint64_t offset, std::uint64_t previous,
                             std::uint32_t page, std::uint64_t file_size);
