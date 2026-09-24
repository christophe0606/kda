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
    return count != 0 && count <= SIZE_MAX / (2U * sizeof(float32_t))
        ? 2U * count : 0;
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
        prepared[k] = coefficients[count - 1U - k];
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
    size_t next = S->state->next;

    for (uint32_t i = 0; i < blockSize; ++i) {
        history[next] = pSrc[i];
        history[next + count] = pSrc[i];
        float32_t sum;
#if KDA_FIR_MVE
        float32x4_t partial = vdupq_n_f32(0.0f);
        for (size_t k = 0; k < count; k += 4U) {
            const mve_pred16_t active = vctp32q((uint32_t)(count - k));
            const float32x4_t coefficients = vldrwq_z_f32(S->prepared + k, active);
            const float32x4_t samples = vldrwq_z_f32(history + next + k, active);
            partial = vfmaq_m_f32(partial, coefficients, samples, active);
        }
        sum = (vgetq_lane_f32(partial, 0) + vgetq_lane_f32(partial, 1)) +
              (vgetq_lane_f32(partial, 2) + vgetq_lane_f32(partial, 3));
#else
        sum = 0.0f;
        for (size_t k = 0; k < count; ++k) {
            sum += S->prepared[k] * history[next + k];
        }
#endif
        pDst[i] = sum;
        next = next == 0 ? count - 1U : next - 1U;
    }
    S->state->next = next;
}
