#include "fir_guard.h"
#include "kda_fir_f32.h"
#include "fir_oracle.h"
#include "fir_vectors.h"
#include "RTE_Components.h"
#include CMSIS_device_header
#include "app_mem_regions.h"
#include <arm_mve.h>
#include <stdio.h>
#include <string.h>

#if KDA_APP_FIR >= 2
#if defined(__FAST_MATH__)
#error "Guard verification must use strict math"
#endif

#define DTCM __attribute__((section(".bss.dtcm.fir"), aligned(32)))
#define STORAGE_FLOATS 520U
#define ARENA_FLOATS 2048U
#define GUARD_FLOAT 1024U
#define SENTINEL 12345.5f

volatile fir_guard_result kda_fir_guard_result DTCM;
static float arena[ARENA_FLOATS] DTCM;
static float buffers[5][STORAGE_FLOATS] DTCM;
static float stream[8U * 512U] DTCM;
static volatile float32x4_t negative_sink DTCM;
static struct {
    uint32_t ctrl, shcsr, mair[2], rnr, count, primask, ccr;
    ARM_MPU_Region_t regions[16];
} saved DTCM;

/* In these isolated images faults stop permanently. HFNMIENA remains zero,
 * so the fault record is accessible even after a protected access fails. */
__attribute__((noreturn, noinline, used))
void kda_fir_guard_fault(const uint32_t *frame, uint32_t exc_return)
{
    kda_fir_guard_result.cfsr = SCB->CFSR;
    kda_fir_guard_result.hfsr = SCB->HFSR;
    kda_fir_guard_result.mmfar = SCB->MMFAR;
    kda_fir_guard_result.bfar = SCB->BFAR;
    kda_fir_guard_result.exception = __get_IPSR();
    kda_fir_guard_result.exc_return = exc_return;
    kda_fir_guard_result.frame_address = (uint32_t)(uintptr_t)frame;
    /* Same-security exception: retain basic stacked R0-R3,R12,LR,PC,xPSR.
     * Do not dereference a frame outside the configured DTCM stack memory. */
    if ((uintptr_t)frame >= APP_DTCM_BASE &&
        (uintptr_t)frame <= APP_DTCM_BASE + APP_HP_DTCM_SIZE - 8U * sizeof(uint32_t)) {
        for (unsigned i = 0; i < 8; ++i) { kda_fir_guard_result.frame[i] = frame[i]; }
    }
    kda_fir_guard_result.phase = 3;
    __DSB();
    for (;;) { __NOP(); }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "mov r1, lr\n"
                   "b kda_fir_guard_fault\n");
}
__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile("b HardFault_Handler\n");
}

static void restore_mpu(void)
{
    ARM_MPU_Disable();
    for (uint32_t i = 0; i < saved.count; ++i) {
        ARM_MPU_SetRegion(i, saved.regions[i].RBAR, saved.regions[i].RLAR);
    }
    MPU->MAIR[0] = saved.mair[0];
    MPU->MAIR[1] = saved.mair[1];
    MPU->RNR = saved.rnr;
    MPU->CTRL = saved.ctrl;
    SCB->SHCSR = saved.shcsr;
    __DSB();
    __ISB();
}

static void set_region(uint32_t index, uint32_t base, uint32_t last, uint32_t code)
{
    ARM_MPU_SetRegion(index,
        ARM_MPU_RBAR(base, ARM_MPU_SH_NON, code, ARM_MPU_AP_NP, !code),
        ARM_MPU_RLAR(last, 0));
}

static void protect_gap(void)
{
    const uint32_t gap = (uint32_t)(uintptr_t)&arena[GUARD_FLOAT];
    ARM_MPU_Disable();
    for (uint32_t i = 0; i < saved.count; ++i) { ARM_MPU_ClrRegion(i); }
    ARM_MPU_SetMemAttr(0, ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE,
                                      ARM_MPU_ATTR_NON_CACHEABLE));
    set_region(0, APP_DTCM_BASE, gap - 1U, 0);
    set_region(1, gap + 32U, APP_DTCM_BASE + APP_HP_DTCM_SIZE - 1U, 0);
    set_region(2, APP_MRAM_HP_BASE, APP_MRAM_HP_BASE + APP_CODE_MRAM_SIZE - 1U, 1);
    set_region(3, APP_ITCM_BASE, APP_ITCM_BASE + APP_HP_ITCM_SIZE - 1U, 1);
#if SRAM0_SRAM1_COMBINED == 1
    set_region(4, APP_SRAM_BASE, APP_SRAM_BASE + APP_SRAM_SIZE - 1U, 0);
#else
    set_region(4, APP_SRAM0_BASE, APP_SRAM0_BASE + APP_SRAM0_SIZE - 1U, 0);
    set_region(5, APP_SRAM1_BASE, APP_SRAM1_BASE + APP_SRAM1_SIZE - 1U, 0);
#endif
    /* No default privileged mapping: the omitted 32 bytes deny reads AND writes. */
    ARM_MPU_Enable(0);
    /* Deliberately use an escalated HardFault so recording runs with MPU bypass. */
    SCB->SHCSR &= ~SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB();
    __ISB();
    kda_fir_guard_result.mpu_ctrl = MPU->CTRL;
    kda_fir_guard_result.mair0 = MPU->MAIR[0];
    kda_fir_guard_result.primask = __get_PRIMASK();
    for (uint32_t i = 0; i < saved.count; ++i) {
        MPU->RNR = i;
        kda_fir_guard_result.regions[i][0] = MPU->RBAR;
        kda_fir_guard_result.regions[i][1] = MPU->RLAR;
    }
}

static int canaries_ok(float *pointers[5], const size_t sizes[5], unsigned selected)
{
    for (unsigned b = 0; b < 5; ++b) {
        float *base = b == selected ? arena : buffers[b];
        const size_t capacity = b == selected ? ARENA_FLOATS : STORAGE_FLOATS;
        const size_t first = (size_t)(pointers[b] - base);
        for (size_t j = 0; j < capacity; ++j) {
            if ((j < first || j >= first + sizes[b]) && base[j] != SENTINEL) {
                return 0;
            }
        }
    }
    return 1;
}

static int guard_case(uint32_t block, uint16_t n, unsigned selected, unsigned offset)
{
    /* 0=source, 1=destination, 2=public coefficients, 3=prepared, 4=history. */
    const size_t sizes[5] = {block, block, n, n, 2U * n};
    float *p[5];
    kda_fir_state_f32 state;
    kda_fir_instance_f32 instance;
    uint32_t seed = UINT32_C(0x95e1a123) ^ n ^ (block << 16) ^ 5U;
    for (size_t i = 0; i < ARENA_FLOATS; ++i) { arena[i] = SENTINEL; }
    for (unsigned b = 0; b < 5; ++b) {
        for (size_t i = 0; i < STORAGE_FLOATS; ++i) { buffers[b][i] = SENTINEL; }
        p[b] = b == selected ? &arena[GUARD_FLOAT - sizes[b]] : buffers[b] + 4U + offset;
    }
    for (size_t i = 0; i < n; ++i) {
        p[2][i] = (float)((int)((i * 17U + 5U) % 31U) - 15) / 32.0f;
    }
    for (size_t i = 0; i < 8U * block; ++i) { stream[i] = input_sample(5, i, &seed); }
    kda_fir_guard_result.block = block;
    kda_fir_guard_result.taps = n;
    kda_fir_guard_result.buffer = selected;
    kda_fir_guard_result.alignment = offset * 4U;
    kda_fir_guard_result.buffer_begin = (uint32_t)(uintptr_t)p[selected];
    kda_fir_guard_result.buffer_bytes = (uint32_t)(sizes[selected] * sizeof(float));
    kda_fir_guard_result.stage = 1;
    protect_gap();
    const int initialized = kda_fir_init_f32(&instance, &state, n, p[2], n,
                                            p[3], n, p[4], 2U * n);
    restore_mpu();
    if (!initialized) { return 0; }
    for (size_t part = 0; part < 8; ++part) {
        memcpy(p[0], stream + part * block, block * sizeof(float));
        for (size_t i = 0; i < block; ++i) { p[1][i] = SENTINEL; }
        kda_fir_guard_result.stage = 2;
        protect_gap();
        kda_fir_f32(&instance, p[0], p[1], block);
        restore_mpu();
        for (size_t i = 0; i < block; ++i) {
            double q;
            const double expected = fir_reference(p[2], n, stream, part * block + i, &q);
            if (!fir_close(p[1][i], expected, q, n) || p[0][i] != stream[part * block + i]) {
                return 0;
            }
        }
    }
    kda_fir_guard_result.stage = 3;
    protect_gap();
    kda_fir_reset_f32(&instance);
    restore_mpu();
    if (state.next != 0) { return 0; }
    for (size_t i = 0; i < 2U * n; ++i) { if (p[4][i] != 0) { return 0; } }
    for (size_t i = 0; i < n; ++i) {
        const float expected = (float)((int)((i * 17U + 5U) % 31U) - 15) / 32.0f;
        if (p[2][i] != expected) { return 0; }
    }
    return canaries_ok(p, sizes, selected);
}

__attribute__((noinline)) static void negative_read(const float *last_valid)
{
    /* Volatile destination retains every lane of the unpredicated 16-byte load. */
    negative_sink = vldrwq_f32(last_valid);
}

void kda_fir_guard_run(void)
{
    kda_fir_guard_result = (fir_guard_result){0};
    kda_fir_guard_result.magic = UINT32_C(0x47524432);
    kda_fir_guard_result.mode = KDA_APP_FIR;
    kda_fir_guard_result.phase = 1;
    kda_fir_guard_result.guard_begin = (uint32_t)(uintptr_t)&arena[GUARD_FLOAT];
    kda_fir_guard_result.guard_end = kda_fir_guard_result.guard_begin + 32U;
    kda_fir_guard_result.mpu_type = MPU->TYPE;
    saved.count = ARM_MPU_TYPE();
    kda_fir_guard_result.region_count = saved.count;
    kda_fir_guard_result.control = __get_CONTROL();
    kda_fir_guard_result.msp = __get_MSP();
    kda_fir_guard_result.psp = __get_PSP();
    saved.ccr = SCB->CCR;
    kda_fir_guard_result.ccr = saved.ccr;
    if (saved.count < 6 || saved.count > 16 || (__get_CONTROL() & 1U) || __get_IPSR() ||
        (kda_fir_guard_result.guard_begin & 31U) ||
        kda_fir_guard_result.guard_begin <= APP_DTCM_BASE ||
        kda_fir_guard_result.guard_end >= APP_DTCM_BASE + APP_HP_DTCM_SIZE) {
        kda_fir_guard_result.phase = 4;
        ++kda_fir_guard_result.failures;
        return;
    }
    saved.primask = __get_PRIMASK();
    __disable_irq();
    saved.ctrl = MPU->CTRL;
    saved.shcsr = SCB->SHCSR;
    saved.mair[0] = MPU->MAIR[0];
    saved.mair[1] = MPU->MAIR[1];
    saved.rnr = MPU->RNR;
    for (uint32_t i = 0; i < saved.count; ++i) {
        MPU->RNR = i;
        saved.regions[i].RBAR = MPU->RBAR;
        saved.regions[i].RLAR = MPU->RLAR;
    }
    SCB_DisableDCache();
    SCB_DisableICache();
    SCB->CFSR = SCB->CFSR;
    SCB->HFSR = SCB->HFSR;
#if KDA_APP_FIR == 3 || KDA_APP_FIR == 4
    arena[GUARD_FLOAT - 1U] = 1.0f;
    kda_fir_guard_result.stage = 4;
    protect_gap();
#if KDA_APP_FIR == 3
    negative_read(&arena[GUARD_FLOAT - 1U]);
#else
    *(volatile float *)&arena[GUARD_FLOAT] = 2.0f;
#endif
    __DSB();
    kda_fir_guard_result.unexpected = 1;
    ++kda_fir_guard_result.failures;
    restore_mpu();
#else
    for (size_t b = 0; b < sizeof blocks / sizeof blocks[0]; ++b) {
        for (size_t n = 0; n < sizeof taps / sizeof taps[0]; ++n) {
            for (unsigned buffer = 0; buffer < 5; ++buffer) {
                for (unsigned offset = 0; offset < 4; ++offset) {
                    if (!guard_case(blocks[b], taps[n], buffer, offset)) {
                        ++kda_fir_guard_result.failures;
                        goto finished;
                    }
                    ++kda_fir_guard_result.cases_completed;
                }
            }
        }
    }
finished:
#endif
    if (saved.ccr & SCB_CCR_IC_Msk) { SCB_EnableICache(); }
    if (saved.ccr & SCB_CCR_DC_Msk) { SCB_EnableDCache(); }
    __set_PRIMASK(saved.primask);
    kda_fir_guard_result.phase = 2;
    printf("FIR guard: mode=%lu cases=%lu failures=%lu\r\n",
           (unsigned long)kda_fir_guard_result.mode,
           (unsigned long)kda_fir_guard_result.cases_completed,
           (unsigned long)kda_fir_guard_result.failures);
    fflush(stdout);
}
#endif
