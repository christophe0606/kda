#include "fir_benchmark.h"
/* ABI-matched empty call, kept out of line; endpoint and repeated-loop
 * diagnostics are reported separately from inclusive FIR cycles. */
__attribute__((noinline)) static void empty_call(const void *instance,
                                                const float *src, float *dst,
                                                uint32_t block)
{
    __asm volatile("" : : "r"(instance), "r"(src), "r"(dst), "r"(block) : "memory");
}
__attribute__((noinline))
void fir_batch_empty(uint32_t count, const void *instance,
                     const float *src, float *dst, uint32_t block)
{
    for (uint32_t i = 0; i < count; ++i) { empty_call(instance, src, dst, block); }
}
