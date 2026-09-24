#ifndef KDA_FIR_BENCHMARK_H
#define KDA_FIR_BENCHMARK_H
#include <stdint.h>
#define FIR_BATCHES 31U
#define FIR_CASES 323U
typedef void (*fir_batch_fn)(uint32_t, const void *, const float *, float *, uint32_t);
void fir_batch_candidate(uint32_t, const void *, const float *, float *, uint32_t);
void fir_batch_baseline(uint32_t, const void *, const float *, float *, uint32_t);
void fir_batch_empty(uint32_t, const void *, const float *, float *, uint32_t);
void fir_batch_control(uint32_t, const void *, const float *, float *, uint32_t);

typedef struct {
    uint32_t block, taps, repetitions, flags;
    uint32_t empty[FIR_BATCHES], candidate[FIR_BATCHES], baseline[FIR_BATCHES];
} fir_bench_case;

/* Word-only wire format for supported debugger memory export. */
typedef struct {
    uint32_t magic, version, bytes, phase, cases_completed, failures;
    uint32_t mode, tcm_requested, system_clock, fpscr, control, primask, msp, psp;
    uint32_t ccr, pmu_type, pmu_auth, pmu_ctrl, pmu_filter, pmu_enable, pmu_irq;
    uint32_t probe_short, probe_long, probe_frozen, probe_wrap, endpoint_cycles;
    uint32_t code[5], data[12][2], seed, warmup_calls, maximum_interval;
    uint32_t saved_ctrl, saved_filter, saved_enable, saved_irq, saved_overflow;
    uint32_t saved_ccntr, restored, faults;
    fir_bench_case cases[FIR_CASES];
    uint32_t build_id[8];
    uint32_t stack_base, stack_top, stack_low, stack_limit;
    uint32_t control_cycles[FIR_CASES][FIR_BATCHES];
} fir_bench_result;
extern volatile fir_bench_result kda_fir_benchmark;
void kda_fir_benchmark_run(void);
#endif
