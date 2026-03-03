#pragma once

#include <cstddef>
#include <cstdint>

namespace NativeRng {

// Read random bytes from /dev/urandom. Returns false on error.
bool getRandomBytes(uint8_t *buf, size_t len);

} // namespace NativeRng
