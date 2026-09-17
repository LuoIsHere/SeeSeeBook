# Changelog

## v0.2 — 2026-09-15

- Replaced the startup menu with a descriptor-driven `Books` / `File` / `Menu` launcher. Reader remains an internal target opened by Books or File; device utilities are listed in the secondary Menu.
- Added the BooksApp shelf with a 3 × 2 grid, transactional TXT/EPUB scan settings, status-bar pagination, UTF-8-safe two-line names, TXT previews, EPUB cover placeholders and cached covers, and return-state preservation around Reader.
- Added the bounded `BookCatalogService`. It scans the SD card outside hidden directories, sorts and deduplicates supported paths, isolates per-book EPUB failures, and atomically stores the catalog and per-card settings in `/.system/books/catalog_v1.bin`.
- Added UTF-8 TXT reading with demand paging, a background SD page index, previous-page history, saved resume positions, strict stale-result checks, and safe handling of media removal or replacement.
- Added unencrypted reflowable EPUB2/EPUB3 support for ZIP32 Stored/Deflate packages, streaming OPF/spine/XHTML processing, SD-derived body and cover caches, shared page indexing, and logical cover page 0.
- Moved TXT and EPUB progress, indexes, and derived EPUB data to per-path SHA-256 directories under `/.system/books/`. Incompatible pagination or parser data is discarded and rebuilt without using old page or byte offsets.
- Unified PaperMono drawing on a 96,000-byte packed 2bpp framebuffer. Pure black-and-white frames use the monochrome OTP path; frames containing intermediate gray use the SSD1677 `0xD7` four-level waveform.
- Added reusable Gray4 image quantization and a Gray4 Test App. Reader covers use real four-level output; Books covers use deterministic 1-bit Bayer projection so shelf interaction remains on the monochrome refresh path.
- Reduced unnecessary quality refreshes, synchronized content and status-bar updates, added pressed-state inversion to interactive controls, suspended the front light during full refreshes, and preserved complete RAM1 synchronization for PaperMono `0xFF` fast updates.
- Added ESP32-S3 QEMU integration coverage for navigation, catalog persistence, TXT/EPUB parsing and reading, progress, SD lifecycle, UI ownership, Gray4 packing and conversion, and refresh policy. The release baseline prints `ALL_READER_TESTS_PASSED`; the ESP-IDF 5.5.5 firmware build passes partition-size validation.

## v0.1

- Added a Mooncake-based application framework and menu navigation.
- Added RTC date/time settings, battery information, and SD card directory and file-list browsing.
- Added touch input, front-light controls, a shared status bar, and PaperMono e-paper display support.
- Organized application logic, system services, event dispatch, UI rendering, and hardware abstraction into separate layers.

Detailed change records are maintained in the internal `inDocs` documents, which are not tracked in this repository.
