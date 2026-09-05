#include "epub_archive.hpp"

#include <algorithm>
#include <cstring>

#include <esp_log.h>

namespace {
constexpr std::uint32_t local_signature = 0x04034b50U;
constexpr std::uint32_t central_signature = 0x02014b50U;
constexpr std::uint32_t eocd_signature = 0x06054b50U;
constexpr char log_tag[] = "epub_zip";

std::uint16_t u16(const std::uint8_t* bytes)
{
    return std::uint16_t(bytes[0]) | (std::uint16_t(bytes[1]) << 8U);
}

std::uint32_t u32(const std::uint8_t* bytes)
{
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8U) |
           (std::uint32_t(bytes[2]) << 16U) | (std::uint32_t(bytes[3]) << 24U);
}

esp_err_t exact_read(const epub_zip_archive& archive, std::uint64_t offset,
                     void* data, std::size_t capacity)
{
    std::size_t length = 0U; std::uint64_t size = 0U; std::int64_t mtime = 0;
    const auto error = archive.io().read(archive.io().context, archive.generation(), archive.path(),
                                         offset, data, capacity, length, size, mtime);
    if (error != ESP_OK) { return error; }
    return length == capacity && size == archive.identity().file_size &&
           mtime == archive.identity().modified_time ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
}  // namespace

void epub_zip_archive::close()
{
    entries_.reset();
    entry_count_ = 0U;
    path_[0] = '\0';
    identity_ = {};
    central_offset_ = 0U;
    generation_ = 0U;
}

esp_err_t epub_zip_archive::open(const char* path, std::uint32_t generation)
{
    close();
    if (!book_canonical_path(path, path_, sizeof(path_)) || !io_.media_valid(io_.context, generation)) {
        return ESP_ERR_INVALID_ARG;
    }
    generation_ = generation;
    std::uint8_t probe = 0U; std::size_t length = 0U;
    auto error = io_.read(io_.context, generation_, path_, 0U, &probe, 1U, length,
                          identity_.file_size, identity_.modified_time);
    if (error != ESP_OK) { close(); return error; }
    if (identity_.file_size < 22U || identity_.file_size > UINT32_MAX || identity_.modified_time < 0) {
        close(); return ESP_ERR_INVALID_SIZE;
    }
    const std::size_t tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(identity_.file_size, 65557U));
    auto tail = epub_allocate_array<std::uint8_t>(tail_size);
    if (!tail) { close(); return ESP_ERR_NO_MEM; }
    error = exact_read(*this, identity_.file_size - tail_size, tail.get(), tail_size);
    if (error != ESP_OK) { close(); return error; }
    std::size_t eocd = tail_size;
    for (std::size_t i = tail_size - 22U + 1U; i-- > 0U;) {
        if (u32(tail.get() + i) == eocd_signature &&
            i + 22U + u16(tail.get() + i + 20U) == tail_size) { eocd = i; break; }
    }
    if (eocd == tail_size || u16(tail.get() + eocd + 4U) != 0U ||
        u16(tail.get() + eocd + 6U) != 0U ||
        u16(tail.get() + eocd + 8U) != u16(tail.get() + eocd + 10U)) {
        close(); return ESP_ERR_NOT_SUPPORTED;
    }
    const std::uint32_t count = u16(tail.get() + eocd + 10U);
    const std::uint64_t central_size = u32(tail.get() + eocd + 12U);
    central_offset_ = u32(tail.get() + eocd + 16U);
    if (count == 0U || count > EPUB_ZIP_ENTRY_LIMIT || central_offset_ + central_size > identity_.file_size - tail_size + eocd) {
        close(); return ESP_ERR_INVALID_SIZE;
    }
    entries_ = epub_allocate_array<epub_zip_entry>(count);
    if (!entries_) { close(); return ESP_ERR_NO_MEM; }
    std::uint64_t cursor = central_offset_;
    for (std::uint32_t index = 0U; index < count; ++index) {
        std::uint8_t header[46] = {};
        error = exact_read(*this, cursor, header, sizeof(header));
        if (error != ESP_OK || u32(header) != central_signature) { close(); return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error; }
        const std::uint16_t name_length = u16(header + 28U);
        const std::uint16_t extra_length = u16(header + 30U);
        const std::uint16_t comment_length = u16(header + 32U);
        const std::uint64_t record_size = 46U + name_length + extra_length + comment_length;
        if (name_length == 0U || name_length >= EPUB_ARCHIVE_PATH_CAPACITY ||
            record_size > central_offset_ + central_size - cursor) { close(); return ESP_ERR_INVALID_SIZE; }
        char raw[EPUB_ARCHIVE_PATH_CAPACITY] = {};
        error = exact_read(*this, cursor + 46U, raw, name_length);
        if (error != ESP_OK) { close(); return error; }
        raw[name_length] = '\0';
        cursor += record_size;
        if (raw[name_length - 1U] == '/') { continue; }
        epub_zip_entry entry = {};
        if (!epub_normalize_archive_path(raw, entry.path, sizeof(entry.path))) { close(); return ESP_ERR_INVALID_ARG; }
        entry.flags = u16(header + 8U);
        entry.method = u16(header + 10U);
        entry.crc32 = u32(header + 16U);
        entry.compressed_size = u32(header + 20U);
        entry.uncompressed_size = u32(header + 24U);
        entry.local_header_offset = u32(header + 42U);
        if ((entry.flags & 0x0001U) != 0U || (entry.flags & 0x0040U) != 0U ||
            entry.local_header_offset >= central_offset_ ||
            std::any_of(entries_.get(), entries_.get() + entry_count_,
                        [&entry](const epub_zip_entry& other) {
                            return std::strcmp(other.path, entry.path) == 0;
                        })) { close(); return ESP_ERR_NOT_SUPPORTED; }
        entries_[entry_count_++] = entry;
    }
    if (cursor != central_offset_ + central_size || entry_count_ == 0U) { close(); return ESP_ERR_INVALID_SIZE; }
    const auto fingerprint = [&](unsigned sample, std::uint32_t& value) -> esp_err_t {
        const auto window = book_fingerprint_window(identity_.file_size, sample);
        auto bytes = epub_allocate_array<std::uint8_t>(std::max<std::size_t>(1U, window.length));
        if (!bytes) { return ESP_ERR_NO_MEM; }
        const auto read_error = exact_read(*this, window.offset, bytes.get(), window.length);
        if (read_error == ESP_OK) { value = book_crc32(bytes.get(), window.length); }
        return read_error;
    };
    if ((error = fingerprint(0U, identity_.fingerprint.head)) != ESP_OK ||
        (error = fingerprint(1U, identity_.fingerprint.middle)) != ESP_OK ||
        (error = fingerprint(2U, identity_.fingerprint.tail)) != ESP_OK) { close(); return error; }
    return ESP_OK;
}

const epub_zip_entry* epub_zip_archive::find(const char* path) const
{
    if (path == nullptr) { return nullptr; }
    for (std::size_t index = 0U; index < entry_count_; ++index) {
        if (std::strcmp(entries_[index].path, path) == 0) { return &entries_[index]; }
    }
    return nullptr;
}

void epub_zip_stream::reset()
{
    archive_ = nullptr; entry_ = {}; sink_ = {};
    dictionary_.reset(); decompressor_.reset();
    input_offset_ = input_length_ = dictionary_offset_ = 0U;
    pending_output_offset_ = pending_output_length_ = 0U;
    data_offset_ = compressed_read_ = compressed_consumed_ = output_size_ = 0U;
    crc32_ = 0U; done_ = decompression_done_ = false;
}

esp_err_t epub_zip_stream::begin(const epub_zip_archive& archive, const epub_zip_entry& entry,
                                 epub_stream_sink sink)
{
    reset();
    if (sink.write == nullptr || (entry.method != 0U && entry.method != MZ_DEFLATED) ||
        entry.uncompressed_size > EPUB_CONTENT_SIZE_LIMIT ||
        (entry.method == 0U && entry.compressed_size != entry.uncompressed_size)) { return ESP_ERR_NOT_SUPPORTED; }
    std::uint8_t header[30] = {};
    auto error = exact_read(archive, entry.local_header_offset, header, sizeof(header));
    const std::uint16_t local_flags = error == ESP_OK ? u16(header + 6U) : 0U;
    const std::uint16_t name_length = error == ESP_OK ? u16(header + 26U) : 0U;
    if (error != ESP_OK || u32(header) != local_signature || u16(header + 8U) != entry.method ||
        local_flags != entry.flags || (local_flags & 0x0001U) != 0U || name_length == 0U ||
        name_length >= EPUB_ARCHIVE_PATH_CAPACITY) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    if ((local_flags & 0x0008U) == 0U &&
        (u32(header + 14U) != entry.crc32 || u32(header + 18U) != entry.compressed_size ||
         u32(header + 22U) != entry.uncompressed_size)) { return ESP_ERR_INVALID_RESPONSE; }
    char local_name[EPUB_ARCHIVE_PATH_CAPACITY] = {};
    error = exact_read(archive, entry.local_header_offset + sizeof(header), local_name, name_length);
    if (error != ESP_OK) { return error; }
    local_name[name_length] = '\0';
    char normalized[EPUB_ARCHIVE_PATH_CAPACITY] = {};
    if (!epub_normalize_archive_path(local_name, normalized, sizeof(normalized)) ||
        std::strcmp(normalized, entry.path) != 0) { return ESP_ERR_INVALID_RESPONSE; }
    data_offset_ = entry.local_header_offset + sizeof(header) + name_length + u16(header + 28U);
    if (data_offset_ > archive.central_offset() || entry.compressed_size > archive.central_offset() - data_offset_) {
        return ESP_ERR_INVALID_SIZE;
    }
    archive_ = &archive; entry_ = entry; sink_ = sink;
    if (entry.method == MZ_DEFLATED) {
        dictionary_ = epub_allocate_array<std::uint8_t>(TINFL_LZ_DICT_SIZE);
        decompressor_ = epub_allocate_array<tinfl_decompressor>(1U);
        if (!dictionary_ || !decompressor_) { reset(); return ESP_ERR_NO_MEM; }
        tinfl_init(decompressor_.get());
    }
    if (entry.uncompressed_size == 0U && entry.method == 0U) { return finish(); }
    return ESP_OK;
}

esp_err_t epub_zip_stream::fill_input()
{
    if (input_offset_ < input_length_) { return ESP_OK; }
    if (compressed_read_ == entry_.compressed_size) { return ESP_ERR_INVALID_SIZE; }
    input_offset_ = input_length_ = 0U;
    const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(sizeof(input_), entry_.compressed_size - compressed_read_));
    auto error = exact_read(*archive_, data_offset_ + compressed_read_, input_, amount);
    if (error == ESP_OK) { input_length_ = amount; compressed_read_ += amount; }
    return error;
}

esp_err_t epub_zip_stream::finish()
{
    if (output_size_ != entry_.uncompressed_size || crc32_ != entry_.crc32 ||
        compressed_consumed_ != entry_.compressed_size) { return ESP_ERR_INVALID_CRC; }
    done_ = true;
    dictionary_.reset(); decompressor_.reset();
    return ESP_OK;
}

esp_err_t epub_zip_stream::deliver_pending()
{
    const auto amount = std::min<std::size_t>(EPUB_ZIP_IO_BUFFER_SIZE, pending_output_length_);
    if (output_size_ > entry_.uncompressed_size || amount > entry_.uncompressed_size - output_size_) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!sink_.write(sink_.context, dictionary_.get() + pending_output_offset_, amount)) { return ESP_FAIL; }
    crc32_ = book_crc32(dictionary_.get() + pending_output_offset_, amount, crc32_);
    output_size_ += amount;
    pending_output_offset_ += amount;
    pending_output_length_ -= amount;
    if (pending_output_length_ == 0U && decompression_done_) { return finish(); }
    return ESP_OK;
}

esp_err_t epub_zip_stream::step()
{
    if (archive_ == nullptr || done_ || !archive_->io().media_valid(archive_->io().context, archive_->generation())) {
        return done_ ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (pending_output_length_ != 0U) { return deliver_pending(); }
    auto error = fill_input();
    if (error != ESP_OK) { return error; }
    if (entry_.method == 0U) {
        const std::size_t amount = std::min<std::size_t>(EPUB_ZIP_IO_BUFFER_SIZE, input_length_ - input_offset_);
        if (!sink_.write(sink_.context, input_ + input_offset_, amount)) { return ESP_FAIL; }
        crc32_ = book_crc32(input_ + input_offset_, amount, crc32_);
        input_offset_ += amount; compressed_consumed_ += amount; output_size_ += amount;
        return output_size_ == entry_.uncompressed_size ? finish() : ESP_OK;
    }
    const std::size_t available_input = input_length_ - input_offset_;
    std::size_t consumed = available_input;
    const std::size_t output_capacity = TINFL_LZ_DICT_SIZE - dictionary_offset_;
    std::size_t produced = output_capacity;
    const std::size_t produced_at = dictionary_offset_;
    const bool more = compressed_read_ < entry_.compressed_size;
    const auto status = tinfl_decompress(decompressor_.get(), input_ + input_offset_, &consumed,
        dictionary_.get(), dictionary_.get() + dictionary_offset_, &produced,
        more ? static_cast<mz_uint32>(TINFL_FLAG_HAS_MORE_INPUT) : mz_uint32{0U});
    input_offset_ += consumed; compressed_consumed_ += consumed;
    pending_output_offset_ = produced_at;
    pending_output_length_ = produced;
    dictionary_offset_ = (dictionary_offset_ + produced) & (TINFL_LZ_DICT_SIZE - 1U);
    if (status < TINFL_STATUS_DONE) {
        ESP_LOGW(log_tag, "deflate failed status=%d in=%u/%llu out=%u/%llu dict=%u",
                 static_cast<int>(status), static_cast<unsigned>(consumed),
                 static_cast<unsigned long long>(entry_.compressed_size),
                 static_cast<unsigned>(produced),
                 static_cast<unsigned long long>(entry_.uncompressed_size),
                 static_cast<unsigned>(dictionary_offset_));
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (status == TINFL_STATUS_DONE) { decompression_done_ = true; }
    if (status != TINFL_STATUS_DONE && consumed == 0U && produced == 0U) { return ESP_ERR_INVALID_SIZE; }
    if (pending_output_length_ != 0U) { return deliver_pending(); }
    return decompression_done_ ? finish() : ESP_OK;
}

namespace {
struct memory_sink_context { std::uint8_t* data; std::size_t capacity; std::size_t used; };
bool memory_write(void* context, const std::uint8_t* data, std::size_t length)
{
    auto& value = *static_cast<memory_sink_context*>(context);
    if (length > value.capacity - value.used) { return false; }
    std::memcpy(value.data + value.used, data, length); value.used += length; return true;
}
}

esp_err_t epub_zip_extract_memory(const epub_zip_archive& archive,
                                  const epub_zip_entry& entry,
                                  std::uint8_t* output, std::size_t capacity)
{
    if (entry.uncompressed_size > capacity || (output == nullptr && capacity != 0U)) { return ESP_ERR_INVALID_SIZE; }
    memory_sink_context context = {output, capacity, 0U};
    epub_zip_stream stream;
    auto error = stream.begin(archive, entry, {&context, memory_write});
    while (error == ESP_OK && !stream.done()) { error = stream.step(); }
    return error == ESP_OK && context.used == entry.uncompressed_size ? ESP_OK :
           error == ESP_OK ? ESP_ERR_INVALID_SIZE : error;
}
