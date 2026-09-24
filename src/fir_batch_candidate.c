#include "fir_benchmark.h"
#include "kda_fir_f32.h"
__attribute__((noinline))
void fir_batch_candidate(uint32_t count, const void *instance,
                         const float *src, float *dst, uint32_t block)
{
    for (uint32_t i = 0; i < count; ++i) {
        kda_fir_f32((const kda_fir_instance_f32 *)instance, src, dst, block);
    }
}
