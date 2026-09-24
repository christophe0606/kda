#ifndef FIR_VECTORS_H
#define FIR_VECTORS_H
#include <stddef.h>
#include <stdint.h>

static const uint32_t blocks[] = {
    1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 63, 64, 127, 128, 129, 256, 512
};
static const uint16_t taps[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 64, 128
};
static inline uint32_t random_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}
static inline float input_sample(unsigned pattern, size_t i, uint32_t *seed)
{
    switch (pattern) {
    case 0: return i == 0 ? 1.0f : 0.0f;
    case 1: return 0.0f;
    case 2: return 0.25f;
    case 3: return (float)((int)(i % 67U) - 33) / 64.0f;
    case 4: return (i & 1U) ? -0.5f : 0.5f;
    case 5: return (float)((int)(random_next(seed) >> 8) % 2049 - 1024) / 1009.0f;
    default: {
        const float magnitude[] = {0.0625f, 1.0f, 16.0f};
        return magnitude[i % 3U] * ((i & 1U) ? -0.75f : 0.5f);
    }
    }
}
#endif
