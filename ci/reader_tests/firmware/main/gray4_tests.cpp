#include <array>
#include <cstdio>
#include <cstdlib>

#include "gray4_framebuffer.hpp"
#include "gray4_otp_encoding.hpp"
#include "paper_mono_config.hpp"

namespace {

unsigned checks = 0U;

#define VERIFY_GRAY4(condition) do { \
    ++checks; \
    if (!(condition)) { \
        std::printf("TEST_FAILURE gray4 line=%d: %s\n", __LINE__, #condition); \
        std::fflush(stdout); \
        std::abort(); \
    } \
} while (0)

void test_size_and_packing()
{
    VERIFY_GRAY4(
        gray4_framebuffer_view::required_size(800U, 480U) == 96000U);
    VERIFY_GRAY4(PAPER_MONO_GRAY4_FRAME_SIZE == 96000U);

    std::array<std::uint8_t, 4U> storage = {};
    gray4_framebuffer_view frame(storage.data(), storage.size(), 8U, 2U);
    VERIFY_GRAY4(frame.valid());
    const display_gray4 first_row[] = {
        display_gray4::black, display_gray4::dark_gray,
        display_gray4::light_gray, display_gray4::white,
        display_gray4::white, display_gray4::light_gray,
        display_gray4::dark_gray, display_gray4::black,
    };
    for (std::uint16_t x = 0U; x < 8U; ++x) {
        VERIFY_GRAY4(frame.set_pixel(x, 0U, first_row[x]));
    }
    VERIFY_GRAY4(storage[0] == 0x1bU);
    VERIFY_GRAY4(storage[1] == 0xe4U);
    for (std::uint16_t x = 0U; x < 8U; ++x) {
        display_gray4 value = display_gray4::black;
        VERIFY_GRAY4(frame.get_pixel(x, 0U, value));
        VERIFY_GRAY4(value == first_row[x]);
    }
}

void test_fill_detection_and_boundaries()
{
    std::array<std::uint8_t, 4U> storage = {};
    gray4_framebuffer_view frame(storage.data(), storage.size(), 8U, 2U);
    VERIFY_GRAY4(frame.fill(display_gray4::black));
    VERIFY_GRAY4(storage[0] == 0x00U && !frame.has_intermediate_gray());
    VERIFY_GRAY4(frame.fill(display_gray4::white));
    VERIFY_GRAY4(storage[0] == 0xffU && !frame.has_intermediate_gray());
    VERIFY_GRAY4(frame.fill(display_gray4::dark_gray));
    VERIFY_GRAY4(storage[0] == 0x55U && frame.has_intermediate_gray());
    VERIFY_GRAY4(frame.fill(display_gray4::light_gray));
    VERIFY_GRAY4(storage[0] == 0xaaU && frame.has_intermediate_gray());

    VERIFY_GRAY4(frame.fill(display_gray4::white));
    const auto before = storage;
    VERIFY_GRAY4(!frame.set_pixel(8U, 0U, display_gray4::black));
    VERIFY_GRAY4(!frame.set_pixel(0U, 2U, display_gray4::black));
    VERIFY_GRAY4(!frame.fill_rect(7U, 1U, 2U, 1U, display_gray4::black));
    VERIFY_GRAY4(!frame.fill_rect(0U, 0U, 0U, 1U, display_gray4::black));
    VERIFY_GRAY4(storage == before);
    VERIFY_GRAY4(frame.fill_rect(1U, 0U, 3U, 2U, display_gray4::dark_gray));
    VERIFY_GRAY4(frame.has_intermediate_gray());
    display_gray4 value = display_gray4::white;
    VERIFY_GRAY4(frame.get_pixel(3U, 1U, value) &&
                 value == display_gray4::dark_gray);
    VERIFY_GRAY4(frame.get_pixel(4U, 1U, value) &&
                 value == display_gray4::white);

    gray4_framebuffer_view invalid(nullptr, 0U, 8U, 2U);
    VERIFY_GRAY4(!invalid.valid());
    VERIFY_GRAY4(!invalid.fill(display_gray4::white));
}

void test_quantization_and_luminance()
{
    VERIFY_GRAY4(gray4_quantize(0U) == display_gray4::black);
    VERIFY_GRAY4(gray4_quantize(63U) == display_gray4::black);
    VERIFY_GRAY4(gray4_quantize(64U) == display_gray4::dark_gray);
    VERIFY_GRAY4(gray4_quantize(127U) == display_gray4::dark_gray);
    VERIFY_GRAY4(gray4_quantize(128U) == display_gray4::light_gray);
    VERIFY_GRAY4(gray4_quantize(191U) == display_gray4::light_gray);
    VERIFY_GRAY4(gray4_quantize(192U) == display_gray4::white);
    VERIFY_GRAY4(gray4_quantize(255U) == display_gray4::white);
    VERIFY_GRAY4(gray4_luminance(0U, 0U, 0U) == 0U);
    VERIFY_GRAY4(gray4_luminance(255U, 255U, 255U) == 255U);
    VERIFY_GRAY4(gray4_luminance(255U, 0U, 0U) == 76U);
    VERIFY_GRAY4(gray4_luminance(0U, 255U, 0U) == 150U);
    VERIFY_GRAY4(gray4_luminance(0U, 0U, 255U) == 28U);
}

void test_mono_and_otp_encoding()
{
    std::array<std::uint8_t, 2U> storage = {};
    gray4_framebuffer_view frame(storage.data(), storage.size(), 8U, 1U);
    const display_gray4 values[] = {
        display_gray4::black, display_gray4::dark_gray,
        display_gray4::light_gray, display_gray4::white,
        display_gray4::black, display_gray4::dark_gray,
        display_gray4::light_gray, display_gray4::white,
    };
    for (std::uint16_t x = 0U; x < 8U; ++x) {
        VERIFY_GRAY4(frame.set_pixel(x, 0U, values[x]));
    }

    // The retained mono encoder maps only the two endpoint levels to the
    // existing SSD1677 1-bit transfer contract (white bit is 1).
    VERIFY_GRAY4(paper_mono::gray4_mono_byte(frame, 0U, 0U) == 0x33U);

    // Gray4 streams X in descending order. These bytes encode the official
    // RAM1/RAM2 pairs: white=00, light=10, dark=01, black=11.
    VERIFY_GRAY4(
        paper_mono::gray4_otp_plane_byte(
            frame, 0U, 0U, paper_mono::gray4_otp_plane::ram_1) == 0x55U);
    VERIFY_GRAY4(
        paper_mono::gray4_otp_plane_byte(
            frame, 0U, 0U, paper_mono::gray4_otp_plane::ram_2) == 0x33U);
    VERIFY_GRAY4(paper_mono::gray4_mono_byte(frame, 1U, 0U) == 0xffU);
}

}  // namespace

void test_gray4_support()
{
    test_size_and_packing();
    test_fill_detection_and_boundaries();
    test_quantization_and_luminance();
    test_mono_and_otp_encoding();
    std::printf("GRAY4_TESTS_PASS checks=%u\n", checks);
}
