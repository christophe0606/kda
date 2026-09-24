#include "kda_fir_f32.h"
#include "fir_oracle.h"
#include "fir_vectors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 0; } } while (0)

typedef struct {
    float *base;
    float *data;
    size_t count;
} guarded_buffer;

static guarded_buffer allocate_buffer(size_t count)
{
    guarded_buffer result = {0};
    result.base = malloc((count + 2U) * sizeof(float));
    if (result.base != NULL) {
        result.data = result.base + 1;
        result.count = count;
        result.base[0] = 12345.5f;
        result.base[count + 1U] = -23456.5f;
    }
    return result;
}

static int guards_ok(guarded_buffer buffer)
{
    return buffer.base[0] == 12345.5f &&
           buffer.base[buffer.count + 1U] == -23456.5f;
}

static int outputs_match(const float *coefficients, size_t n,
                         const float *input, const float *output, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        double magnitude;
        const double expected = fir_reference(coefficients, n, input, i, &magnitude);
        if (!fir_close(output[i], expected, magnitude, n)) {
            return 0;
        }
    }
    return 1;
}

static int matrix_case(uint16_t n, uint32_t block, unsigned pattern)
{
    const size_t length = (size_t)block * 8U;
    guarded_buffer coefficients = allocate_buffer(n);
    guarded_buffer prepared = allocate_buffer(kda_fir_coeff_f32_count(n));
    guarded_buffer history = allocate_buffer(kda_fir_history_f32_count(n));
    guarded_buffer input = allocate_buffer(length);
    guarded_buffer output = allocate_buffer(length);
    float *saved_input = malloc(length * sizeof(float));
    float *saved_coefficients = malloc((size_t)n * sizeof(float));
    CHECK(coefficients.data && prepared.data && history.data && input.data &&
          output.data && saved_input && saved_coefficients);
    uint32_t seed = UINT32_C(0x95e1a123) ^ n ^ (block << 16) ^ pattern;
    for (size_t k = 0; k < n; ++k) {
        coefficients.data[k] = pattern == 4 ? 0.125f :
            (float)((int)((k * 17U + 5U) % 31U) - 15) / 32.0f;
    }
    for (size_t i = 0; i < length; ++i) {
        input.data[i] = input_sample(pattern, i, &seed);
    }
    memcpy(saved_coefficients, coefficients.data, (size_t)n * sizeof(float));
    memcpy(saved_input, input.data, length * sizeof(float));
    kda_fir_state_f32 state;
    kda_fir_instance_f32 instance;
    CHECK(kda_fir_init_f32(&instance, &state, n, coefficients.data, n,
                           prepared.data, n, history.data, (size_t)n * 2U));
    for (size_t offset = 0; offset < length; offset += block) {
        kda_fir_f32(&instance, input.data + offset, output.data + offset, block);
    }
    if (!outputs_match(coefficients.data, n, input.data, output.data, length)) {
        fprintf(stderr, "Mismatch N=%u B=%u pattern=%u\n", (unsigned)n,
                (unsigned)block, pattern);
        return 0;
    }
    CHECK(memcmp(saved_input, input.data, length * sizeof(float)) == 0);
    CHECK(memcmp(saved_coefficients, coefficients.data, (size_t)n * sizeof(float)) == 0);
    CHECK(guards_ok(coefficients) && guards_ok(prepared) && guards_ok(history) &&
          guards_ok(input) && guards_ok(output));
    free(coefficients.base); free(prepared.base); free(history.base);
    free(input.base); free(output.base); free(saved_input); free(saved_coefficients);
    return 1;
}

static int lifecycle(void)
{
    float public_coefficients[] = {3, 2, 1};
    const float original[] = {3, 2, 1};
    float prepared[3], history[6], output[6];
    const float impulse[] = {1, 0, 0, 0, 0, 0};
    kda_fir_state_f32 state;
    kda_fir_instance_f32 instance;
    CHECK(kda_fir_init_f32(&instance, &state, 3, public_coefficients, 3,
                           prepared, 3, history, 6));
    /* Public coefficients are not referenced after initialization. */
    public_coefficients[0] = 99;
    for (size_t i = 0; i < 6; ++i) {
        kda_fir_f32(&instance, impulse + i, output + i, 1);
    }
    CHECK(output[0] == 1 && output[1] == 2 && output[2] == 3);
    CHECK(outputs_match(original, 3, impulse, output, 6));
    kda_fir_reset_f32(&instance);
    CHECK(state.next == 0);
    for (size_t k = 0; k < 6; ++k) { CHECK(history[k] == 0); }
    kda_fir_f32(&instance, impulse, output, 6);
    CHECK(outputs_match(original, 3, impulse, output, 6));
    const float changed[] = {-2, 4, 0.5f};
    CHECK(kda_fir_init_f32(&instance, &state, 3, changed, 3, prepared, 3, history, 6));
    kda_fir_f32(&instance, impulse, output, 2);
    kda_fir_f32(&instance, impulse + 2, output + 2, 4);
    CHECK(outputs_match(changed, 3, impulse, output, 6));
    /* Reinitialization with a new dimension must rebuild bounds and history. */
    CHECK(kda_fir_init_f32(&instance, &state, 1, changed, 1, prepared, 3, history, 6));
    kda_fir_f32(&instance, impulse, output, 6);
    CHECK(outputs_match(changed, 1, impulse, output, 6));
    return 1;
}

static int independent_variable_blocks(void)
{
    const float coefficients[] = {-0.5f, 0.25f, 0.75f, -0.125f, 1};
    float prepared[2][5], history[2][10], input[2][40], output[2][40];
    kda_fir_state_f32 state[2];
    kda_fir_instance_f32 instance[2];
    for (size_t s = 0; s < 2; ++s) {
        CHECK(kda_fir_init_f32(instance + s, state + s, 5, coefficients, 5,
                               prepared[s], 5, history[s], 10));
        for (size_t i = 0; i < 40; ++i) { input[s][i] = (float)(i + s * 3U) / 16; }
    }
    size_t offset = 0;
    unsigned step = 1;
    while (offset < 40) {
        uint32_t count = (uint32_t)(40 - offset);
        if (count > step) { count = step; }
        for (size_t s = 0; s < 2; ++s) {
            kda_fir_f32(instance + s, input[s] + offset, output[s] + offset, count);
        }
        offset += count;
        step = step == 7 ? 1 : step + 1;
    }
    CHECK(outputs_match(coefficients, 5, input[0], output[0], 40));
    CHECK(outputs_match(coefficients, 5, input[1], output[1], 40));
    return 1;
}

static int invalid_initialization(void)
{
    const float coefficients[] = {3, 2, 1};
    float prepared[3], history[6], input = 0.5f, output;
    kda_fir_state_f32 state;
    kda_fir_instance_f32 instance;
    CHECK(kda_fir_history_f32_count(0) == 0 && kda_fir_coeff_f32_count(0) == 0);
    CHECK(kda_fir_history_f32_count(UINT16_MAX) == (size_t)UINT16_MAX * 2U);
    CHECK(kda_fir_init_f32(&instance, &state, 3, coefficients, 3, prepared, 3, history, 6));
    kda_fir_f32(&instance, &input, &output, 1);
    unsigned char saved_instance[sizeof instance], saved_state[sizeof state];
    float saved_prepared[3], saved_history[6];
    memcpy(saved_instance, &instance, sizeof instance);
    memcpy(saved_state, &state, sizeof state);
    memcpy(saved_prepared, prepared, sizeof prepared);
    memcpy(saved_history, history, sizeof history);
    for (unsigned failure = 0; failure < 9; ++failure) {
        CHECK(!kda_fir_init_f32(failure == 0 ? NULL : &instance,
            failure == 1 ? NULL : &state, failure == 2 ? 0 : 3,
            failure == 3 ? NULL : coefficients, failure == 4 ? 2 : 3,
            failure == 5 ? NULL : prepared, failure == 6 ? 2 : 3,
            failure == 7 ? NULL : history, failure == 8 ? 5 : 6));
        CHECK(memcmp(saved_instance, &instance, sizeof instance) == 0);
        CHECK(memcmp(saved_state, &state, sizeof state) == 0);
        CHECK(memcmp(saved_prepared, prepared, sizeof prepared) == 0);
        CHECK(memcmp(saved_history, history, sizeof history) == 0);
    }
    return 1;
}

static int negative_controls(void)
{
    const float coefficients[] = {3, 2, 1}, wrong[] = {1, 2, 3};
    const float changed[] = {2, -1, 0.5f}, input[] = {1, 2, 4, 8, 16, 32};
    float prepared[3], history[6], output[6];
    kda_fir_state_f32 state;
    kda_fir_instance_f32 instance;
    CHECK(!fir_close(NAN, 0, 0, 3));
    CHECK(!fir_close(1.0f, 0, 0, 3));
    CHECK(kda_fir_init_f32(&instance, &state, 3, wrong, 3, prepared, 3, history, 6));
    kda_fir_f32(&instance, input, output, 6);
    CHECK(!outputs_match(coefficients, 3, input, output, 6));
    CHECK(kda_fir_init_f32(&instance, &state, 3, coefficients, 3, prepared, 3, history, 6));
    kda_fir_f32(&instance, input, output, 3);
    kda_fir_reset_f32(&instance);
    kda_fir_f32(&instance, input + 3, output + 3, 3);
    CHECK(!outputs_match(coefficients, 3, input, output, 6));
    kda_fir_reset_f32(&instance);
    kda_fir_f32(&instance, input, output, 6);
    CHECK(outputs_match(coefficients, 3, input, output, 6));
    CHECK(!outputs_match(changed, 3, input, output, 6)); /* Stale prepared taps. */
    output[2] = output[3]; /* Dropped/misaligned output. */
    CHECK(!outputs_match(coefficients, 3, input, output, 6));
    kda_fir_reset_f32(&instance);
    prepared[0] += 1;
    kda_fir_f32(&instance, input, output, 6);
    CHECK(!outputs_match(coefficients, 3, input, output, 6));
    CHECK(kda_fir_init_f32(&instance, &state, 3, coefficients, 3, prepared, 3, history, 6));
    kda_fir_f32(&instance, input, output, 3);
    history[state.next + 1U] += 10;
    kda_fir_f32(&instance, input + 3, output + 3, 3);
    CHECK(!outputs_match(coefficients, 3, input, output, 6));
    return 1;
}

int fir_contract_lifecycle_tests(void)
{
    return lifecycle() && independent_variable_blocks() &&
        invalid_initialization() && negative_controls();
}

#if !defined(KDA_FIR_TARGET_TESTS)
int main(void)
{
    for (size_t b = 0; b < sizeof blocks / sizeof blocks[0]; ++b) {
        for (size_t n = 0; n < sizeof taps / sizeof taps[0]; ++n) {
            for (unsigned pattern = 0; pattern < 7; ++pattern) {
                if (!matrix_case(taps[n], blocks[b], pattern)) { return EXIT_FAILURE; }
            }
        }
    }
    if (!matrix_case(UINT16_MAX, 3, 5) || !fir_contract_lifecycle_tests()) {
        return EXIT_FAILURE;
    }
    puts("FIR: 323 pairs x 7 patterns x 8 blocks, max taps, lifecycle and negative controls passed");
    return EXIT_SUCCESS;
}
#endif
