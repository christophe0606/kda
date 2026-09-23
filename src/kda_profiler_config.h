#ifndef KDA_PROFILER_CONFIG_H
#define KDA_PROFILER_CONFIG_H

/* Ensemble 2.2.1 DevKit-E8 default UTIMER input is 400 MHz (the board's
 * 250 ms timer example uses 100,000,000 counts). Recheck after clock changes;
 * this is a timer clock, not an alias for SystemCoreClock.
 */
#define PROFILER_TIMER_CLOCK_HZ 400000000U
#define PROFILER_ALIF_UTIMER_CHANNEL 0
#define PROFILER_PMU_COUNT 0
/* The board scatter file routes .*dtcm* sections to CPU-local DTCM. */
#define PROFILER_BUFFER_ATTRIBUTES __attribute__((section(".bss.dtcm.profiler"), aligned(32)))

/* The external common layer supplies 1000 Hz and 65536 bytes. Override here
 * with #undef/#define if needed, without modifying the shared dependency.
 */

#endif
