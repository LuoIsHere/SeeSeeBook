#include "book_index_engine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

bool book_index_engine::matches(const char* path, std::uint32_t generation) const
{
    return phase_ != phase::idle && generation == generation_ && std::strcmp(path, metadata_.canonical_path) == 0;
}

bool book_index_engine::working() const
{
    return phase_ == phase::scanning || phase_ == phase::validating_cache || phase_ == phase::validating_new;
}

void book_index_engine::emit(book_event_type type, const book_progress& progress,
                            std::uint32_t request, esp_err_t error)
{
    book_service_event event = {};
    event.type = type;
    event.session_id = session_;
    event.request_id = request;
    event.media_generation = generation_;
    event.progress = progress;
    event.page_count = phase_ == phase::ready ? header_.page_count : 0U;
    event.file_size = metadata_.file.file_size;
    event.modified_time = metadata_.file.modified_time;
    event.error = error;
    event.index_valid = phase_ == phase::ready;
    event.persistent = persistent_;
    io_.emit(io_.context, event);
}

void book_index_engine::fail(esp_err_t error)
{
    phase_ = phase::failed;
    persistent_ = false;
    emit(book_event_type::error, position_, 0U, error);
}

bool book_index_engine::text_read(std::uint64_t offset, void* data, std::size_t capacity, std::size_t& length)
{
    std::uint64_t size = 0U;
    std::int64_t mtime = 0;
    const auto error = io_.read(io_.context, generation_, metadata_.canonical_path, offset,
                                data, capacity, length, size, mtime);
    if (error != ESP_OK || size != metadata_.file.file_size || mtime != metadata_.file.modified_time ||
        offset > size || length > size - offset || (length == 0U && offset < size)) {
        fail(error == ESP_OK ? ESP_ERR_INVALID_STATE : error);
        return false;
    }
    return true;
}

bool book_index_engine::fingerprint(book_fingerprint& output)
{
    std::uint32_t values[3] = {};
    for (unsigned sample = 0U; sample < 3U; ++sample) {
        const auto window = book_fingerprint_window(metadata_.file.file_size, sample);
        std::size_t length = 0U;
        // Empty files still perform a metadata-checked read with a nonzero capacity.
        if (!text_read(window.offset, buffer_, std::max<std::size_t>(1U, window.length), length)) { return false; }
        if (length != window.length) { fail(ESP_ERR_INVALID_SIZE); return false; }
        values[sample] = book_crc32(buffer_, length);
    }
    output = {values[0], values[1], values[2]};
    return true;
}

bool book_index_engine::prepare_directory()
{
    for (const char* path : {"/.system", "/.system/books", static_cast<const char*>(directory_)}) {
        const auto error = io_.mkdir(io_.context, generation_, path);
        if (error != ESP_OK) { fail(error); return false; }
    }
    return true;
}

bool book_index_engine::write_metadata()
{
    book_metadata saved = metadata_;
    saved.progress = position_;
    saved.index_complete = phase_ == phase::ready;
    saved.index_format_version = BOOK_PAGE_INDEX_FORMAT_VERSION;
    saved.pagination_version = BOOK_PAGINATION_VERSION;
    saved.page_count = saved.index_complete ? header_.page_count : 0U;
    saved.offsets_crc32 = saved.index_complete ? header_.offsets_crc32 : 0U;
    if (!saved.index_complete) { saved.progress.page = 0U; }
    if (!book_metadata_encode(saved, json_, sizeof(json_))) { fail(ESP_ERR_NO_MEM); return false; }
    char temporary[136] = {};
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", metadata_path_);
    auto error = io_.write(io_.context, generation_, temporary, 0U, json_, std::strlen(json_), true);
    if (error == ESP_OK) { error = io_.replace(io_.context, generation_, temporary, metadata_path_); }
    if (error != ESP_OK) { fail(error); return false; }
    persistent_ = true;
    return true;
}

void book_index_engine::open(const char* path, const char* book_id, const text_layout_profile& layout,
                             std::uint32_t session, std::uint32_t generation)
{
    if (matches(path, generation) && working()) {
        session_ = session;
        std::uint8_t probe = 0U;
        std::size_t length = 0U;
        if (!text_read(0U, &probe, 1U, length)) { return; }
        resume_ = position_;
        emit(book_event_type::opened, resume_);
        if (phase_ == phase::ready) { emit(book_event_type::ready, resume_); }
        return;
    }
    // One builder. Switching books abandons only .tmp; a closed Reader alone
    // never cancels this book-level cache build.
    phase_ = phase::idle;
    metadata_ = {};
    header_ = {};
    resume_ = position_ = {};
    buffered_offsets_ = buffer_length_ = 0U;
    written_offsets_ = buffer_offset_ = previous_offset_ = 0U;
    checked_offsets_ = checked_crc_ = 0U;
    metadata_dirty_ = persistent_ = resume_by_page_ = false;
    session_ = session;
    generation_ = generation;
    layout_ = layout;
    if (!book_canonical_path(path, metadata_.canonical_path, sizeof(metadata_.canonical_path))) {
        fail(ESP_ERR_INVALID_ARG); return;
    }
    std::snprintf(directory_, sizeof(directory_), "/.system/books/%s", book_id);
    std::snprintf(metadata_path_, sizeof(metadata_path_), "%s/metadata.json", directory_);
    std::snprintf(index_path_, sizeof(index_path_), "%s/pages.idx", directory_);
    std::snprintf(temporary_index_, sizeof(temporary_index_), "%s.tmp", index_path_);
    std::size_t length = 0U;
    auto error = io_.read(io_.context, generation_, path, 0U, buffer_, 1U, length,
                          metadata_.file.file_size, metadata_.file.modified_time);
    if (error != ESP_OK || metadata_.file.file_size > BOOK_JSON_INTEGER_MAX ||
        metadata_.file.modified_time < 0 ||
        static_cast<std::uint64_t>(metadata_.file.modified_time) > BOOK_JSON_INTEGER_MAX) {
        fail(error == ESP_OK ? ESP_ERR_INVALID_SIZE : error); return;
    }
    book_metadata old = {};
    std::uint64_t size = 0U;
    std::int64_t mtime = 0;
    error = io_.read(io_.context, generation_, metadata_path_, 0U, json_, sizeof(json_), length, size, mtime);
    const bool have_metadata = error == ESP_OK && length == size &&
                              book_metadata_decode(json_, length, old);
    if (have_metadata && std::strcmp(old.canonical_path, metadata_.canonical_path) != 0) {
        // A hash directory alone never authorizes another canonical path.
        fail(ESP_ERR_INVALID_STATE); return;
    }
    bool candidate = have_metadata &&
        book_cache_check(old, metadata_.canonical_path, metadata_.file.file_size, metadata_.file.modified_time) !=
            book_cache_decision::rebuild;
    if (candidate) {
        error = io_.read(io_.context, generation_, index_path_, 0U, buffer_, BOOK_INDEX_HEADER_SIZE, length, size, mtime);
        candidate = error == ESP_OK && book_index_decode(buffer_, length, size, metadata_.file.file_size, header_) &&
                    header_.page_count == old.page_count && header_.offsets_crc32 == old.offsets_crc32;
    }
    const bool same_stat = have_metadata && old.file.file_size == metadata_.file.file_size &&
                           old.file.modified_time == metadata_.file.modified_time;
    if (candidate && same_stat) {
        metadata_.file.fingerprint = old.file.fingerprint; // No TXT fingerprint I/O on this path.
    } else if (!fingerprint(metadata_.file.fingerprint)) { return; }
    const bool same_content = have_metadata && old.file.file_size == metadata_.file.file_size &&
        (same_stat || book_fingerprint_equal(old.file.fingerprint, metadata_.file.fingerprint));
    candidate = candidate && same_content;
    // A pagination version change discards the old progress as well as the index.
    if (same_content && old.pagination_version == BOOK_PAGINATION_VERSION) {
        resume_ = old.progress;
    }
    position_ = resume_;
    metadata_.index_format_version = BOOK_PAGE_INDEX_FORMAT_VERSION;
    metadata_.pagination_version = BOOK_PAGINATION_VERSION;
    if (!prepare_directory()) { return; }
    if (candidate) {
        phase_ = phase::validating_cache;
        resume_by_page_ = true;
        metadata_dirty_ = !same_stat;
    } else {
        if (!begin_rebuild(false)) { return; }
    }
    emit(book_event_type::opened, resume_);
}

bool book_index_engine::begin_rebuild(bool calculate_fingerprint)
{
    if (calculate_fingerprint && !fingerprint(metadata_.file.fingerprint)) { return false; }
    header_ = {1U, metadata_.file.file_size, 0U};
    resume_by_page_ = false;
    resume_.page = position_.page = 0U;
    checked_offsets_ = checked_crc_ = 0U;
    previous_offset_ = written_offsets_ = buffer_offset_ = 0U;
    buffer_length_ = buffered_offsets_ = 0U;
    std::memset(buffer_, 0, BOOK_INDEX_HEADER_SIZE);
    const auto error = io_.write(io_.context, generation_, temporary_index_, 0U, buffer_, BOOK_INDEX_HEADER_SIZE, true);
    if (error != ESP_OK) { fail(error); return false; }
    phase_ = phase::scanning;
    if (!append_offset(0U)) { return false; }
    paginator_.reset(0U, layout_);
    return true;
}

bool book_index_engine::flush_offsets()
{
    if (buffered_offsets_ == 0U) { return true; }
    const auto error = io_.write(io_.context, generation_, temporary_index_,
        BOOK_INDEX_HEADER_SIZE + written_offsets_, offsets_, buffered_offsets_, false);
    if (error != ESP_OK) { fail(error); return false; }
    written_offsets_ += buffered_offsets_;
    buffered_offsets_ = 0U;
    return true;
}

bool book_index_engine::append_offset(std::uint64_t offset)
{
    if (buffered_offsets_ == sizeof(offsets_) && !flush_offsets()) { return false; }
    book_encode_u64(offsets_ + buffered_offsets_, offset);
    header_.offsets_crc32 = book_crc32(offsets_ + buffered_offsets_, BOOK_INDEX_OFFSET_WIDTH, header_.offsets_crc32);
    buffered_offsets_ += BOOK_INDEX_OFFSET_WIDTH;
    return true;
}

void book_index_engine::scan_step()
{
    const auto offset = paginator_.read_offset();
    if (buffer_length_ == 0U || offset < buffer_offset_ || offset >= buffer_offset_ + buffer_length_) {
        buffer_offset_ = offset;
        if (!text_read(offset, buffer_, sizeof(buffer_), buffer_length_)) { return; }
    }
    const auto start = static_cast<std::size_t>(offset - buffer_offset_);
    const auto parsed = paginator_.feed(reinterpret_cast<const char*>(buffer_ + start), buffer_length_ - start,
                                       buffer_offset_ + buffer_length_ == metadata_.file.file_size);
    if (parsed == reader_parse_status::invalid_utf8) { fail(ESP_ERR_INVALID_ARG); return; }
    if (parsed != reader_parse_status::page_ready) { return; }
    const auto& page = paginator_.page();
    if (!page.end_of_file) {
        if (header_.page_count == UINT32_MAX || page.next_page_start_offset <= page.current_page_start_offset ||
            page.next_page_start_offset >= metadata_.file.file_size) { fail(ESP_ERR_INVALID_SIZE); return; }
        if (!append_offset(page.next_page_start_offset)) { return; }
        ++header_.page_count;
        paginator_.reset(page.next_page_start_offset, layout_);
        return;
    }
    if (!flush_offsets()) { return; }
    std::uint8_t bytes[BOOK_INDEX_HEADER_SIZE] = {};
    book_index_encode(header_, bytes);
    const auto error = io_.write(io_.context, generation_, temporary_index_, 0U, bytes, sizeof(bytes), false);
    if (error != ESP_OK) { fail(error); return; }
    phase_ = phase::validating_new;
}

void book_index_engine::validate_step()
{
    std::size_t length = 0U;
    std::uint64_t size = 0U;
    std::int64_t mtime = 0;
    const char* path = phase_ == phase::validating_new ? temporary_index_ : index_path_;
    const auto expected_size = BOOK_INDEX_HEADER_SIZE + std::uint64_t(header_.page_count) * BOOK_INDEX_OFFSET_WIDTH;
    if (checked_offsets_ == 0U) {
        book_index_header actual = {};
        const auto error = io_.read(io_.context, generation_, path, 0U, buffer_, BOOK_INDEX_HEADER_SIZE, length, size, mtime);
        if (error != ESP_OK || !book_index_decode(buffer_, length, size, metadata_.file.file_size, actual) ||
            actual.page_count != header_.page_count || actual.offsets_crc32 != header_.offsets_crc32) {
            if (phase_ == phase::validating_cache && io_.media_valid(io_.context, generation_)) { begin_rebuild(true); }
            else { fail(error == ESP_OK ? ESP_ERR_INVALID_SIZE : error); }
            return;
        }
    }
    const auto offset = BOOK_INDEX_HEADER_SIZE + std::uint64_t(checked_offsets_) * BOOK_INDEX_OFFSET_WIDTH;
    const auto capacity = static_cast<std::size_t>(std::min<std::uint64_t>(sizeof(buffer_), expected_size - offset));
    const auto error = io_.read(io_.context, generation_, path, offset, buffer_, capacity, length, size, mtime);
    bool valid = error == ESP_OK && size == expected_size && length == capacity && length % BOOK_INDEX_OFFSET_WIDTH == 0U;
    if (valid) {
        checked_crc_ = book_crc32(buffer_, length, checked_crc_);
        for (std::size_t i = 0U; i < length; i += BOOK_INDEX_OFFSET_WIDTH) {
            const auto value = book_decode_u64(buffer_ + i);
            if (!book_index_offset_valid(value, previous_offset_, checked_offsets_, metadata_.file.file_size)) {
                valid = false; break;
            }
            previous_offset_ = value;
            ++checked_offsets_;
        }
    }
    if (!valid || (checked_offsets_ == header_.page_count && checked_crc_ != header_.offsets_crc32)) {
        if (phase_ == phase::validating_cache && io_.media_valid(io_.context, generation_)) {
            // Rebuild without trusting damaged offsets. Preserve only a bounded byte fallback.
            begin_rebuild(true);
        } else { fail(error == ESP_OK ? ESP_ERR_INVALID_SIZE : error); }
        return;
    }
    if (checked_offsets_ == header_.page_count) { complete_index(phase_ == phase::validating_new); }
}

bool book_index_engine::read_offset(std::uint32_t page, std::uint64_t& offset)
{
    if (page >= header_.page_count) { return false; }
    std::uint8_t bytes[BOOK_INDEX_OFFSET_WIDTH] = {};
    std::size_t length = 0U;
    std::uint64_t size = 0U;
    std::int64_t mtime = 0;
    const auto error = io_.read(io_.context, generation_, index_path_,
        BOOK_INDEX_HEADER_SIZE + std::uint64_t(page) * BOOK_INDEX_OFFSET_WIDTH,
        bytes, sizeof(bytes), length, size, mtime);
    if (error != ESP_OK || length != sizeof(bytes) ||
        size != BOOK_INDEX_HEADER_SIZE + std::uint64_t(header_.page_count) * BOOK_INDEX_OFFSET_WIDTH) { return false; }
    offset = book_decode_u64(bytes);
    return page == 0U ? offset == 0U : offset > 0U && offset < metadata_.file.file_size;
}

bool book_index_engine::locate(book_progress& progress, bool prefer_page)
{
    if (prefer_page && progress.page < header_.page_count) { return read_offset(progress.page, progress.byte_offset); }
    // Greatest page start <= offset: the page containing the saved position.
    const auto target = metadata_.file.file_size == 0U ? 0U :
        std::min(progress.byte_offset, metadata_.file.file_size - 1U);
    std::uint32_t low = 0U, high = header_.page_count;
    while (low < high) {
        const auto middle = low + (high - low) / 2U;
        std::uint64_t offset = 0U;
        if (!read_offset(middle, offset)) { return false; }
        if (offset <= target) { low = middle + 1U; } else { high = middle; }
    }
    progress.page = low == 0U ? 0U : low - 1U;
    return read_offset(progress.page, progress.byte_offset);
}

void book_index_engine::complete_index(bool newly_built)
{
    std::size_t length = 0U;
    std::uint8_t probe = 0U;
    if (!text_read(0U, &probe, 1U, length)) { return; }
    if (newly_built) {
        const auto error = io_.replace(io_.context, generation_, temporary_index_, index_path_);
        if (error != ESP_OK) { fail(error); return; }
    }
    phase_ = phase::ready;
    if (!locate(resume_, resume_by_page_) || !locate(position_, false)) { fail(ESP_ERR_INVALID_SIZE); return; }
    if ((newly_built || metadata_dirty_) && !write_metadata()) { return; }
    persistent_ = true;
    metadata_dirty_ = false;
    emit(book_event_type::ready, resume_);
}

void book_index_engine::query(std::uint32_t session, std::uint32_t request, bool by_page,
                              std::uint32_t page, std::uint64_t offset)
{
    if (session != session_) { return; }
    book_progress progress = {page, offset};
    if (phase_ == phase::ready && !locate(progress, by_page)) { fail(ESP_ERR_INVALID_SIZE); return; }
    if (progress.byte_offset != 0U && progress.byte_offset >= metadata_.file.file_size) {
        emit(book_event_type::position, position_, request, ESP_ERR_INVALID_ARG); return;
    }
    position_ = progress;
    emit(book_event_type::position, position_, request);
}

void book_index_engine::save(std::uint32_t session, std::uint64_t offset)
{
    if (session != session_ || phase_ == phase::idle || phase_ == phase::failed) { return; }
    if (offset != 0U && offset >= metadata_.file.file_size) { return; }
    position_ = {0U, offset};
    if (phase_ == phase::ready && !locate(position_, false)) { fail(ESP_ERR_INVALID_SIZE); return; }
    metadata_dirty_ = true;
    if (write_metadata()) { emit(book_event_type::saved, position_); }
}

void book_index_engine::step()
{
    if (!working()) { return; }
    if (!io_.media_valid(io_.context, generation_)) { fail(ESP_ERR_INVALID_STATE); return; }
    if (phase_ == phase::scanning) { scan_step(); } else { validate_step(); }
}
