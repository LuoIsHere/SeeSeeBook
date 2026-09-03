#pragma once

#include <cstdint>

// Service-only gate: serializes one bounded background SD operation against
// card generation changes/mounting, yielding to foreground storage requests.
bool storage_book_access_begin(std::uint32_t media_generation);
void storage_book_access_end();
