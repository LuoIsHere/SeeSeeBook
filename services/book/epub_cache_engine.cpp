#include "epub_cache_engine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
epub_archive_io archive_io(const book_engine_io& io)
{
    return {io.context, io.read, io.media_valid};
}
}

epub_cache_engine::epub_cache_engine(const book_engine_io& io)
    : io_(io), archive_(archive_io(io))
{
}

void epub_cache_engine::cancel()
{
    stream_.reset();
    archive_.close();
    package_ = {};
    filter_.reset();
    phase_ = phase::idle;
}

bool epub_cache_engine::working() const
{
    return phase_ == phase::cover || phase_ == phase::spine || phase_ == phase::finalize;
}

bool epub_cache_engine::ready() const
{
    return phase_ == phase::ready;
}

bool epub_cache_engine::prepare_directory()
{
    for (const char* path : {"/.system", "/.system/books", static_cast<const char*>(directory_)}) {
        if (io_.mkdir(io_.context, generation_, path) != ESP_OK) { return false; }
    }
    return true;
}

bool epub_cache_engine::source_identity(const char* path, book_file_identity& identity, bool fingerprint)
{
    std::uint8_t byte = 0U; std::size_t length = 0U;
    auto error = io_.read(io_.context, generation_, path, 0U, &byte, 1U, length,
                          identity.file_size, identity.modified_time);
    if (error != ESP_OK || identity.file_size < 22U || identity.file_size > UINT32_MAX || identity.modified_time < 0) {
        return false;
    }
    if (!fingerprint) { return true; }
    for (unsigned sample = 0U; sample < 3U; ++sample) {
        const auto window = book_fingerprint_window(identity.file_size, sample);
        auto bytes = epub_allocate_array<std::uint8_t>(
            std::max<std::size_t>(1U, window.length));
        if (!bytes) { return false; }
        std::uint64_t size = 0U; std::int64_t mtime = 0;
        error = io_.read(io_.context, generation_, path, window.offset, bytes.get(), window.length,
                         length, size, mtime);
        if (error != ESP_OK || length != window.length || size != identity.file_size ||
            mtime != identity.modified_time) { return false; }
        const auto crc = book_crc32(bytes.get(), window.length);
        if (sample == 0U) { identity.fingerprint.head = crc; }
        else if (sample == 1U) { identity.fingerprint.middle = crc; }
        else { identity.fingerprint.tail = crc; }
    }
    return true;
}

bool epub_cache_engine::validate_file(const char* path, std::uint64_t expected_size)
{
    std::uint8_t probe = 0U; std::size_t length = 0U; std::uint64_t size = 0U; std::int64_t mtime = 0;
    return io_.read(io_.context, generation_, path, 0U, &probe, 1U, length, size, mtime) == ESP_OK &&
           size == expected_size && length == std::min<std::uint64_t>(1U, expected_size);
}

bool epub_cache_engine::load_map()
{
    std::size_t length = 0U; std::uint64_t size = 0U; std::int64_t mtime = 0;
    const auto error = io_.read(io_.context, generation_, map_path_, 0U, map_bytes_, sizeof(map_bytes_),
                                length, size, mtime);
    return error == ESP_OK && length == size &&
           epub_spine_map_decode(map_bytes_, length, metadata_.content_size, starts_,
                                 EPUB_SPINE_ITEM_LIMIT, starts_count_) &&
           starts_count_ == metadata_.spine_count;
}

bool epub_cache_engine::write_metadata()
{
    if (!epub_cache_metadata_encode(metadata_, json_, sizeof(json_))) { return false; }
    char temporary[132] = {};
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", metadata_path_);
    auto error = io_.write(io_.context, generation_, temporary, 0U, json_, std::strlen(json_), true);
    if (error == ESP_OK) { error = io_.replace(io_.context, generation_, temporary, metadata_path_); }
    return error == ESP_OK;
}

bool epub_cache_engine::validate_cached(const char* path)
{
    std::size_t length = 0U; std::uint64_t size = 0U; std::int64_t mtime = 0;
    if (io_.read(io_.context, generation_, metadata_path_, 0U, json_, sizeof(json_),
                 length, size, mtime) != ESP_OK || length != size ||
        !epub_cache_metadata_decode(json_, length, metadata_) ||
        std::strcmp(metadata_.canonical_path, path) != 0 ||
        metadata_.parser_version != EPUB_PARSER_VERSION) { return false; }
    book_file_identity current = {};
    if (!source_identity(path, current, true) || current.file_size != metadata_.source.file_size ||
        !book_fingerprint_equal(current.fingerprint, metadata_.source.fingerprint)) { return false; }
    bool changed_stat = current.modified_time != metadata_.source.modified_time;
    if (!validate_file(content_path_, metadata_.content_size) || !load_map() ||
        (metadata_.cover_encoding != book_cover_encoding::none &&
         !validate_file(cover_path_, metadata_.cover_size))) { return false; }
    const auto progress = epub_position_from_linear(starts_, starts_count_,
                                                     metadata_.progress.linear_offset);
    if (progress.spine_index != metadata_.progress.spine_index ||
        progress.content_offset != metadata_.progress.content_offset) { return false; }
    bool dirty = changed_stat;
    if (changed_stat) { metadata_.source = current; }
    if (metadata_.pagination_version != BOOK_PAGINATION_VERSION) {
        metadata_.pagination_version = BOOK_PAGINATION_VERSION;
        metadata_.progress = {0U, 0U, 0U};
        rebuilt_ = true;
        dirty = true;
    }
    if (dirty && !write_metadata()) { return false; }
    return true;
}

esp_err_t epub_cache_engine::extract_xml(const char* path, std::size_t limit,
                                         epub_unique_array<std::uint8_t>& bytes,
                                         std::size_t& length)
{
    const auto* entry = archive_.find(path);
    if (entry == nullptr) { return ESP_ERR_NOT_FOUND; }
    if (entry->uncompressed_size == 0U || entry->uncompressed_size > limit) { return ESP_ERR_INVALID_SIZE; }
    length = static_cast<std::size_t>(entry->uncompressed_size);
    bytes = epub_allocate_array<std::uint8_t>(length + 1U);
    if (!bytes) { return ESP_ERR_NO_MEM; }
    auto error = epub_zip_extract_memory(archive_, *entry, bytes.get(), length);
    if (error == ESP_OK) { bytes[length] = 0U; }
    return error;
}

esp_err_t epub_cache_engine::begin_rebuild(const char* path)
{
    auto error = archive_.open(path, generation_);
    if (error != ESP_OK) { return error; }
    metadata_ = {};
    std::strcpy(metadata_.canonical_path, path);
    metadata_.source = archive_.identity();
    metadata_.parser_version = EPUB_PARSER_VERSION;
    metadata_.pagination_version = BOOK_PAGINATION_VERSION;
    metadata_.complete = true;
    epub_unique_array<std::uint8_t> container;
    std::size_t container_length = 0U;
    error = extract_xml("META-INF/container.xml", EPUB_CONTAINER_XML_LIMIT, container, container_length);
    char package_path[EPUB_ARCHIVE_PATH_CAPACITY] = {};
    if (error != ESP_OK || !epub_parse_container(reinterpret_cast<const char*>(container.get()),
                                                   container_length, package_path, sizeof(package_path))) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    container.reset();
    epub_unique_array<std::uint8_t> package_xml;
    std::size_t package_length = 0U;
    error = extract_xml(package_path, EPUB_PACKAGE_XML_LIMIT, package_xml, package_length);
    if (error != ESP_OK || !epub_parse_package(reinterpret_cast<const char*>(package_xml.get()),
                                                package_length, package_path, package_)) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    package_xml.reset();
    if (package_.cover_encoding == book_cover_encoding::none && package_.cover_document_path[0] != '\0') {
        epub_unique_array<std::uint8_t> cover_document;
        std::size_t cover_document_length = 0U;
        if (extract_xml(package_.cover_document_path, EPUB_CONTAINER_XML_LIMIT,
                        cover_document, cover_document_length) == ESP_OK &&
            epub_parse_cover_document(reinterpret_cast<const char*>(cover_document.get()),
                                      cover_document_length, package_.cover_document_path,
                                      package_.cover_path, sizeof(package_.cover_path))) {
            package_.cover_encoding = epub_cover_encoding_from_path(package_.cover_path);
        }
    }
    for (std::uint16_t index = 0U; index < package_.spine_count; ++index) {
        const auto& item = package_.spine[index];
        const auto* entry = archive_.find(item.path);
        if (entry == nullptr || entry->uncompressed_size > EPUB_SPINE_ITEM_SIZE_LIMIT ||
            (entry->method != 0U && entry->method != MZ_DEFLATED)) { return ESP_ERR_NOT_SUPPORTED; }
    }
    if (package_.cover_encoding != book_cover_encoding::none) {
        const auto* cover = archive_.find(package_.cover_path);
        if (cover == nullptr || cover->uncompressed_size == 0U ||
            cover->uncompressed_size > EPUB_COVER_SIZE_LIMIT ||
            (cover->method != 0U && cover->method != MZ_DEFLATED)) {
            package_.cover_encoding = book_cover_encoding::none;
            package_.cover_path[0] = '\0';
        }
    }
    if (!prepare_directory()) { return ESP_FAIL; }
    rebuilt_ = true;
    output_used_ = content_written_ = cover_written_ = 0U;
    spine_index_ = starts_count_ = 0U;
    output_started_ = cover_started_ = false;
    if (package_.cover_encoding != book_cover_encoding::none) {
        error = begin_cover();
        if (error == ESP_OK) { return ESP_OK; }
        stream_.reset();
        package_.cover_encoding = book_cover_encoding::none;
        package_.cover_path[0] = '\0';
        cover_written_ = 0U;
        cover_started_ = false;
    }
    return begin_spine();
}

esp_err_t epub_cache_engine::open(const char* path, const char* book_id, std::uint32_t generation)
{
    cancel();
    if (path == nullptr || book_id == nullptr || std::strlen(book_id) != 64U) { return ESP_ERR_INVALID_ARG; }
    char canonical[BOOK_PATH_CAPACITY] = {};
    if (!book_canonical_path(path, canonical, sizeof(canonical))) { return ESP_ERR_INVALID_ARG; }
    generation_ = generation;
    std::snprintf(directory_, sizeof(directory_), "/.system/books/%s", book_id);
    std::snprintf(metadata_path_, sizeof(metadata_path_), "%s/epub_metadata.json", directory_);
    std::snprintf(content_path_, sizeof(content_path_), "%s/epub_content.txt", directory_);
    std::snprintf(temporary_content_, sizeof(temporary_content_), "%s.tmp", content_path_);
    std::snprintf(map_path_, sizeof(map_path_), "%s/epub_spine.map", directory_);
    std::snprintf(temporary_map_, sizeof(temporary_map_), "%s.tmp", map_path_);
    std::snprintf(cover_path_, sizeof(cover_path_), "%s/epub_cover.bin", directory_);
    std::snprintf(temporary_cover_, sizeof(temporary_cover_), "%s.tmp", cover_path_);
    rebuilt_ = false;
    if (validate_cached(canonical)) { phase_ = phase::ready; return ESP_OK; }
    metadata_ = {};
    const auto error = begin_rebuild(canonical);
    if (error != ESP_OK) { phase_ = phase::failed; archive_.close(); return error; }
    return ESP_OK;
}

bool epub_cache_engine::cover_write(void* context, const std::uint8_t* data, std::size_t length)
{
    auto& self = *static_cast<epub_cache_engine*>(context);
    if (self.cover_written_ > EPUB_COVER_SIZE_LIMIT || length > EPUB_COVER_SIZE_LIMIT - self.cover_written_) {
        self.sink_error_ = ESP_ERR_INVALID_SIZE; return false;
    }
    const auto error = self.io_.write(self.io_.context, self.generation_, self.temporary_cover_,
                                      self.cover_written_, data, length, !self.cover_started_);
    if (error != ESP_OK) { self.sink_error_ = error; return false; }
    self.cover_started_ = true; self.cover_written_ += length; return true;
}

esp_err_t epub_cache_engine::begin_cover()
{
    const auto* entry = archive_.find(package_.cover_path);
    if (entry == nullptr) { return ESP_ERR_NOT_FOUND; }
    sink_error_ = ESP_OK;
    const auto error = stream_.begin(archive_, *entry, {this, cover_write});
    if (error == ESP_OK) { phase_ = phase::cover; }
    return error;
}

bool epub_cache_engine::text_write(void* context, const char* data, std::size_t length)
{
    return static_cast<epub_cache_engine*>(context)->write_text(data, length);
}

bool epub_cache_engine::spine_write(void* context, const std::uint8_t* data, std::size_t length)
{
    auto& self = *static_cast<epub_cache_engine*>(context);
    if (!self.filter_->feed(data, length, false)) { self.sink_error_ = ESP_ERR_INVALID_ARG; return false; }
    return true;
}

bool epub_cache_engine::write_text(const char* data, std::size_t length)
{
    if (content_written_ + output_used_ > EPUB_CONTENT_SIZE_LIMIT ||
        length > EPUB_CONTENT_SIZE_LIMIT - content_written_ - output_used_) {
        sink_error_ = ESP_ERR_INVALID_SIZE; return false;
    }
    while (length != 0U) {
        const auto amount = std::min(length, sizeof(output_) - output_used_);
        std::memcpy(output_ + output_used_, data, amount);
        output_used_ += amount; data += amount; length -= amount;
        if (output_used_ == sizeof(output_) && !flush_text()) { return false; }
    }
    return true;
}

bool epub_cache_engine::flush_text()
{
    if (output_used_ == 0U) { return true; }
    const auto error = io_.write(io_.context, generation_, temporary_content_, content_written_,
                                 output_, output_used_, !output_started_);
    if (error != ESP_OK) { sink_error_ = error; return false; }
    output_started_ = true; content_written_ += output_used_; output_used_ = 0U; return true;
}

esp_err_t epub_cache_engine::begin_spine()
{
    if (spine_index_ >= package_.spine_count) { phase_ = phase::finalize; return ESP_OK; }
    if (!flush_text() || spine_index_ >= EPUB_SPINE_ITEM_LIMIT) { return sink_error_ == ESP_OK ? ESP_ERR_INVALID_SIZE : sink_error_; }
    starts_[spine_index_] = content_written_;
    starts_count_ = spine_index_ + 1U;
    filter_.emplace(epub_text_sink{this, text_write});
    const auto* entry = archive_.find(package_.spine[spine_index_].path);
    if (entry == nullptr) { return ESP_ERR_NOT_FOUND; }
    sink_error_ = ESP_OK;
    const auto error = stream_.begin(archive_, *entry, {this, spine_write});
    if (error == ESP_OK) { phase_ = phase::spine; }
    return error;
}

esp_err_t epub_cache_engine::finish_spine()
{
    if (!filter_->feed(nullptr, 0U, true) || !filter_->finish_chapter() || !flush_text()) {
        return sink_error_ == ESP_OK ? ESP_ERR_INVALID_ARG : sink_error_;
    }
    filter_.reset(); stream_.reset(); ++spine_index_;
    return begin_spine();
}

esp_err_t epub_cache_engine::finalize()
{
    if (!flush_text() || content_written_ == 0U || starts_count_ == 0U) {
        return sink_error_ == ESP_OK ? ESP_ERR_INVALID_SIZE : sink_error_;
    }
    std::size_t map_length = 0U;
    epub_spine_map_encode(starts_, starts_count_, content_written_, map_bytes_, sizeof(map_bytes_), map_length);
    if (map_length == 0U) { return ESP_ERR_INVALID_SIZE; }
    auto error = io_.write(io_.context, generation_, temporary_map_, 0U, map_bytes_, map_length, true);
    if (error == ESP_OK) { error = io_.replace(io_.context, generation_, temporary_content_, content_path_); }
    if (error == ESP_OK) { error = io_.replace(io_.context, generation_, temporary_map_, map_path_); }
    if (error == ESP_OK && package_.cover_encoding != book_cover_encoding::none) {
        error = io_.replace(io_.context, generation_, temporary_cover_, cover_path_);
    }
    if (error != ESP_OK) { return error; }
    metadata_.content_size = content_written_;
    metadata_.cover_size = package_.cover_encoding == book_cover_encoding::none ? 0U : cover_written_;
    metadata_.cover_encoding = package_.cover_encoding;
    metadata_.spine_count = starts_count_;
    metadata_.progress = {0U, 0U, 0U};
    if (!write_metadata()) { return ESP_FAIL; }
    archive_.close(); package_ = {}; phase_ = phase::ready; return ESP_OK;
}

esp_err_t epub_cache_engine::step()
{
    if (!working() || !io_.media_valid(io_.context, generation_)) { return ready() ? ESP_OK : ESP_ERR_INVALID_STATE; }
    esp_err_t error = ESP_OK;
    if (phase_ == phase::cover) {
        error = stream_.step();
        if (error == ESP_FAIL && sink_error_ != ESP_OK) { error = sink_error_; }
        if (error != ESP_OK && io_.media_valid(io_.context, generation_)) {
            stream_.reset();
            package_.cover_encoding = book_cover_encoding::none;
            package_.cover_path[0] = '\0';
            cover_written_ = 0U;
            cover_started_ = false;
            error = begin_spine();
        } else if (error == ESP_OK && stream_.done()) {
            if (cover_written_ == 0U) { error = ESP_ERR_INVALID_SIZE; }
            else { stream_.reset(); error = begin_spine(); }
        }
    } else if (phase_ == phase::spine) {
        error = stream_.step();
        if (error == ESP_FAIL && sink_error_ != ESP_OK) { error = sink_error_; }
        if (error == ESP_OK && stream_.done()) { error = finish_spine(); }
    } else if (phase_ == phase::finalize) {
        error = finalize();
    }
    if (error != ESP_OK) { phase_ = phase::failed; stream_.reset(); archive_.close(); filter_.reset(); }
    return error;
}

esp_err_t epub_cache_engine::save(std::uint64_t linear_offset)
{
    if (!ready() || linear_offset >= metadata_.content_size || starts_count_ != metadata_.spine_count) {
        return ESP_ERR_INVALID_ARG;
    }
    metadata_.progress = epub_position_from_linear(starts_, starts_count_, linear_offset);
    return write_metadata() ? ESP_OK : ESP_FAIL;
}
