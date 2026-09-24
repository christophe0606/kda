#include "fir_benchmark.h"
/* Public declarations only; compile the comparator opaquely. */
#include "dsp/filtering_functions.h"
__attribute__((noinline))
void fir_batch_baseline(uint32_t count, const void *instance,
                        const float *src, float *dst, uint32_t block)
{
    for (uint32_t i = 0; i < count; ++i) {
        arm_fir_f32((const arm_fir_instance_f32 *)instance, src, dst, block);
    }
}
