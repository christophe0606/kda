#include "fir_benchmark.h"
#include "kda_fir_f32.h"
#define FIR_CALLEE "kda_fir_f32"
#include "fir_batch_body.h"
__attribute__((naked, noinline))
void fir_batch_candidate(uint32_t count, const void *instance,
                         const float *src, float *dst, uint32_t block)
{
    __asm volatile(FIR_BATCH_ASM(FIR_CALL_128));
}
