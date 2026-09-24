#include "kda_fir_f32.h"

#include <string.h>

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 2)
#include <arm_mve.h>
#define KDA_FIR_MVE 1
#else
#define KDA_FIR_MVE 0
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

void kda_fir_f32(const kda_fir_instance_f32 *S, const float32_t *pSrc,
                 float32_t *pDst, uint32_t blockSize)
{
    const size_t count = S->num_taps;
    float32_t *const history = S->state->history;
    const float32_t *const coefficients = S->prepared;
    if (count == 1U) {
        const float32_t b0 = coefficients[0];
        for (uint32_t i = 0; i < blockSize; ++i) { pDst[i] = b0 * pSrc[i]; }
        return;
    }
    if (count <= 4U) {
        float32_t h0 = history[0], h1 = history[1], h2 = history[2];
        const float32_t b0 = coefficients[count-1U], b1 = coefficients[count-2U];
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
        return;
    }
    const size_t prefix = count - 1U;
    while (blockSize != 0U) {
        const uint32_t length = blockSize < KDA_FIR_CHUNK ? blockSize : KDA_FIR_CHUNK;
        for (uint32_t i = 0; i < length; ++i) { history[prefix+i] = pSrc[i]; }
        uint32_t i = 0;
#if KDA_FIR_MVE
        for (; i + 8U <= length; i += 8U) {
            float32x4_t a = vdupq_n_f32(0.0f), b = vdupq_n_f32(0.0f);
            for (size_t k = 0; k < count; ++k) {
                a = vfmaq_n_f32(a, vldrwq_f32(history+i+k), coefficients[k]);
                b = vfmaq_n_f32(b, vldrwq_f32(history+i+k+4U), coefficients[k]);
            }
            vstrwq_f32(pDst+i,a); vstrwq_f32(pDst+i+4U,b);
        }
        for (; i < length; i += 4U) {
            const mve_pred16_t active = vctp32q(length-i);
            float32x4_t a = vdupq_n_f32(0.0f);
            for (size_t k = 0; k < count; ++k) {
                a = vfmaq_n_f32(a, vldrwq_z_f32(history+i+k,active), coefficients[k]);
            }
            vstrwq_p_f32(pDst+i,a,active);
        }
#else
        for (; i < length; ++i) {
            float32_t sum = 0.0f;
            for (size_t k = 0; k < count; ++k) { sum += coefficients[k] * history[i+k]; }
            pDst[i] = sum;
        }
#endif
        /* Forward copy is safe for overlapping ranges: every source is ahead. */
        for (size_t k = 0; k < prefix; ++k) { history[k] = history[length+k]; }
        pSrc += length; pDst += length; blockSize -= length;
    }
}
