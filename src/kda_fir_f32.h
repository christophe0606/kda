#ifndef KDA_FIR_F32_H
#define KDA_FIR_F32_H

#include <stddef.h>
#include <stdint.h>

/* Public f32 scalar type; a repeated identical C11 typedef is permitted. */
typedef float float32_t;
#define KDA_FIR_CHUNK 128U

typedef struct {
    float32_t *history;
    size_t next;
} kda_fir_state_f32;

typedef struct {
    uint16_t num_taps;
    const float32_t *prepared;
    kda_fir_state_f32 *state;
} kda_fir_instance_f32;

/* Counts are floats, not bytes. Zero means invalid or unrepresentable. */
size_t kda_fir_history_f32_count(uint16_t num_taps);
size_t kda_fir_coeff_f32_count(uint16_t num_taps);

/* Coefficients arrive as {b[N-1], ..., b[0]}. All objects/buffers are disjoint.
 * Successful initialization copies coefficients, zeros history and publishes S.
 * Null pointers or insufficient capacities return 0 without modifying anything.
 * Caller owns prepared/history/state for the instance lifetime. The public
 * coefficients need not remain alive after a successful initialization.
 */
int kda_fir_init_f32(kda_fir_instance_f32 *S, kda_fir_state_f32 *state,
                     uint16_t num_taps,
                     const float32_t *coefficients, size_t coefficient_count,
                     float32_t *prepared, size_t prepared_count,
                     float32_t *history, size_t history_count);

/* Reset a valid initialized instance without changing its prepared coefficients. */
void kda_fir_reset_f32(const kda_fir_instance_f32 *S);

/* A valid initialized instance and positive blockSize are required. Input/output
 * contain blockSize floats and are disjoint from each other and instance storage.
 * Block size may vary between calls. No allocation or coefficient padding.
 * Natural float alignment is sufficient. See docs/fir.md for the full contract.
 */
void kda_fir_f32(const kda_fir_instance_f32 *S, const float32_t *pSrc,
                 float32_t *pDst, uint32_t blockSize);

#endif
