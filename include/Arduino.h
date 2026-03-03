// Minimal Arduino.h stub for building rweather/Crypto on native platform.
// Only RNG.cpp needs this; we don't use the Arduino RNG class.
//
// When a real Arduino framework is present, #include_next forwards to the
// real header. On native (no framework), the stubs below satisfy the linker.
#pragma once

#if __has_include_next(<Arduino.h>)
#include_next <Arduino.h>
#else

#include <cstdint>
#include <cstring>
#include <cstdlib>

typedef uint8_t byte;

inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
inline void randomSeed(unsigned long) {}
inline long random(long max) { return 0; (void)max; }

// EEPROM stubs
#define EEPROM_END 0

#endif
