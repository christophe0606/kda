#ifndef KDA_FIR_GUARD_H
#define KDA_FIR_GUARD_H
#include <stdint.h>

typedef struct {
    uint32_t magic, mode, phase, cases_completed, failures;
    uint32_t block, taps, buffer, alignment, stage;
    uint32_t guard_begin, guard_end, buffer_begin, buffer_bytes;
    uint32_t mpu_type, mpu_ctrl, mair0, primask, control, msp, psp, ccr;
    uint32_t cfsr, hfsr, mmfar, bfar, exception, unexpected;
    uint32_t region_count, regions[16][2];
    uint32_t exc_return, frame_address, frame[8];
} fir_guard_result;

extern volatile fir_guard_result kda_fir_guard_result;
void kda_fir_guard_run(void);
#endif
