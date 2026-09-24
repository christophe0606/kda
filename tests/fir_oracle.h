#ifndef FIR_ORACLE_H
#define FIR_ORACLE_H

#include <stddef.h>

double fir_reference(const float *public_coefficients, size_t taps,
                     const float *stream, size_t sample, double *absolute_sum);
int fir_close(float actual, double reference, double absolute_sum, size_t taps);

#endif
