#include <stdio.h>

#if defined(KDA_ALIF_E8)
#include "fir_profile.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "retarget_init.h"
#if KDA_APP_FIR
#include "fir_target.h"
#include "fir_guard.h"
#include "fir_benchmark.h"
#else
#include "sampling_profiler.h"
#include "profiler_utimer_config.h"

/* Observable work plus noinline preserve distinct symbols in optimized builds. */
volatile uint32_t kda_light_steps;
volatile uint32_t kda_heavy_steps;
volatile uint32_t kda_workload_iterations;

__attribute__((noinline)) static void workload_light(void)
{
    for (uint32_t i = 0; i < 2000U; ++i) {
        ++kda_light_steps;
    }
}

__attribute__((noinline)) static void workload_heavy(void)
{
    for (uint32_t i = 0; i < 6000U; ++i) {
        ++kda_heavy_steps;
    }
}
#endif

int main(void)
{
    if (stdout_init() == 0) {
        printf("Hello World!\r\n");
        fflush(stdout);
    }

#if KDA_APP_FIR
#if KDA_APP_FIR == 5
    kda_fir_target_run();
    if (kda_fir_result.phase == 3 && kda_fir_result.failures == 0) {
        kda_fir_benchmark_run();
    }
#elif KDA_APP_FIR >= 2
    kda_fir_guard_run();
#else
    kda_fir_target_run();
#endif
    for (;;) { __WFI(); }
#else
    /* HP alone owns UTIMER channel 0; HE does not access UTIMER. Do not
     * reuse this shared clock update if HE gains a timer without first
     * adding serialized clock setup and an inter-core readiness handshake.
     */
    UTIMER->UTIMER_GLB_CLOCK_ENABLE |= (1UL << PROFILER_ALIF_UTIMER_CHANNEL);
    __DSB();
    /* CMSIS Load and debugger reset can each start this image. A core reset
     * does not necessarily reset UTIMER. Reclaim only our reserved channel
     * before the adapter checks that it is unused; never reset the shared block.
     */
    NVIC_DisableIRQ(PROFILER_ALIF_TIMER_IRQ);
    UTIMER->UTIMER_GLB_CNTR_STOP = (1UL << PROFILER_ALIF_UTIMER_CHANNEL);
    UTIMER->UTIMER_GLB_CNTR_CLEAR = (1UL << PROFILER_ALIF_UTIMER_CHANNEL);
    UTIMER->UTIMER_CHANNEL_CFG[PROFILER_ALIF_UTIMER_CHANNEL].UTIMER_CNTR_CTRL = 0U;
    __DSB();
    NVIC_ClearPendingIRQ(PROFILER_ALIF_TIMER_IRQ);
    NVIC_ClearTargetState(PROFILER_ALIF_TIMER_IRQ);
    int capturing = sampling_profiler_init();
    const uint32_t capture_start = DWT->CYCCNT;
    if (capturing) {
        sampling_profiler_enable();
    }

    for (;;) {
        workload_light();
        workload_heavy();
        ++kda_workload_iterations;
        /* Finalize once, but continue running the workload indefinitely.
         * At the fixed 400 MHz CPU clock, two seconds fits in uint32_t.
         * The timeout also ends captures with no accepted timer samples.
         */
        if (capturing && (sampling_profiler_full() ||
            (uint32_t)(DWT->CYCCNT - capture_start) >= 2U * SystemCoreClock)) {
            sampling_profiler_stop(kda_workload_iterations,
                kda_light_steps == 2000U * kda_workload_iterations &&
                kda_heavy_steps == 6000U * kda_workload_iterations);
            capturing = 0;
        }
    }
#endif
}

#else
/* Keep the original host executable available for the CMake tests. */
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc > 2) {
        fputs("Usage: hello [name]\n", stderr);
        return EXIT_FAILURE;
    }

    printf("Hello, %s!\n", argc == 2 ? argv[1] : "embedded");
    return 0;
}
#endif
