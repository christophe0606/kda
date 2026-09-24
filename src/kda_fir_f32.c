#include "kda_fir_f32.h"

#include <string.h>

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#define KDA_FIR_MVE 1
#else
#define KDA_FIR_MVE 0
#endif
#if defined(_MSC_VER)
#define KDA_NOINLINE __declspec(noinline)
#else
#define KDA_NOINLINE __attribute__((noinline))
#endif
#if defined(KDA_FIR_REQUIRE_MVE) && !KDA_FIR_MVE
#error "The target FIR candidate requires floating-point MVE"
#endif

_Static_assert(sizeof(float32_t) == 4, "FIR requires a 32-bit float");

size_t kda_fir_history_f32_count(uint16_t num_taps)
{
    const size_t count = num_taps;
    return count != 0 && count <= SIZE_MAX / sizeof(float32_t) - (KDA_FIR_CHUNK - 1U)
        ? count + KDA_FIR_CHUNK - 1U : 0;
}

size_t kda_fir_coeff_f32_count(uint16_t num_taps)
{
    return kda_fir_history_f32_count(num_taps) != 0 ? (size_t)num_taps : 0;
}

int kda_fir_init_f32(kda_fir_instance_f32 *S, kda_fir_state_f32 *state,
                     uint16_t num_taps,
                     const float32_t *coefficients, size_t coefficient_count,
                     float32_t *prepared, size_t prepared_count,
                     float32_t *history, size_t history_count)
{
    const size_t needed_history = kda_fir_history_f32_count(num_taps);
    const size_t count = num_taps;
    if (S == NULL || state == NULL || coefficients == NULL || prepared == NULL ||
        history == NULL || needed_history == 0 || coefficient_count < count ||
        prepared_count < count || history_count < needed_history) {
        return 0;
    }

    for (size_t k = 0; k < count; ++k) {
        prepared[k] = coefficients[k];
    }
    memset(history, 0, needed_history * sizeof(*history));
    state->history = history;
    state->next = 0;
    S->num_taps = num_taps;
    S->prepared = prepared;
    S->state = state;
    return 1;
}

void kda_fir_reset_f32(const kda_fir_instance_f32 *S)
{
    memset(S->state->history, 0,
           kda_fir_history_f32_count(S->num_taps) * sizeof(float32_t));
    S->state->next = 0;
}

KDA_NOINLINE static void fir_scale(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    const float32_t b0 = S->prepared[0];
    for (uint32_t i = 0; i < blockSize; ++i) { pDst[i] = b0 * pSrc[i]; }
}

KDA_NOINLINE static void fir_tiny(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    const size_t count = S->num_taps;
    float32_t *const history = S->state->history;
    const float32_t *const coefficients = S->prepared;
    float32_t h0 = history[0], h1 = history[1], h2 = history[2];
    const float32_t b0 = coefficients[count-1U], b1 = coefficients[count-2U];
#if KDA_FIR_MVE
    uint32_t c0, c1, c2;
    memcpy(&c0,&h0,4); memcpy(&c1,&h1,4); memcpy(&c2,&h2,4);
    const float32_t b2 = count >= 3U ? coefficients[count-3U] : 0;
    const float32_t b3 = count == 4U ? coefficients[0] : 0;
    while (blockSize >= 4U) {
        const float32x4_t current = vldrwq_f32(pSrc);
        float32x4_t sum = vmulq_n_f32(current,b0);
        uint32x4_t delayed = vshlcq_u32(vreinterpretq_u32_f32(current),&c0,32);
        sum = vfmaq_n_f32(sum,vreinterpretq_f32_u32(delayed),b1);
        if (count >= 3U) {
            delayed = vshlcq_u32(delayed,&c1,32);
            sum = vfmaq_n_f32(sum,vreinterpretq_f32_u32(delayed),b2);
        }
        if (count == 4U) {
            delayed = vshlcq_u32(delayed,&c2,32);
            sum = vfmaq_n_f32(sum,vreinterpretq_f32_u32(delayed),b3);
        }
        vstrwq_f32(pDst,sum);
        pSrc += 4; pDst += 4; blockSize -= 4;
    }
    memcpy(&h0,&c0,4); memcpy(&h1,&c1,4); memcpy(&h2,&c2,4);
#endif
    if (count == 2U) {
        for (uint32_t i = 0; i < blockSize; ++i) {
            const float32_t x = pSrc[i];
            pDst[i] = b0*x + b1*h0; h0 = x;
        }
    } else if (count == 3U) {
        const float32_t b2 = coefficients[0];
        for (uint32_t i = 0; i < blockSize; ++i) {
            const float32_t x = pSrc[i];
            pDst[i] = b0*x + b1*h0 + b2*h1; h1 = h0; h0 = x;
        }
    } else {
        const float32_t b2 = coefficients[1], b3 = coefficients[0];
        for (uint32_t i = 0; i < blockSize; ++i) {
            const float32_t x = pSrc[i];
            pDst[i] = b0*x + b1*h0 + b2*h1 + b3*h2;
            h2 = h1; h1 = h0; h0 = x;
        }
    }
    history[0] = h0; history[1] = h1; history[2] = h2;
}

#if KDA_FIR_MVE
static inline void fir_tile8(const float32_t *samples,
    const float32_t *coefficients, float32_t *output, uint32_t taps)
{
    /* Interleave loads and arithmetic to overlap their 64-bit beats. */
    __asm volatile(
        "vmov.i32 q0, #0\n"
        "vmov.i32 q1, #0\n"
        "dls lr, %[taps]\n"
        ".p2align 2\n"
        "1:\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q2, [%[samples]], #4\n"
        "vfma.f32 q0, q2, r12\n"
        "vldrw.u32 q2, [%[samples], #12]\n"
        "vfma.f32 q1, q2, r12\n"
        "le lr, 1b\n"
        "vstrw.32 q0, [%[output]]\n"
        "vstrw.32 q1, [%[output], #16]\n"
        : [samples] "+&r" (samples), [coefficients] "+&r" (coefficients)
        : [output] "r" (output), [taps] "r" (taps)
        : "q0", "q1", "q2", "r12", "lr", "cc", "memory");
}

static inline void fir_tile16(const float32_t *samples,
    const float32_t *coefficients, float32_t *output, uint32_t taps)
{
    __asm volatile(
        "vmov.i32 q0, #0\n"
        "vmov.i32 q1, #0\n"
        "vmov.i32 q2, #0\n"
        "vmov.i32 q3, #0\n"
        "dls lr, %[taps]\n"
        ".p2align 2\n"
        "1:\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q4, [%[samples]], #4\n"
        "vfma.f32 q0, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #12]\n"
        "vfma.f32 q1, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #28]\n"
        "vfma.f32 q2, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #44]\n"
        "vfma.f32 q3, q4, r12\n"
        "le lr, 1b\n"
        "vstrw.32 q0, [%[output]]\n"
        "vstrw.32 q1, [%[output], #16]\n"
        "vstrw.32 q2, [%[output], #32]\n"
        "vstrw.32 q3, [%[output], #48]\n"
        : [samples] "+&r" (samples), [coefficients] "+&r" (coefficients)
        : [output] "r" (output), [taps] "r" (taps)
        : "q0", "q1", "q2", "q3", "q4", "r12", "lr", "cc", "memory");
}
#endif

KDA_NOINLINE static void fir_window(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    const size_t count = S->num_taps;
#if defined(__clang__)
    /* The public dispatcher sends only longer filters to this helper. */
    __builtin_assume(count > 4U);
#endif
    float32_t *const history = S->state->history;
    const float32_t *const coefficients = S->prepared;
    const size_t prefix = count - 1U;
    size_t offset = S->state->next;
    while (blockSize != 0U) {
        const uint32_t length = blockSize < KDA_FIR_CHUNK ? blockSize : KDA_FIR_CHUNK;
        if (offset + length > KDA_FIR_CHUNK) {
            /* Compact only when the next input chunk will not fit. */
            for (size_t k = 0; k < prefix; ++k) { history[k] = history[offset+k]; }
            offset = 0;
        }
        float32_t *const window = history + offset;
        for (uint32_t i = 0; i < length; ++i) { window[prefix+i] = pSrc[i]; }
        uint32_t i = 0;
#if KDA_FIR_MVE
        if (length < 4U) {
            for (; i < length; ++i) {
                float32x4_t partial = vdupq_n_f32(0.0f);
                for (size_t k = 0; k < count; k += 4U) {
                    const mve_pred16_t active = vctp32q((uint32_t)(count-k));
                    partial = vfmaq_f32(partial,vldrwq_z_f32(coefficients+k,active),
                                        vldrwq_z_f32(window+i+k,active));
                }
                pDst[i] = (vgetq_lane_f32(partial,0)+vgetq_lane_f32(partial,1))+
                          (vgetq_lane_f32(partial,2)+vgetq_lane_f32(partial,3));
            }
        }
#pragma clang loop unroll(disable)
        for (; i + 16U <= length; i += 16U) {
            fir_tile16(window+i, coefficients, pDst+i, (uint32_t)count);
        }
        if (i + 8U <= length) {
            fir_tile8(window+i, coefficients, pDst+i, (uint32_t)count);
            i += 8U;
        }
        for (; i < length; i += 4U) {
            const mve_pred16_t active = vctp32q(length-i);
            float32x4_t a = vdupq_n_f32(0.0f);
            for (size_t k = 0; k < count; ++k) {
                a = vfmaq_n_f32(a, vldrwq_z_f32(window+i+k,active), coefficients[k]);
            }
            vstrwq_p_f32(pDst+i,a,active);
        }
#else
        for (; i < length; ++i) {
            float32_t sum = 0.0f;
            for (size_t k = 0; k < count; ++k) { sum += coefficients[k] * window[i+k]; }
            pDst[i] = sum;
        }
#endif
        offset += length;
        pSrc += length; pDst += length; blockSize -= length;
    }
    S->state->next = offset;
}

void kda_fir_f32(const kda_fir_instance_f32 *S, const float32_t *pSrc,
                 float32_t *pDst, uint32_t blockSize)
{
    if (S->num_taps == 1U) { fir_scale(S,pSrc,pDst,blockSize); }
    else if (S->num_taps <= 4U) { fir_tiny(S,pSrc,pDst,blockSize); }
    else { fir_window(S,pSrc,pDst,blockSize); }
}
