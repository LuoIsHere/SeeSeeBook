#include "book_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <cJSON.h>

#include "utf8.hpp"

namespace {
constexpr std::uint8_t index_magic[8] = {'S', 'S', 'B', 'I', 'D', 'X', '0', '1'};

void encode_u32(std::uint8_t* bytes, std::uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) { bytes[i] = static_cast<std::uint8_t>(value >> (8U * i)); }
}

std::uint32_t decode_u32(const std::uint8_t* bytes)
{
    std::uint32_t value = 0U;
    for (unsigned i = 0; i < 4U; ++i) { value |= std::uint32_t(bytes[i]) << (8U * i); }
    return value;
}

bool number(const cJSON* object, const char* key, std::uint64_t maximum, std::uint64_t& value)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > static_cast<double>(maximum) ||
        std::floor(item->valuedouble) != item->valuedouble) { return false; }
    value = static_cast<std::uint64_t>(item->valuedouble);
    return true;
}

// Bound cJSON recursion and allocation even for malformed/untrusted SD files.
bool bounded_json(const char* text, std::size_t size)
{
    if (text == nullptr || size == 0U || size >= BOOK_METADATA_CAPACITY) { return false; }
    unsigned depth = 0U, tokens = 0U;
    bool string = false, escape = false;
    for (std::size_t i = 0; i < size; ++i) {
        const unsigned char c = text[i];
        if (c == 0U) { return false; }
        if (string) {
            if (escape) {
                if (c == 'u' && i + 4U < size && std::memcmp(text + i + 1U, "0000", 4U) == 0) {
                    return false;
                }
                escape = false;
            } else if (c == '\\') { escape = true; }
            else if (c == '"') { string = false; }
            else if (c < 0x20U) { return false; }
        } else if (c == '"') { string = true; ++tokens; }
        else if (c == '{' || c == '[') { if (++depth > 8U) { return false; } ++tokens; }
        else if (c == '}' || c == ']') { if (depth == 0U) { return false; } --depth; }
        else if (c == ',' || c == ':') { ++tokens; }
        if (tokens > 96U) { return false; }
    }
    return depth == 0U && !string;
}

bool unique_keys(const cJSON* object)
{
    if (!cJSON_IsObject(object)) { return false; }
    for (const cJSON* item = object->child; item != nullptr; item = item->next) {
        for (const cJSON* other = item->next; other != nullptr; other = other->next) {
            if (std::strcmp(item->string, other->string) == 0) { return false; }
        }
    }
    return true;
}

bool metadata_valid(const book_metadata& value)
{
    char canonical[BOOK_PATH_CAPACITY] = {};
    return book_canonical_path(value.canonical_path, canonical, sizeof(canonical)) &&
           std::strcmp(canonical, value.canonical_path) == 0 &&
           value.file.file_size <= BOOK_JSON_INTEGER_MAX && value.file.modified_time >= 0 &&
           static_cast<std::uint64_t>(value.file.modified_time) <= BOOK_JSON_INTEGER_MAX &&
           value.index_format_version != 0U && value.pagination_version != 0U &&
           (value.progress.byte_offset == 0U || value.progress.byte_offset < value.file.file_size) &&
           (value.index_complete ? value.page_count > 0U &&
                value.page_count <= std::max<std::uint64_t>(1U, value.file.file_size) &&
                value.progress.page < value.page_count
                : value.page_count == 0U && value.progress.page == 0U);
}
}  // namespace

bool book_canonical_path(const char* source, char* destination, std::size_t capacity)
{
    if (source == nullptr || source[0] != '/' || capacity < 2U) { return false; }
    const std::size_t size = strnlen(source, BOOK_PATH_CAPACITY);
    if (size >= BOOK_PATH_CAPACITY) { return false; }
    for (std::size_t i = 0U; i < size;) {
        std::uint32_t codepoint = 0U;
        std::size_t length = 0U;
        if (utf8_decode(source + i, size - i, codepoint, length) != utf8_decode_result::complete ||
            codepoint < 0x20U || codepoint == 0x7fU || codepoint == '\\') { return false; }
        i += length;
    }
    std::size_t used = 1U;
    destination[0] = '/';
    for (std::size_t i = 1U; i < size;) {
        while (i < size && source[i] == '/') { ++i; }
        const auto start = i;
        while (i < size && source[i] != '/') { ++i; }
        const auto length = i - start;
        if (length == 0U) { break; }
        if ((length == 1U && source[start] == '.') ||
            (length == 2U && source[start] == '.' && source[start + 1U] == '.')) { return false; }
        const auto separator = used > 1U ? 1U : 0U;
        if (used + separator + length >= capacity) { return false; }
        if (separator != 0U) { destination[used++] = '/'; }
        std::memcpy(destination + used, source + start, length);
        used += length;
    }
    destination[used] = '\0';
    return used > 1U;
}

bool book_metadata_decode(const char* json, std::size_t length, book_metadata& output)
{
    output = {};
    if (!bounded_json(json, length)) { return false; }
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (root == nullptr) { return false; }
    while (end != nullptr && end < json + length &&
           (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) { ++end; }
    const auto* file = cJSON_GetObjectItemCaseSensitive(root, "file");
    const auto* fingerprint = cJSON_GetObjectItemCaseSensitive(file, "fingerprint");
    const auto* pagination = cJSON_GetObjectItemCaseSensitive(root, "pagination");
    const auto* progress = cJSON_GetObjectItemCaseSensitive(root, "progress");
    const auto* path = cJSON_GetObjectItemCaseSensitive(root, "canonical_path");
    const auto* algorithm = cJSON_GetObjectItemCaseSensitive(fingerprint, "algorithm");
    const auto* complete = cJSON_GetObjectItemCaseSensitive(pagination, "complete");
    std::uint64_t schema = 0U, mtime = 0U, head = 0U, middle = 0U, tail = 0U;
    std::uint64_t format = 0U, version = 0U, count = 0U, crc = 0U, page = 0U;
    book_metadata value = {};
    bool valid = end == json + length && unique_keys(root) && unique_keys(file) &&
        unique_keys(fingerprint) && unique_keys(pagination) && unique_keys(progress) &&
        number(root, "schema_version", UINT32_MAX, schema) && schema == BOOK_METADATA_SCHEMA_VERSION &&
        cJSON_IsString(path) && path->valuestring != nullptr &&
        std::strlen(path->valuestring) < sizeof(value.canonical_path) &&
        cJSON_IsString(algorithm) && std::strcmp(algorithm->valuestring, BOOK_FINGERPRINT_ALGORITHM) == 0 &&
        number(file, "size", BOOK_JSON_INTEGER_MAX, value.file.file_size) &&
        number(file, "mtime", BOOK_JSON_INTEGER_MAX, mtime) &&
        number(fingerprint, "head", UINT32_MAX, head) &&
        number(fingerprint, "middle", UINT32_MAX, middle) &&
        number(fingerprint, "tail", UINT32_MAX, tail) &&
        number(pagination, "index_format_version", UINT32_MAX, format) &&
        number(pagination, "pagination_version", UINT32_MAX, version) &&
        number(pagination, "page_count", UINT32_MAX, count) &&
        number(pagination, "offsets_crc32", UINT32_MAX, crc) && cJSON_IsBool(complete) &&
        number(progress, "page", UINT32_MAX, page) &&
        number(progress, "byte_offset", BOOK_JSON_INTEGER_MAX, value.progress.byte_offset);
    if (valid) {
        std::strcpy(value.canonical_path, path->valuestring);
        value.file.modified_time = static_cast<std::int64_t>(mtime);
        value.file.fingerprint = {std::uint32_t(head), std::uint32_t(middle), std::uint32_t(tail)};
        value.index_format_version = format;
        value.pagination_version = version;
        value.page_count = count;
        value.offsets_crc32 = crc;
        value.progress.page = page;
        value.index_complete = cJSON_IsTrue(complete);
        valid = metadata_valid(value);
    }
    cJSON_Delete(root);
    if (valid) { output = value; }
    return valid;
}

bool book_metadata_encode(const book_metadata& value, char* json, std::size_t capacity)
{
    if (json == nullptr || capacity == 0U || capacity > BOOK_METADATA_CAPACITY || !metadata_valid(value)) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) { return false; }
    auto* file = cJSON_AddObjectToObject(root, "file");
    auto* pagination = cJSON_AddObjectToObject(root, "pagination");
    auto* progress = cJSON_AddObjectToObject(root, "progress");
    auto* fingerprint = file == nullptr ? nullptr : cJSON_AddObjectToObject(file, "fingerprint");
    const bool valid = file != nullptr && pagination != nullptr && progress != nullptr && fingerprint != nullptr &&
        cJSON_AddNumberToObject(root, "schema_version", BOOK_METADATA_SCHEMA_VERSION) &&
        cJSON_AddStringToObject(root, "canonical_path", value.canonical_path) &&
        cJSON_AddNumberToObject(file, "size", static_cast<double>(value.file.file_size)) &&
        cJSON_AddNumberToObject(file, "mtime", static_cast<double>(value.file.modified_time)) &&
        cJSON_AddStringToObject(fingerprint, "algorithm", BOOK_FINGERPRINT_ALGORITHM) &&
        cJSON_AddNumberToObject(fingerprint, "head", value.file.fingerprint.head) &&
        cJSON_AddNumberToObject(fingerprint, "middle", value.file.fingerprint.middle) &&
        cJSON_AddNumberToObject(fingerprint, "tail", value.file.fingerprint.tail) &&
        cJSON_AddNumberToObject(pagination, "index_format_version", value.index_format_version) &&
        cJSON_AddNumberToObject(pagination, "pagination_version", value.pagination_version) &&
        cJSON_AddNumberToObject(pagination, "page_count", value.page_count) &&
        cJSON_AddNumberToObject(pagination, "offsets_crc32", value.offsets_crc32) &&
        cJSON_AddBoolToObject(pagination, "complete", value.index_complete) &&
        cJSON_AddNumberToObject(progress, "page", value.progress.page) &&
        cJSON_AddNumberToObject(progress, "byte_offset", static_cast<double>(value.progress.byte_offset)) &&
        cJSON_PrintPreallocated(root, json, static_cast<int>(capacity), true);
    cJSON_Delete(root);
    return valid;
}

std::uint32_t book_crc32(const void* data, std::size_t length, std::uint32_t previous)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = previous ^ UINT32_MAX;
    for (std::size_t i = 0U; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8U; ++bit) { crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U))); }
    }
    return crc ^ UINT32_MAX;
}

book_sample_window book_fingerprint_window(std::uint64_t size, unsigned sample)
{
    const auto length = std::min<std::uint64_t>(size, 4096U);
    // Head=0; middle=clamp(size/2-2048, 0, size-length); tail=size-length.
    const auto center_start = size / 2U > 2048U ? size / 2U - 2048U : 0U;
    return {sample == 0U ? 0U : sample == 1U ? std::min(center_start, size - length) : size - length,
            static_cast<std::size_t>(length)};
}

bool book_fingerprint_equal(const book_fingerprint& left, const book_fingerprint& right)
{
    return left.head == right.head && left.middle == right.middle && left.tail == right.tail;
}

book_cache_decision book_cache_check(const book_metadata& metadata, const char* path,
                                   std::uint64_t size, std::int64_t mtime)
{
    if (std::strcmp(metadata.canonical_path, path) != 0 || !metadata.index_complete ||
        metadata.index_format_version != BOOK_PAGE_INDEX_FORMAT_VERSION ||
        metadata.pagination_version != BOOK_PAGINATION_VERSION || metadata.file.file_size != size) {
        return book_cache_decision::rebuild;
    }
    return metadata.file.modified_time == mtime ? book_cache_decision::reuse : book_cache_decision::fingerprint;
}

void book_encode_u64(std::uint8_t* destination, std::uint64_t value)
{
    for (unsigned i = 0; i < 8U; ++i) { destination[i] = static_cast<std::uint8_t>(value >> (8U * i)); }
}

std::uint64_t book_decode_u64(const std::uint8_t* source)
{
    std::uint64_t value = 0U;
    for (unsigned i = 0; i < 8U; ++i) { value |= std::uint64_t(source[i]) << (8U * i); }
    return value;
}

void book_index_encode(const book_index_header& value, std::uint8_t* bytes)
{
    std::memset(bytes, 0, BOOK_INDEX_HEADER_SIZE);
    std::memcpy(bytes, index_magic, sizeof(index_magic));
    encode_u32(bytes + 8U, BOOK_PAGE_INDEX_FORMAT_VERSION);
    encode_u32(bytes + 12U, BOOK_PAGINATION_VERSION);
    encode_u32(bytes + 16U, BOOK_INDEX_OFFSET_WIDTH);
    encode_u32(bytes + 20U, value.page_count);
    book_encode_u64(bytes + 24U, value.file_size);
    encode_u32(bytes + 32U, value.offsets_crc32);
    encode_u32(bytes + 36U, book_crc32(bytes, 36U));
}

bool book_index_decode(const std::uint8_t* bytes, std::size_t length,
                       std::uint64_t index_size, std::uint64_t text_size,
                       book_index_header& header)
{
    header = {};
    if (length < BOOK_INDEX_HEADER_SIZE || std::memcmp(bytes, index_magic, sizeof(index_magic)) != 0 ||
        decode_u32(bytes + 8U) != BOOK_PAGE_INDEX_FORMAT_VERSION ||
        decode_u32(bytes + 12U) != BOOK_PAGINATION_VERSION ||
        decode_u32(bytes + 16U) != BOOK_INDEX_OFFSET_WIDTH ||
        decode_u32(bytes + 36U) != book_crc32(bytes, 36U) || book_decode_u64(bytes + 40U) != 0U) { return false; }
    header = {decode_u32(bytes + 20U), book_decode_u64(bytes + 24U), decode_u32(bytes + 32U)};
    return header.page_count != 0U && header.file_size == text_size &&
           header.page_count <= std::max<std::uint64_t>(1U, text_size) &&
           index_size == BOOK_INDEX_HEADER_SIZE + std::uint64_t(header.page_count) * BOOK_INDEX_OFFSET_WIDTH;
}

bool book_index_offset_valid(std::uint64_t offset, std::uint64_t previous,
                             std::uint32_t page, std::uint64_t file_size)
{
    return page == 0U ? offset == 0U : offset > previous && offset < file_size;
}

static_assert(sizeof(std::uint64_t) == BOOK_INDEX_OFFSET_WIDTH);
static_assert(BOOK_INDEX_HEADER_SIZE == 48U);
