#ifndef KDA_FIR_TARGET_H
#define KDA_FIR_TARGET_H
#include <stdint.h>

/* Stable debugger-visible progress; phase 1=matrix, 2=lifecycle, 3=done. */
typedef struct {
    uint32_t magic, phase, cases_completed, failures;
    uint32_t block, taps, pattern, sample, implementation;
    uint32_t fpscr_start, fpscr_end, mpu_type;
    uint32_t candidate_coeff_floats, candidate_history_floats;
    uint32_t baseline_coeff_floats, baseline_state_floats;
    double maximum_absolute_error[2], maximum_scaled_error[2];
    double failed_actual, failed_expected;
} fir_target_result;
extern volatile fir_target_result kda_fir_result;
void kda_fir_target_run(void);
#endif
