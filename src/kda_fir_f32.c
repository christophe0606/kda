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
#define KDA_INLINE __forceinline
#else
#define KDA_NOINLINE __attribute__((noinline))
#define KDA_INLINE inline __attribute__((always_inline))
#endif
#if defined(KDA_FIR_REQUIRE_MVE) && !KDA_FIR_MVE
#error "The target FIR candidate requires floating-point MVE"
#endif

_Static_assert(sizeof(float32_t) == 4, "FIR requires a 32-bit float");

static kda_fir_processor_f32 select_processor(uint16_t num_taps);

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
    S->process = select_processor(num_taps);
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

KDA_INLINE static void fir_tiny(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize, size_t count)
{
    float32_t *const history = S->state->history;
    const float32_t *const coefficients = S->prepared;
    float32_t h0 = history[0];
    float32_t h1 = count >= 3U ? history[1] : 0;
    float32_t h2 = count == 4U ? history[2] : 0;
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
    if (blockSize == 0U) {
        memcpy(history,&c0,4);
        if (count >= 3U) { memcpy(history+1,&c1,4); }
        if (count == 4U) { memcpy(history+2,&c2,4); }
        return;
    }
    if (count >= 3U && blockSize >= 2U) {
        /* Only two or three lanes are live. Shifted-out inactive lanes do
         * not represent history; retain the last valid inputs explicitly. */
        const uint32_t previous = c0;
        const mve_pred16_t active = vctp32q(blockSize);
        const float32x4_t current = vldrwq_z_f32(pSrc,active);
        float32x4_t sum = vmulq_n_f32(current,b0);
        uint32x4_t delayed = vshlcq_u32(vreinterpretq_u32_f32(current),&c0,32);
        sum = vfmaq_n_f32(sum,vreinterpretq_f32_u32(delayed),b1);
        delayed = vshlcq_u32(delayed,&c1,32);
        sum = vfmaq_n_f32(sum,vreinterpretq_f32_u32(delayed),b2);
        if (count == 4U) {
            delayed = vshlcq_u32(delayed,&c2,32);
            sum = vfmaq_n_f32(sum,vreinterpretq_f32_u32(delayed),b3);
        }
        vstrwq_p_f32(pDst,sum,active);
        history[0] = pSrc[blockSize-1U];
        history[1] = pSrc[blockSize-2U];
        if (count == 4U) {
            if (blockSize == 3U) { history[2] = pSrc[0]; }
            else { memcpy(history+2,&previous,4); }
        }
        return;
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
    history[0] = h0;
    if (count >= 3U) { history[1] = h1; }
    if (count == 4U) { history[2] = h2; }
}

KDA_NOINLINE void fir_short(const kda_fir_instance_f32 *S,
    const float32_t *__restrict pSrc, float32_t *__restrict pDst,
    uint32_t blockSize)
{
    const uint32_t count = S->num_taps;
#if defined(__clang__)
    __builtin_assume(count > 32U && blockSize < 8U);
#endif
    const uint32_t prefix = count - 1U;
    float32_t *const history = S->state->history;
    const float32_t *const coefficients = S->prepared;
    size_t offset = count > 32U ? S->state->next : 0U;
    if (offset + blockSize > KDA_FIR_CHUNK) {
        for (uint32_t k = 0; k < prefix; ++k) { history[k] = history[offset+k]; }
        offset = 0;
    }
    float32_t *const window = history + offset;
#if KDA_FIR_MVE
    for (uint32_t i = 0; i < blockSize; i += 4U) {
        const mve_pred16_t active = vctp32q(blockSize-i);
        vstrwq_p_f32(window+prefix+i,vldrwq_z_f32(pSrc+i,active),active);
    }
#else
    for (uint32_t i = 0; i < blockSize; ++i) { window[prefix+i] = pSrc[i]; }
#endif
    uint32_t i = 0;
#if KDA_FIR_MVE
#pragma clang loop unroll(disable)
    for (; i + 1U < blockSize; i += 2U) {
        float32x4_t a = vdupq_n_f32(0.0f), b = vdupq_n_f32(0.0f);
        for (uint32_t k = 0; k < count; k += 4U) {
            const mve_pred16_t active = vctp32q(count-k);
            const float32x4_t c = vldrwq_z_f32(coefficients+k,active);
            a = vfmaq_m_f32(a,c,vldrwq_z_f32(window+i+k,active),active);
            b = vfmaq_m_f32(b,c,vldrwq_z_f32(window+i+1U+k,active),active);
        }
        pDst[i] = (vgetq_lane_f32(a,0)+vgetq_lane_f32(a,1))+
                  (vgetq_lane_f32(a,2)+vgetq_lane_f32(a,3));
        pDst[i+1U] = (vgetq_lane_f32(b,0)+vgetq_lane_f32(b,1))+
                     (vgetq_lane_f32(b,2)+vgetq_lane_f32(b,3));
    }
    if (i < blockSize) {
        float32x4_t a = vdupq_n_f32(0.0f);
        for (uint32_t k = 0; k < count; k += 4U) {
            const mve_pred16_t active = vctp32q(count-k);
            a = vfmaq_f32(a,vldrwq_z_f32(coefficients+k,active),
                           vldrwq_z_f32(window+i+k,active));
        }
        pDst[i] = (vgetq_lane_f32(a,0)+vgetq_lane_f32(a,1))+
                  (vgetq_lane_f32(a,2)+vgetq_lane_f32(a,3));
    }
#else
    for (; i < blockSize; ++i) {
        float32_t sum = 0.0f;
        for (uint32_t k = 0; k < count; ++k) { sum += coefficients[k]*window[i+k]; }
        pDst[i] = sum;
    }
#endif
    if (count > 32U) { S->state->next = offset + blockSize; }
    else {
        /* Ascending copy is safe even when the two history spans overlap. */
        for (uint32_t k = 0; k < prefix; ++k) { history[k] = history[blockSize+k]; }
    }
}

#if KDA_FIR_MVE
static inline void fir_tile8(const float32_t *samples,
    const float32_t *coefficients, float32_t *output, uint32_t taps)
{
    /* Callers have more than four taps. Seed the first product, then
     * overlap contiguous loads/arithmetic and final arithmetic/stores. */
    __asm volatile(
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q2, [%[samples]], #4\n"
        "vmul.f32 q0, q2, r12\n"
        "vldrw.u32 q2, [%[samples], #12]\n"
        "vmul.f32 q1, q2, r12\n"
        "dls lr, %[taps]\n"
        ".p2align 2\n"
        "1:\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q2, [%[samples]], #4\n"
        "vfma.f32 q0, q2, r12\n"
        "vldrw.u32 q2, [%[samples], #12]\n"
        "vfma.f32 q1, q2, r12\n"
        "le lr, 1b\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q2, [%[samples]], #4\n"
        "vfma.f32 q0, q2, r12\n"
        "vstrw.32 q0, [%[output]]\n"
        "vldrw.u32 q2, [%[samples], #12]\n"
        "vfma.f32 q1, q2, r12\n"
        "vstrw.32 q1, [%[output], #16]\n"
        : [samples] "+&r" (samples), [coefficients] "+&r" (coefficients)
        : [output] "r" (output), [taps] "r" (taps - 2U)
        : "q0", "q1", "q2", "r12", "lr", "cc", "memory");
}

static inline void fir_tail8_window(const float32_t *samples,
    const float32_t *coefficients, float32_t *output, uint32_t taps, uint32_t length)
{
    /* Only for the initialized work allocation, with at least taps+7 floats.
     * Extra output lanes may read its slack; the final store stays predicated.
     * Never use this helper directly on an exactly sized caller input. */
    register float32x4_t sum0 __asm("q0");
    register float32x4_t sum1 __asm("q1");
    __asm volatile(
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q2, [%[samples]], #4\n"
        "vmul.f32 q0, q2, r12\n"
        "vldrw.u32 q2, [%[samples], #12]\n"
        "vmul.f32 q1, q2, r12\n"
        "dls lr, %[taps]\n"
        ".p2align 2\n"
        "1:\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q2, [%[samples]], #4\n"
        "vfma.f32 q0, q2, r12\n"
        "vldrw.u32 q2, [%[samples], #12]\n"
        "vfma.f32 q1, q2, r12\n"
        "le lr, 1b\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q2, [%[samples]], #4\n"
        "vfma.f32 q0, q2, r12\n"
        "vldrw.u32 q2, [%[samples], #12]\n"
        "vfma.f32 q1, q2, r12\n"
        : "=&w" (sum0), "=&w" (sum1), [samples] "+&r" (samples), [coefficients] "+&r" (coefficients)
        : [taps] "r" (taps - 2U)
        : "q2", "r12", "lr", "cc", "memory");
    /* The asm does not touch P0. Expose the results so the compiler owns
     * the final predicate and can consume it without a save/restore pair. */
    vstrwq_f32(output,sum0);
    vstrwq_p_f32(output+4U,sum1,vctp32q(length-4U));
}

static inline void fir_tile16(const float32_t *samples,
    const float32_t *coefficients, float32_t *output, uint32_t taps)
{
    __asm volatile(
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q4, [%[samples]], #4\n"
        "vmul.f32 q0, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #12]\n"
        "vmul.f32 q1, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #28]\n"
        "vmul.f32 q2, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #44]\n"
        "vmul.f32 q3, q4, r12\n"
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
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q4, [%[samples]], #4\n"
        "vfma.f32 q0, q4, r12\n"
        "vstrw.32 q0, [%[output]]\n"
        "vldrw.u32 q4, [%[samples], #12]\n"
        "vfma.f32 q1, q4, r12\n"
        "vstrw.32 q1, [%[output], #16]\n"
        "vldrw.u32 q4, [%[samples], #28]\n"
        "vfma.f32 q2, q4, r12\n"
        "vstrw.32 q2, [%[output], #32]\n"
        "vldrw.u32 q4, [%[samples], #44]\n"
        "vfma.f32 q3, q4, r12\n"
        "vstrw.32 q3, [%[output], #48]\n"
        : [samples] "+&r" (samples), [coefficients] "+&r" (coefficients)
        : [output] "r" (output), [taps] "r" (taps - 2U)
        : "q0", "q1", "q2", "q3", "q4", "r12", "lr", "cc", "memory");
}

/* Only the final vector is partial; 13 <= length <= 15. Preserve the
 * caller predicate explicitly because AC6 has no inline-asm VPR clobber. */
static inline void fir_tail16(const float32_t *samples,
    const float32_t *coefficients, float32_t *output, uint32_t taps, uint32_t length)
{
    uint32_t saved_predicate;
    __asm volatile(
        "vmrs %[saved], p0\n"
        "vctp.32 %[tail]\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q4, [%[samples]], #4\n"
        "vmul.f32 q0, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #12]\n"
        "vmul.f32 q1, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #28]\n"
        "vmul.f32 q2, q4, r12\n"
        "vpst\n"
        "vldrwt.u32 q4, [%[samples], #44]\n"
        "vmul.f32 q3, q4, r12\n"
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
        "vpst\n"
        "vldrwt.u32 q4, [%[samples], #44]\n"
        "vfma.f32 q3, q4, r12\n"
        "le lr, 1b\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q4, [%[samples]], #4\n"
        "vfma.f32 q0, q4, r12\n"
        "vstrw.32 q0, [%[output]]\n"
        "vldrw.u32 q4, [%[samples], #12]\n"
        "vfma.f32 q1, q4, r12\n"
        "vstrw.32 q1, [%[output], #16]\n"
        "vldrw.u32 q4, [%[samples], #28]\n"
        "vfma.f32 q2, q4, r12\n"
        "vstrw.32 q2, [%[output], #32]\n"
        "vpst\n"
        "vldrwt.u32 q4, [%[samples], #44]\n"
        "vfma.f32 q3, q4, r12\n"
        "vpst\n"
        "vstrwt.32 q3, [%[output], #48]\n"
        "vmsr p0, %[saved]\n"
        : [saved] "=&r" (saved_predicate), [samples] "+&r" (samples), [coefficients] "+&r" (coefficients)
        : [output] "r" (output), [taps] "r" (taps - 2U), [tail] "r" (length - 12U)
        : "q0", "q1", "q2", "q3", "q4", "r12", "lr", "cc", "memory");
}

/* Only initialized work storage may supply the inactive input lanes. */
static inline void fir_tail16_window(const float32_t *samples,
    const float32_t *coefficients, float32_t *output, uint32_t taps, uint32_t length)
{
    uint32_t saved_predicate;
    __asm volatile(
        "vmrs %[saved], p0\n"
        "vctp.32 %[tail]\n"
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q4, [%[samples]], #4\n"
        "vmul.f32 q0, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #12]\n"
        "vmul.f32 q1, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #28]\n"
        "vmul.f32 q2, q4, r12\n"
        "vldrw.u32 q4, [%[samples], #44]\n"
        "vmul.f32 q3, q4, r12\n"
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
        "ldr r12, [%[coefficients]], #4\n"
        "vldrw.u32 q4, [%[samples]], #4\n"
        "vfma.f32 q0, q4, r12\n"
        "vstrw.32 q0, [%[output]]\n"
        "vldrw.u32 q4, [%[samples], #12]\n"
        "vfma.f32 q1, q4, r12\n"
        "vstrw.32 q1, [%[output], #16]\n"
        "vldrw.u32 q4, [%[samples], #28]\n"
        "vfma.f32 q2, q4, r12\n"
        "vstrw.32 q2, [%[output], #32]\n"
        "vldrw.u32 q4, [%[samples], #44]\n"
        "vfma.f32 q3, q4, r12\n"
        "vpst\n"
        "vstrwt.32 q3, [%[output], #48]\n"
        "vmsr p0, %[saved]\n"
        : [saved] "=&r" (saved_predicate), [samples] "+&r" (samples), [coefficients] "+&r" (coefficients)
        : [output] "r" (output), [taps] "r" (taps - 2U), [tail] "r" (length - 12U)
        : "q0", "q1", "q2", "q3", "q4", "r12", "lr", "cc", "memory");
}
#endif


KDA_INLINE static void fir_small_core(const kda_fir_instance_f32 *S,
    const float32_t *__restrict pSrc, float32_t *__restrict pDst,
    uint32_t blockSize, uint32_t count, int rounded_history)
{
    (void)rounded_history;
#if defined(__clang__)
    __builtin_assume(count > 4U && count <= 32U && blockSize <= 8U);
#endif
    const uint32_t prefix = count - 1U;
    float32_t *const history = S->state->history;
    const float32_t *const coefficients = S->prepared;
#if KDA_FIR_MVE
    for (uint32_t i = 0; i < blockSize; i += 4U) {
        const mve_pred16_t active = vctp32q(blockSize-i);
        vstrwq_p_f32(history+prefix+i,vldrwq_z_f32(pSrc+i,active),active);
    }
    if (blockSize == 8U) { fir_tile8(history,coefficients,pDst,count); }
    else if (blockSize > 4U) { fir_tail8_window(history,coefficients,pDst,count,blockSize); }
    else {
        const mve_pred16_t active = vctp32q(blockSize);
        /* All lanes read the initialized N+127 work allocation. Only the
         * caller-facing output store needs the block-size predicate. */
        float32x4_t sum = vmulq_n_f32(vldrwq_f32(history),coefficients[0]);
        for (uint32_t k = 1; k < count; ++k) {
            sum = vfmaq_n_f32(sum,vldrwq_f32(history+k),coefficients[k]);
        }
        vstrwq_p_f32(pDst,sum,active);
    }
    /* The rounded vector retention also writes up to three scratch words.
     * Both spans stay in the initialized work allocation, even when B<H. */
    for (uint32_t k = 0; k < prefix; k += 4U) {
        if (rounded_history) {
            vstrwq_f32(history+k,vldrwq_f32(history+blockSize+k));
        } else {
            const mve_pred16_t active = vctp32q(prefix-k);
            vstrwq_p_f32(history+k,vldrwq_z_f32(history+blockSize+k,active),active);
        }
    }
#else
    for (uint32_t i = 0; i < blockSize; ++i) { history[prefix+i] = pSrc[i]; }
    for (uint32_t i = 0; i < blockSize; ++i) {
        float32_t sum = 0.0f;
        for (uint32_t k = 0; k < count; ++k) { sum += coefficients[k]*history[i+k]; }
        pDst[i] = sum;
    }
    for (uint32_t k = 0; k < prefix; ++k) { history[k] = history[blockSize+k]; }
#endif
}

KDA_NOINLINE void fir_small(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    fir_small_core(S,pSrc,pDst,blockSize,S->num_taps,0);
}

KDA_NOINLINE void fir_small7(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    fir_small_core(S,pSrc,pDst,blockSize,7U,1);
}

KDA_NOINLINE void fir_small8(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    fir_small_core(S,pSrc,pDst,blockSize,8U,1);
}

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
        if (length - i >= 13U) {
            fir_tail16(window+i, coefficients, pDst+i, (uint32_t)count, length-i);
            i = length;
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

KDA_INLINE static void fir_fixed_outputs(const float32_t *__restrict samples,
    const float32_t *__restrict coefficients, float32_t *__restrict output,
    uint32_t length, size_t count)
{
#if KDA_FIR_MVE
    /* A valid float buffer has a representable byte span. Exposing that
     * public precondition also proves the four-sample loop cannot wrap. */
    __builtin_assume(length <= SIZE_MAX / sizeof(float32_t));
#pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < length; i += 4U) {
        const mve_pred16_t active = vctp32q(length-i);
        float32x4_t sum = vdupq_n_f32(0.0f);
#pragma clang loop unroll(full)
        for (size_t k = 0; k < count; ++k) {
            sum = vfmaq_n_f32(sum, vldrwq_z_f32(samples+i+k,active), coefficients[k]);
        }
        vstrwq_p_f32(output+i,sum,active);
    }
#else
    for (uint32_t i = 0; i < length; ++i) {
        float32_t sum = 0.0f;
        for (size_t k = 0; k < count; ++k) { sum += samples[i+k]*coefficients[k]; }
        output[i] = sum;
    }
#endif
}

KDA_INLINE static void fir_medium_outputs(const float32_t *__restrict samples,
    const float32_t *__restrict coefficients, float32_t *__restrict output,
    uint32_t length, uint32_t count, int internal_window)
{
    (void)internal_window;
    uint32_t i = 0;
#if KDA_FIR_MVE
    __builtin_assume(length <= SIZE_MAX / sizeof(float32_t));
#pragma clang loop unroll(disable)
    for (; i + 16U <= length; i += 16U) {
        fir_tile16(samples+i, coefficients, output+i, count);
    }
    if (length - i >= 13U) {
        if (i != 0U) {
            /* Recompute preceding outputs, ending exactly at the public end.
             * length>=16 here, so neither source nor destination goes before
             * its supplied span. Accumulation order is identical. */
            fir_tile16(samples+length-16U,coefficients,output+length-16U,count);
        } else if (internal_window) {
            fir_tail16_window(samples+i, coefficients, output+i, count, length-i);
        } else {
            fir_tail16(samples+i, coefficients, output+i, count, length-i);
        }
        i = length;
    }
    if (i + 8U <= length) {
        fir_tile8(samples+i, coefficients, output+i, count);
        i += 8U;
    }
    if (length-i < 4U) {
#pragma clang loop unroll(disable)
        for (; i < length; ++i) {
            float32x4_t partial = vdupq_n_f32(0.0f);
#pragma clang loop unroll(disable)
            for (uint32_t k = 0; k < count; k += 4U) {
                const mve_pred16_t active = vctp32q(count-k);
                partial = vfmaq_f32(partial,vldrwq_z_f32(coefficients+k,active),
                                    vldrwq_z_f32(samples+i+k,active));
            }
            output[i] = (vgetq_lane_f32(partial,0)+vgetq_lane_f32(partial,1))+
                        (vgetq_lane_f32(partial,2)+vgetq_lane_f32(partial,3));
        }
    }
#pragma clang loop unroll(disable)
    for (; i < length; i += 4U) {
        const mve_pred16_t active = vctp32q(length-i);
        float32x4_t sum = vdupq_n_f32(0.0f);
        for (uint32_t k = 0; k < count; ++k) {
            sum = vfmaq_n_f32(sum, vldrwq_z_f32(samples+i+k,active), coefficients[k]);
        }
        vstrwq_p_f32(output+i,sum,active);
    }
#else
    for (; i < length; ++i) {
        float32_t sum = 0.0f;
        for (uint32_t k = 0; k < count; ++k) { sum += samples[i+k]*coefficients[k]; }
        output[i] = sum;
    }
#endif
}

KDA_NOINLINE void fir_medium_window(const kda_fir_instance_f32 *S,
    const float32_t *__restrict pSrc, float32_t *__restrict pDst,
    uint32_t blockSize)
{
    const uint32_t count = S->num_taps;
#if defined(__clang__)
    __builtin_assume(count > 8U && count <= 32U && blockSize > 8U && blockSize <= 32U);
#endif
    const uint32_t prefix = count - 1U;
    float32_t *const history = S->state->history;
#if KDA_FIR_MVE
#pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < blockSize; i += 4U) {
        const mve_pred16_t active = vctp32q(blockSize-i);
        vstrwq_p_f32(history+prefix+i,vldrwq_z_f32(pSrc+i,active),active);
    }
#else
    for (uint32_t i = 0; i < blockSize; ++i) { history[prefix+i] = pSrc[i]; }
#endif
    fir_medium_outputs(history,S->prepared,pDst,blockSize,count,1);
#if KDA_FIR_MVE
#pragma clang loop unroll(disable)
    for (uint32_t k = 0; k < prefix; k += 4U) {
        const mve_pred16_t active = vctp32q(prefix-k);
        vstrwq_p_f32(history+k,vldrwq_z_f32(history+blockSize+k,active),active);
    }
#else
    for (uint32_t k = 0; k < prefix; ++k) { history[k] = history[blockSize+k]; }
#endif
}

KDA_NOINLINE void fir_medium(const kda_fir_instance_f32 *S,
    const float32_t *__restrict pSrc, float32_t *__restrict pDst,
    uint32_t blockSize)
{
    const uint32_t count = S->num_taps;
#if defined(__clang__)
    /* The dispatcher sends every B<=32 call to small or medium_window. */
    __builtin_assume(count > 8U && count <= 32U && blockSize > 32U);
#endif
    float32_t *__restrict history = S->state->history;
    const float32_t *__restrict coefficients = S->prepared;
    const uint32_t prefix = count - 1U;
    /* B>32 and 9<=N<=32 imply a complete one- or two-tile boundary. */
    const uint32_t edge = count <= 17U ? 16U : 32U;
#if KDA_FIR_MVE
#pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < edge; i += 4U) {
        vstrwq_f32(history+prefix+i, vldrwq_f32(pSrc+i));
    }
    fir_tile16(history,coefficients,pDst,count);
    if (edge == 32U) { fir_tile16(history+16U,coefficients,pDst+16U,count); }
#else
    for (uint32_t i = 0; i < edge; ++i) { history[prefix+i] = pSrc[i]; }
    fir_medium_outputs(history, coefficients, pDst, edge, count, 1);
#endif
    fir_medium_outputs(pSrc+edge-prefix, coefficients, pDst+edge,
                       blockSize-edge, count, 0);
#if KDA_FIR_MVE
#pragma clang loop unroll(disable)
    for (uint32_t k = 0; k < prefix; k += 4U) {
        const mve_pred16_t active = vctp32q(prefix-k);
        vstrwq_p_f32(history+k, vldrwq_z_f32(pSrc+blockSize-prefix+k,active), active);
    }
#else
    for (uint32_t k = 0; k < prefix; ++k) { history[k] = pSrc[blockSize-prefix+k]; }
#endif
}

KDA_INLINE static void fir_fixed(const kda_fir_instance_f32 *S,
    const float32_t *__restrict pSrc, float32_t *__restrict pDst,
    uint32_t blockSize, size_t count)
{
    float32_t *__restrict history = S->state->history;
    const float32_t *__restrict coefficients = S->prepared;
    const size_t prefix = count - 1U;
    const uint32_t boundary = (uint32_t)((prefix + 3U) & ~(size_t)3U);
    /* The fixed-tap wrappers route B<=8 to the small helpers. Boundary is
     * four or eight, so these are complete vectors inside both public spans. */
    const uint32_t edge = boundary;
#if KDA_FIR_MVE
    for (uint32_t i = 0; i < edge; i += 4U) {
        vstrwq_f32(history+prefix+i, vldrwq_f32(pSrc+i));
    }
#pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < edge; i += 4U) {
        float32x4_t sum = vdupq_n_f32(0.0f);
#pragma clang loop unroll(full)
        for (size_t k = 0; k < count; ++k) {
            sum = vfmaq_n_f32(sum,vldrwq_f32(history+i+k),coefficients[k]);
        }
        vstrwq_f32(pDst+i,sum);
    }
#else
    for (uint32_t i = 0; i < edge; ++i) { history[prefix+i] = pSrc[i]; }
    fir_fixed_outputs(history, coefficients, pDst, edge, count);
#endif
    if (blockSize > edge) {
        fir_fixed_outputs(pSrc+edge-prefix, coefficients, pDst+edge,
                          blockSize-edge, count);
    }
    if (blockSize >= prefix) {
        for (size_t k = 0; k < prefix; ++k) { history[k] = pSrc[blockSize-prefix+k]; }
    } else {
        /* Forward overlap-safe retention for a block shorter than history. */
        for (size_t k = 0; k < prefix; ++k) { history[k] = history[blockSize+k]; }
    }
}

/* External linkage preserves the four-register ABI for dispatcher tail calls.
 * These helper symbols are internal to this translation unit's implementation;
 * only kda_fir_f32 is declared in the public processing interface. */
KDA_NOINLINE void fir_tiny2(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    fir_tiny(S, pSrc, pDst, blockSize, 2U);
}

KDA_NOINLINE void fir_tiny3(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    fir_tiny(S, pSrc, pDst, blockSize, 3U);
}

KDA_NOINLINE void fir_tiny4(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    fir_tiny(S, pSrc, pDst, blockSize, 4U);
}

KDA_NOINLINE void fir_fixed5(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    if (blockSize <= 8U) { fir_small(S,pSrc,pDst,blockSize); return; }
    fir_fixed(S, pSrc, pDst, blockSize, 5U);
}

KDA_NOINLINE void fir_fixed6(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    if (blockSize <= 8U) { fir_small(S,pSrc,pDst,blockSize); return; }
    fir_fixed(S, pSrc, pDst, blockSize, 6U);
}

KDA_NOINLINE void fir_fixed7(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    if (blockSize <= 8U) { fir_small7(S,pSrc,pDst,blockSize); return; }
    fir_fixed(S, pSrc, pDst, blockSize, 7U);
}

KDA_NOINLINE void fir_fixed8(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    if (blockSize <= 8U) { fir_small8(S,pSrc,pDst,blockSize); return; }
    fir_fixed(S, pSrc, pDst, blockSize, 8U);
}

KDA_NOINLINE void fir_dispatch_medium(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    if (blockSize <= 8U) { fir_small(S,pSrc,pDst,blockSize); }
    else if (blockSize <= 32U) { fir_medium_window(S,pSrc,pDst,blockSize); }
    else { fir_medium(S,pSrc,pDst,blockSize); }
}

KDA_NOINLINE void fir_dispatch_window(const kda_fir_instance_f32 *S,
    const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)
{
    if (blockSize < 8U) { fir_short(S,pSrc,pDst,blockSize); }
    else { fir_window(S,pSrc,pDst,blockSize); }
}

static kda_fir_processor_f32 select_processor(uint16_t num_taps)
{
    switch (num_taps) {
    case 1: return fir_scale;
    case 2: return fir_tiny2;
    case 3: return fir_tiny3;
    case 4: return fir_tiny4;
    case 5: return fir_fixed5;
    case 6: return fir_fixed6;
    case 7: return fir_fixed7;
    case 8: return fir_fixed8;
    default: return num_taps <= 32U ? fir_dispatch_medium : fir_dispatch_window;
    }
}

void kda_fir_f32(const kda_fir_instance_f32 *S, const float32_t *pSrc,
                 float32_t *pDst, uint32_t blockSize)
{
    S->process(S,pSrc,pDst,blockSize);
}
