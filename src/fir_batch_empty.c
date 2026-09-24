#include "fir_benchmark.h"
/* ABI-matched empty call, kept out of line; endpoint and repeated-loop
 * diagnostics are reported separately from inclusive FIR cycles. */
__attribute__((noinline, used)) static void empty_call(const void *instance,
                                                const float *src, float *dst,
                                                uint32_t block)
{
    __asm volatile("" : : "r"(instance), "r"(src), "r"(dst), "r"(block) : "memory");
}
#define FIR_CALLEE "empty_call"
#include "fir_batch_body.h"
__attribute__((naked, noinline))
void fir_batch_empty(uint32_t count, const void *instance,
                     const float *src, float *dst, uint32_t block)
{
    __asm volatile(FIR_BATCH_ASM(FIR_CALL_128));
}

/* Same stack frame, PMU endpoints and outer loop, with public calls omitted. */
__attribute__((naked, noinline))
void fir_batch_control(uint32_t count, const void *instance,
                       const float *src, float *dst, uint32_t block)
{
    __asm volatile(FIR_BATCH_ASM(""));
}
