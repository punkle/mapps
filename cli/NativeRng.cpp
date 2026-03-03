#include "NativeRng.h"
#include <cstdio>

namespace NativeRng {

bool getRandomBytes(uint8_t *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f)
        return false;
    size_t read = fread(buf, 1, len, f);
    fclose(f);
    return read == len;
}

} // namespace NativeRng
