#include "fir_target.h"
#include "kda_fir_f32.h"
#include "fir_oracle.h"
#include "fir_vectors.h"
#include "RTE_Components.h"
#include CMSIS_device_header
/* Consume only the public declarations; comparator compilation is opaque. */
#include "dsp/filtering_functions.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(__FAST_MATH__)
#error "The target checker must use strict floating-point semantics"
#endif
#if !defined(ARM_MATH_MVEF) || defined(ARM_MATH_AUTOVECTORIZE)
#error "The opaque comparator must select its public MVE floating-point configuration"
#endif

#define DTCM __attribute__((section(".bss.dtcm.fir"), aligned(16)))
#define MAX_TAPS 128U
#define MAX_BLOCK 512U
#define STREAM_LENGTH (8U * MAX_BLOCK)
#define BASELINE_READ_MARGIN 3U

volatile fir_target_result kda_fir_result DTCM;
static float coefficients[MAX_TAPS] DTCM;
static float prepared[MAX_TAPS] DTCM;
static float history[2U * MAX_TAPS] DTCM;
static float baseline_coefficients[MAX_TAPS + BASELINE_READ_MARGIN] DTCM;
static float baseline_state[MAX_TAPS + 2U * MAX_BLOCK - 1U + BASELINE_READ_MARGIN] DTCM;
static float input[STREAM_LENGTH] DTCM;
static float baseline_input[STREAM_LENGTH + BASELINE_READ_MARGIN] DTCM;
static float candidate_output[STREAM_LENGTH] DTCM;
static float baseline_output[STREAM_LENGTH + BASELINE_READ_MARGIN] DTCM;
static kda_fir_instance_f32 candidate DTCM;
static kda_fir_state_f32 candidate_state DTCM;
static arm_fir_instance_f32 baseline DTCM;

int fir_contract_lifecycle_tests(void);

static int check_output(const float *output, uint32_t length, uint16_t n,
                        unsigned implementation)
{
    for (uint32_t i = 0; i < length; ++i) {
        double magnitude;
        const double expected = fir_reference(coefficients, n, input, i, &magnitude);
        const double error = fabs((double)output[i] - expected);
        const double e = 2.0 * (double)n * 0x1p-24;
        const double bound = 1e-7 + e / (1.0 - e) * magnitude;
        if (error > kda_fir_result.maximum_absolute_error[implementation]) {
            kda_fir_result.maximum_absolute_error[implementation] = error;
        }
        if (error / bound > kda_fir_result.maximum_scaled_error[implementation]) {
            kda_fir_result.maximum_scaled_error[implementation] = error / bound;
        }
        if (!fir_close(output[i], expected, magnitude, n)) {
            kda_fir_result.sample = i;
            kda_fir_result.implementation = implementation;
            kda_fir_result.failed_actual = output[i];
            kda_fir_result.failed_expected = expected;
            ++kda_fir_result.failures;
            return 0;
        }
    }
    return 1;
}

static int run_case(uint16_t n, uint32_t block, unsigned pattern)
{
    const uint32_t length = 8U * block;
    uint32_t seed = UINT32_C(0x95e1a123) ^ n ^ (block << 16) ^ pattern;
    kda_fir_result.block = block;
    kda_fir_result.taps = n;
    kda_fir_result.pattern = pattern;
    kda_fir_result.candidate_coeff_floats = n;
    kda_fir_result.candidate_history_floats = kda_fir_history_f32_count(n);
    kda_fir_result.baseline_coeff_floats = ((n + 3U) & ~3U) + BASELINE_READ_MARGIN;
    kda_fir_result.baseline_state_floats = n + 2U * block - 1U + BASELINE_READ_MARGIN;
    memset(baseline_coefficients, 0, sizeof baseline_coefficients);
    memset(baseline_state, 0, sizeof baseline_state);
    memset(baseline_input, 0, sizeof baseline_input);
    for (size_t k = 0; k < n; ++k) {
        coefficients[k] = pattern == 4 ? 0.125f :
            (float)((int)((k * 17U + 5U) % 31U) - 15) / 32.0f;
        baseline_coefficients[k] = coefficients[k];
    }
    for (size_t i = 0; i < length; ++i) {
        input[i] = input_sample(pattern, i, &seed);
        baseline_input[i] = input[i];
        candidate_output[i] = NAN;
        baseline_output[i] = NAN;
    }
    if (!kda_fir_init_f32(&candidate, &candidate_state, n, coefficients, n,
                          prepared, n, history, kda_fir_history_f32_count(n))) {
        ++kda_fir_result.failures;
        return 0;
    }
    /* v1.18.0 public usage: round coefficients to 4, state N+2B-1.
     * The public library overview also specifies three readable trailing words.
     * These requirements apply exclusively to the opaque baseline fixture.
     * Each baseline instance retains the block size given at initialization. */
    arm_fir_init_f32(&baseline, n, baseline_coefficients, baseline_state, block);
    for (uint32_t offset = 0; offset < length; offset += block) {
        kda_fir_f32(&candidate, input + offset, candidate_output + offset, block);
        arm_fir_f32(&baseline, baseline_input + offset, baseline_output + offset, block);
    }
    if (!check_output(candidate_output, length, n, 0) ||
        !check_output(baseline_output, length, n, 1)) { return 0; }
    seed = UINT32_C(0x95e1a123) ^ n ^ (block << 16) ^ pattern;
    for (size_t i = 0; i < length; ++i) {
        if (input[i] != input_sample(pattern, i, &seed) || baseline_input[i] != input[i]) {
            ++kda_fir_result.failures;
            return 0;
        }
    }
    for (size_t k = 0; k < n; ++k) {
        const float expected = pattern == 4 ? 0.125f :
            (float)((int)((k * 17U + 5U) % 31U) - 15) / 32.0f;
        if (coefficients[k] != expected || baseline_coefficients[k] != expected) {
            ++kda_fir_result.failures;
            return 0;
        }
    }
    ++kda_fir_result.cases_completed;
    return 1;
}

__attribute__((noinline)) static int baseline_lifecycle(void)
{
    float ca[4 + BASELINE_READ_MARGIN] = {3, 2, 1, 0};
    float cb[4 + BASELINE_READ_MARGIN] = {-1, 2, 0, 0};
    float sa[4 + BASELINE_READ_MARGIN], sb[3 + BASELINE_READ_MARGIN];
    float ya[6 + BASELINE_READ_MARGIN], yb[6 + BASELINE_READ_MARGIN];
    const float impulse[6 + BASELINE_READ_MARGIN] = {1};
    arm_fir_instance_f32 a, b;
    for (unsigned pass = 0; pass < 3; ++pass) {
        if (pass == 2) { ca[0] = -2; ca[1] = 4; ca[2] = 0.5f; }
        /* Repeat initialization tests reset; pass 2 replaces coefficients. */
        memset(sa, 0, sizeof sa);
        memset(sb, 0, sizeof sb);
        arm_fir_init_f32(&a, 3, ca, sa, 1);
        arm_fir_init_f32(&b, 2, cb, sb, 1);
        for (size_t i = 0; i < 6; ++i) {
            arm_fir_f32(&a, impulse + i, ya + i, 1);
            arm_fir_f32(&b, impulse + i, yb + i, 1);
            double q;
            double expected = fir_reference(ca, 3, impulse, i, &q);
            if (!fir_close(ya[i], expected, q, 3)) {
                kda_fir_result.sample = (uint32_t)i;
                kda_fir_result.pattern = pass;
                kda_fir_result.implementation = 1;
                kda_fir_result.taps = 3;
                kda_fir_result.block = 1;
                kda_fir_result.failed_actual = ya[i];
                kda_fir_result.failed_expected = expected;
                return 0;
            }
            expected = fir_reference(cb, 2, impulse, i, &q);
            if (!fir_close(yb[i], expected, q, 2)) {
                kda_fir_result.sample = (uint32_t)i;
                kda_fir_result.pattern = pass;
                kda_fir_result.implementation = 1;
                kda_fir_result.taps = 2;
                kda_fir_result.block = 1;
                kda_fir_result.failed_actual = yb[i];
                kda_fir_result.failed_expected = expected;
                return 0;
            }
        }
    }
    return 1;
}

void kda_fir_target_run(void)
{
    kda_fir_result = (fir_target_result){0};
    kda_fir_result.magic = UINT32_C(0x46495231);
    kda_fir_result.phase = 1;
    kda_fir_result.fpscr_start = __get_FPSCR();
    kda_fir_result.mpu_type = MPU->TYPE;
    for (size_t b = 0; b < sizeof blocks / sizeof blocks[0]; ++b) {
        for (size_t n = 0; n < sizeof taps / sizeof taps[0]; ++n) {
            for (unsigned pattern = 0; pattern < 7; ++pattern) {
                if (!run_case(taps[n], blocks[b], pattern)) { goto done; }
            }
        }
    }
    kda_fir_result.phase = 2;
    if (!fir_contract_lifecycle_tests()) { ++kda_fir_result.failures; }
    if (!baseline_lifecycle()) { ++kda_fir_result.failures; }
done:
    kda_fir_result.fpscr_end = __get_FPSCR();
    kda_fir_result.phase = 3;
    printf("FIR correctness: %lu/2261 cases, %lu failures; FPSCR=%08lx/%08lx\r\n",
           (unsigned long)kda_fir_result.cases_completed,
           (unsigned long)kda_fir_result.failures,
           (unsigned long)kda_fir_result.fpscr_start,
           (unsigned long)kda_fir_result.fpscr_end);
    fflush(stdout);
}
