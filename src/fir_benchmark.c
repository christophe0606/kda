#include "fir_profile.h"
#include "fir_benchmark.h"
#include "fir_target.h"
#include "kda_fir_f32.h"
#include "fir_oracle.h"
#include "fir_vectors.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "dsp/filtering_functions.h"
#include "app_mem_regions.h"
#include <string.h>

#if KDA_APP_FIR == 5
#if defined(__FAST_MATH__) || !defined(__PMU_PRESENT) || __PMU_PRESENT != 1
#error "Benchmark checker requires strict math and the architectural PMU"
#endif
#define DTCM __attribute__((section(".bss.dtcm.fir"), aligned(32)))
#define CYCLE_BIT UINT32_C(0x80000000)
#define INTERVAL_LIMIT UINT32_C(0x10000000)
#define WARMUP 128U
volatile fir_bench_result kda_fir_benchmark DTCM;
static float coeff[128] DTCM, prepared[128] DTCM, history[256] DTCM;
static float baseline_coeff[132] DTCM, baseline_history[1156] DTCM;
static float input[512] DTCM, baseline_input[516] DTCM;
static float output[512] DTCM, baseline_output[516] DTCM;
static kda_fir_state_f32 state DTCM;
static kda_fir_instance_f32 candidate DTCM;
static arm_fir_instance_f32 baseline DTCM;
extern unsigned char Image$$ARM_LIB_STACK$$ZI$$Base[];
extern unsigned char Image$$ARM_LIB_STACK$$ZI$$Limit[];

/* Leaf routines use only caller-saved registers and never touch the stack.
 * Paint unused space before each batch, then retain the lowest changed word.
 * MSPLIM independently bounds allocations even if a stored word matches paint. */
__attribute__((naked, noinline)) static void stack_paint(uint32_t *base)
{
    __asm volatile("mrs r1, msp\nmovw r2, #0xa55a\nmovt r2, #0xc33c\n"
                   "1: cmp r0, r1\nbhs 2f\nstr r2, [r0], #4\nb 1b\n2: bx lr");
}
__attribute__((naked, noinline)) static uint32_t stack_scan(uint32_t *base)
{
    __asm volatile("mrs r1, msp\nmovw r2, #0xa55a\nmovt r2, #0xc33c\n"
                   "1: cmp r0, r1\nbhs 2f\nldr r3, [r0]\ncmp r2, r3\n"
                   "bne 2f\nadds r0, #4\nb 1b\n2: bx lr");
}

static void cycle_stop(void)
{
    PMU->CTRL |= PMU_CTRL_CYCCNT_DISABLE_Msk;
    ARM_PMU_CNTR_Disable(CYCLE_BIT);
    __DSB(); __ISB();
}

static void cycle_start(void)
{
    ARM_PMU_CNTR_Enable(CYCLE_BIT);
    PMU->CTRL &= ~PMU_CTRL_CYCCNT_DISABLE_Msk;
    __DSB(); __ISB();
}

static int dtcm_object(const void *p, uint32_t size)
{
    const uintptr_t begin = (uintptr_t)p;
    return begin >= APP_DTCM_BASE && begin < APP_DTCM_BASE + APP_HP_DTCM_SIZE &&
           size <= APP_DTCM_BASE + APP_HP_DTCM_SIZE - begin;
}

static int record_placement(void)
{
    const uintptr_t code[] = {(uintptr_t)kda_fir_f32, (uintptr_t)arm_fir_f32,
        (uintptr_t)fir_batch_candidate, (uintptr_t)fir_batch_baseline,
        (uintptr_t)fir_batch_empty};
    const void *data[] = {coeff, prepared, history, baseline_coeff, baseline_history,
        input, baseline_input, output, baseline_output, &state, &candidate, &baseline};
    const uint32_t sizes[] = {sizeof coeff, sizeof prepared, sizeof history,
        sizeof baseline_coeff, sizeof baseline_history, sizeof input, sizeof baseline_input,
        sizeof output, sizeof baseline_output, sizeof state, sizeof candidate, sizeof baseline};
    int valid = KDA_FIR_TCM != 0;
    for (unsigned i = 0; i < 5; ++i) {
        kda_fir_benchmark.code[i] = (uint32_t)code[i];
        valid &= (code[i] & ~(uintptr_t)1) < APP_ITCM_BASE + APP_HP_ITCM_SIZE;
    }
    for (unsigned i = 0; i < 12; ++i) {
        kda_fir_benchmark.data[i][0] = (uint32_t)(uintptr_t)data[i];
        kda_fir_benchmark.data[i][1] = sizes[i];
        valid &= dtcm_object(data[i], sizes[i]);
    }
    return valid && dtcm_object((void *)(uintptr_t)(__get_MSP() - 1024U), 1024U);
}

__attribute__((noinline)) static uint32_t interval(fir_batch_fn function, uint32_t repetitions,
                         const void *instance, const float *src, float *dst,
                         uint32_t block, uint32_t *valid)
{
    stack_paint((uint32_t *)Image$$ARM_LIB_STACK$$ZI$$Base);
    cycle_stop();
    ARM_PMU_CYCCNT_Reset();
    ARM_PMU_Set_CNTR_OVS(CYCLE_BIT);
    __DSB(); __ISB();
    cycle_start();
    __DSB(); __ISB();
    const uint32_t start = ARM_PMU_Get_CCNTR();
    __asm volatile("" : : : "memory");
    function(repetitions, instance, src, dst, block);
    __asm volatile("" : : : "memory");
    __DSB(); __ISB();
    const uint32_t end = ARM_PMU_Get_CCNTR();
    cycle_stop();
    const uint32_t overflow = ARM_PMU_Get_CNTR_OVS();
    const uint32_t elapsed = end - start;
    *valid &= end > start && elapsed < INTERVAL_LIMIT && !(overflow & CYCLE_BIT);
    const uint32_t low = stack_scan((uint32_t *)Image$$ARM_LIB_STACK$$ZI$$Base);
    if (low < kda_fir_benchmark.stack_low) { kda_fir_benchmark.stack_low = low; }
    return elapsed;
}

__attribute__((noinline)) static void probe_work(uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) { __NOP(); }
}

static int pmu_probe(void)
{
    ARM_PMU_CNTR_Disable(UINT32_MAX);
    ARM_PMU_Set_CNTR_IRQ_Disable(UINT32_MAX);
    /* No architecture prescaler field is exposed on this M-profile PMU.
     * Preserve the existing cycle filter; readback and probes gate the capture. */
    PMU->CTRL = 0;
    ARM_PMU_Enable();
    cycle_stop();
    ARM_PMU_CYCCNT_Reset();
    cycle_start();
    kda_fir_benchmark.pmu_ctrl = PMU->CTRL;
    __DSB(); __ISB();
    uint32_t start = ARM_PMU_Get_CCNTR();
    probe_work(1024);
    kda_fir_benchmark.probe_short = ARM_PMU_Get_CCNTR() - start;
    start = ARM_PMU_Get_CCNTR();
    probe_work(2048);
    kda_fir_benchmark.probe_long = ARM_PMU_Get_CCNTR() - start;
    cycle_stop();
    __DSB(); __ISB();
    start = ARM_PMU_Get_CCNTR();
    probe_work(1024);
    kda_fir_benchmark.probe_frozen = ARM_PMU_Get_CCNTR() - start;
    /* Controlled rollover checks actual overflow reporting, outside measurement. */
    PMU->CCNTR = UINT32_MAX - 128U;
    ARM_PMU_Set_CNTR_OVS(CYCLE_BIT);
    cycle_start();
    probe_work(1024);
    cycle_stop();
    kda_fir_benchmark.probe_wrap = ARM_PMU_Get_CNTR_OVS();
    ARM_PMU_Set_CNTR_OVS(CYCLE_BIT);
    kda_fir_benchmark.pmu_filter = PMU->CCFILTR;
    kda_fir_benchmark.pmu_enable = PMU->CNTENSET;
    kda_fir_benchmark.pmu_irq = PMU->INTENSET;
    return kda_fir_benchmark.probe_short >= 1024U &&
           kda_fir_benchmark.probe_long > kda_fir_benchmark.probe_short &&
           kda_fir_benchmark.probe_frozen == 0 &&
           (kda_fir_benchmark.probe_wrap & CYCLE_BIT) &&
           PMU->CTRL == (1U | PMU_CTRL_CYCCNT_DISABLE_Msk) &&
           PMU->INTENSET == 0 && PMU->CCFILTR == kda_fir_benchmark.saved_filter;
}

static void fixtures(uint32_t block, uint16_t n)
{
    uint32_t seed = UINT32_C(0x95e1a123) ^ n ^ (block << 16) ^ 5U;
    memset(baseline_coeff, 0, sizeof baseline_coeff);
    for (uint32_t i = 0; i < n; ++i) {
        coeff[i] = (float)((int)((i * 17U + 5U) % 31U) - 15) / 32.0f;
        baseline_coeff[i] = coeff[i];
    }
    for (uint32_t i = 0; i < block; ++i) {
        input[i] = input_sample(5, i, &seed);
        baseline_input[i] = input[i];
    }
    kda_fir_init_f32(&candidate, &state, n, coeff, n, prepared, n, history, 2U*n);
    arm_fir_init_f32(&baseline, n, baseline_coeff, baseline_history, block);
    fir_batch_candidate(WARMUP, &candidate, input, output, block);
    fir_batch_baseline(WARMUP, &baseline, baseline_input, baseline_output, block);
}

/* After 128 warmup calls even B=1,N=128 has reached the periodic input stream.
 * Repeated calls keep that stream; every final output is checked independently. */
static int outputs_ok(uint32_t block, uint16_t n)
{
    for (uint32_t i = 0; i < block; ++i) {
        double expected = 0, magnitude = 0;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t index = (i + n * block - k) % block;
            const double term = (double)coeff[n - 1U - k] * input[index];
            expected += term;
            magnitude += term < 0 ? -term : term;
        }
        if (!fir_close(output[i], expected, magnitude, n) ||
            !fir_close(baseline_output[i], expected, magnitude, n)) { return 0; }
    }
    return 1;
}

void kda_fir_benchmark_run(void)
{
    /* Avoid a large aggregate temporary on the bounded target stack. */
    volatile unsigned char *record = (volatile unsigned char *)&kda_fir_benchmark;
    for (size_t i = 0; i < sizeof kda_fir_benchmark; ++i) { record[i] = 0; }
    kda_fir_benchmark.magic = UINT32_C(0x504d5531);
    kda_fir_benchmark.version = 2;
    kda_fir_benchmark.bytes = sizeof kda_fir_benchmark;
    kda_fir_benchmark.phase = 1;
    kda_fir_benchmark.mode = KDA_APP_FIR;
    kda_fir_benchmark.tcm_requested = KDA_FIR_TCM;
    kda_fir_benchmark.system_clock = SystemCoreClock;
    kda_fir_benchmark.fpscr = __get_FPSCR();
    kda_fir_benchmark.control = __get_CONTROL();
    kda_fir_benchmark.primask = __get_PRIMASK();
    kda_fir_benchmark.msp = __get_MSP();
    kda_fir_benchmark.psp = __get_PSP();
    kda_fir_benchmark.ccr = SCB->CCR;
    kda_fir_benchmark.pmu_type = PMU->TYPE;
    kda_fir_benchmark.pmu_auth = PMU->AUTHSTATUS;
    kda_fir_benchmark.seed = UINT32_C(0x95e1a123);
    kda_fir_benchmark.warmup_calls = WARMUP;
    kda_fir_benchmark.maximum_interval = INTERVAL_LIMIT;
    const uint32_t identity[8] = KDA_BUILD_ID;
    for (unsigned i = 0; i < 8; ++i) { kda_fir_benchmark.build_id[i] = identity[i]; }
    kda_fir_benchmark.stack_base = (uint32_t)(uintptr_t)Image$$ARM_LIB_STACK$$ZI$$Base;
    kda_fir_benchmark.stack_top = (uint32_t)(uintptr_t)Image$$ARM_LIB_STACK$$ZI$$Limit;
    kda_fir_benchmark.stack_low = __get_MSP();
    kda_fir_benchmark.stack_limit = __get_MSPLIM();
    if (!record_placement() || !(PMU->TYPE & PMU_TYPE_CYCCNT_PRESENT_Msk)) {
        kda_fir_benchmark.failures = 1;
        kda_fir_benchmark.phase = 4;
        return;
    }
    kda_fir_benchmark.saved_ctrl = PMU->CTRL;
    kda_fir_benchmark.saved_filter = PMU->CCFILTR;
    kda_fir_benchmark.saved_enable = PMU->CNTENSET;
    kda_fir_benchmark.saved_irq = PMU->INTENSET;
    kda_fir_benchmark.saved_overflow = PMU->OVSSET;
    kda_fir_benchmark.saved_ccntr = PMU->CCNTR;
    __disable_irq();
    if (!pmu_probe()) { ++kda_fir_benchmark.failures; goto done; }
    uint32_t valid = 1;
    kda_fir_benchmark.endpoint_cycles = interval(fir_batch_empty, 0, &candidate,
                                               input, output, 1, &valid);
    if (!valid) { ++kda_fir_benchmark.failures; goto done; }
    kda_fir_benchmark.phase = 2;
    for (unsigned bi = 0; bi < sizeof blocks / sizeof blocks[0]; ++bi) {
        for (unsigned ni = 0; ni < sizeof taps / sizeof taps[0]; ++ni) {
            const uint32_t block = blocks[bi];
            const uint16_t n = taps[ni];
            volatile fir_bench_case *r = &kda_fir_benchmark.cases[bi*17U+ni];
            r->block = block; r->taps = n;
            fixtures(block, n);
            uint32_t reps = 128;
            /* Target >=100k cycles for both calls; keep common repetitions. */
            while (reps < 32768U) {
                const uint32_t a = interval(fir_batch_candidate, reps, &candidate, input, output, block, &valid);
                const uint32_t b = interval(fir_batch_baseline, reps, &baseline, baseline_input, baseline_output, block, &valid);
                if (!valid || (a >= 100000U && b >= 100000U)) { break; }
                reps *= 2U;
            }
            r->repetitions = reps;
            for (unsigned batch = 0; batch < FIR_BATCHES && valid; ++batch) {
                /* Reinitialize and warm equally; all preparation outside timing. */
                fixtures(block, n);
                kda_fir_benchmark.control_cycles[bi*17U+ni][batch] =
                    interval(fir_batch_control, reps, &candidate, input, output, block, &valid);
                r->empty[batch] = interval(fir_batch_empty, reps, &candidate, input, output, block, &valid);
                if (batch & 1U) {
                    r->baseline[batch] = interval(fir_batch_baseline, reps, &baseline, baseline_input, baseline_output, block, &valid);
                    r->candidate[batch] = interval(fir_batch_candidate, reps, &candidate, input, output, block, &valid);
                } else {
                    r->candidate[batch] = interval(fir_batch_candidate, reps, &candidate, input, output, block, &valid);
                    r->baseline[batch] = interval(fir_batch_baseline, reps, &baseline, baseline_input, baseline_output, block, &valid);
                }
                valid &= outputs_ok(block, n);
            }
            if (!valid) { r->flags = 1; ++kda_fir_benchmark.failures; goto done; }
            ++kda_fir_benchmark.cases_completed;
        }
    }
done:
    cycle_stop();
    ARM_PMU_CNTR_Disable(UINT32_MAX);
    ARM_PMU_Set_CNTR_IRQ_Disable(UINT32_MAX);
    ARM_PMU_Disable();
    PMU->CCFILTR = kda_fir_benchmark.saved_filter;
    PMU->CCNTR = kda_fir_benchmark.saved_ccntr;
    ARM_PMU_Set_CNTR_OVS(UINT32_MAX);
    PMU->OVSSET = kda_fir_benchmark.saved_overflow;
    PMU->CTRL = kda_fir_benchmark.saved_ctrl;
    ARM_PMU_CNTR_Enable(kda_fir_benchmark.saved_enable);
    ARM_PMU_Set_CNTR_IRQ_Enable(kda_fir_benchmark.saved_irq);
    __DSB(); __ISB();
    kda_fir_benchmark.restored = PMU->CTRL == kda_fir_benchmark.saved_ctrl &&
        PMU->CCFILTR == kda_fir_benchmark.saved_filter &&
        PMU->CNTENSET == kda_fir_benchmark.saved_enable &&
        PMU->INTENSET == kda_fir_benchmark.saved_irq;
    kda_fir_benchmark.faults = SCB->CFSR;
    __set_PRIMASK(kda_fir_benchmark.primask);
    kda_fir_benchmark.phase = kda_fir_benchmark.failures ? 4 : 3;
}
#endif
