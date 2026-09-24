Optimize a FIR filter with f32 datatype working per block of samples.
It should use Helium instruction set.
For maximum performance with Helium, project must be compiled with -O3 -ffast-math
The asymptotic performance should come close to 0.621 * blocksize * nbtaps.
The asymptotic performance should start to be visible when blocksize is 128 or more and nbtaps is 16 or more.
The filter should be competitive with the CMSIS-DSP version : as fast or faster.
It should also works for small blocksize like 1,2,3,4 and small number of taps like 1,2,3,4.
It is acceptable to have a specific implementation for small filters (small number of taps).
The API should be the same as arm_fir_f32

