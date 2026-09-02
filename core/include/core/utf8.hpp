#pragma once

#include <cstddef>
#include <cstdint>

enum class utf8_decode_result : std::uint8_t { complete, incomplete, invalid };

// Strict Unicode decoding; incomplete input may be continued by the next block.
inline utf8_decode_result utf8_decode(
    const char* data, std::size_t size, std::uint32_t& codepoint, std::size_t& length)
{
    length = 0U;
    if (size == 0U) {
        return utf8_decode_result::incomplete;
    }
    const auto first = static_cast<unsigned char>(data[0]);
    std::uint32_t minimum = 0U;
    if (first < 0x80U) {
        codepoint = first;
        length = 1U;
    } else if (first >= 0xc2U && first <= 0xdfU) {
        codepoint = first & 0x1fU;
        length = 2U;
        minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
        codepoint = first & 0x0fU;
        length = 3U;
        minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
        codepoint = first & 7U;
        length = 4U;
        minimum = 0x10000U;
    } else {
        return utf8_decode_result::invalid;
    }
    for (std::size_t index = 1U; index < length && index < size; ++index) {
        const auto byte = static_cast<unsigned char>(data[index]);
        if ((byte & 0xc0U) != 0x80U) {
            return utf8_decode_result::invalid;
        }
        codepoint = (codepoint << 6U) | (byte & 0x3fU);
    }
    if (size < length) {
        return utf8_decode_result::incomplete;
    }
    return codepoint < minimum || codepoint > 0x10ffffU ||
                   (codepoint >= 0xd800U && codepoint <= 0xdfffU)
               ? utf8_decode_result::invalid : utf8_decode_result::complete;
}

inline std::size_t utf8_encode(std::uint32_t codepoint, char (&text)[5])
{
    std::size_t size = 0U;
    if (codepoint < 0x80U) {
        text[size++] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800U) {
        text[size++] = static_cast<char>(0xc0U | (codepoint >> 6U));
        text[size++] = static_cast<char>(0x80U | (codepoint & 0x3fU));
    } else if (codepoint < 0x10000U) {
        text[size++] = static_cast<char>(0xe0U | (codepoint >> 12U));
        text[size++] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU));
        text[size++] = static_cast<char>(0x80U | (codepoint & 0x3fU));
    } else {
        text[size++] = static_cast<char>(0xf0U | (codepoint >> 18U));
        text[size++] = static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU));
        text[size++] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU));
        text[size++] = static_cast<char>(0x80U | (codepoint & 0x3fU));
    }
    text[size] = '\0';
    return size;
}
