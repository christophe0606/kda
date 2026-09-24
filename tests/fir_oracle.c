#include "fir_oracle.h"

#include <math.h>

#if defined(__FAST_MATH__)
#error "The FIR oracle must use strict floating-point semantics"
#endif

/* Direct chronological convolution: independent of candidate storage/indexing. */
double fir_reference(const float *public_coefficients, size_t taps,
                     const float *stream, size_t sample, double *absolute_sum)
{
    double sum = 0.0;
    double magnitude = 0.0;
    for (size_t delay = 0; delay < taps && delay <= sample; ++delay) {
        const double product = (double)public_coefficients[taps - 1U - delay] *
                               (double)stream[sample - delay];
        sum += product;
        magnitude += fabs(product);
    }
    *absolute_sum = magnitude;
    return sum;
}

int fir_close(float actual, double reference, double absolute_sum, size_t taps)
{
    const double scaled_epsilon = 2.0 * (double)taps * 0x1p-24;
    const double gamma = scaled_epsilon / (1.0 - scaled_epsilon);
    return isfinite(actual) && isfinite(reference) &&
           fabs((double)actual - reference) <= 1e-7 + gamma * absolute_sum;
}
