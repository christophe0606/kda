/* Shared harness assembly only. The processing functions remain opaque calls.
 * count is a multiple of 128. Save eight words to preserve ABI stack alignment.
 * The fifth argument is read at the caller SP before the saved-register frame. */
#define FIR_CALL_ONCE "mov r0, r5\nmov r1, r6\nmov r2, r7\nmov r3, r8\nbl " FIR_CALLEE "\n"
#define FIR_CALL_2 FIR_CALL_ONCE FIR_CALL_ONCE
#define FIR_CALL_4 FIR_CALL_2 FIR_CALL_2
#define FIR_CALL_8 FIR_CALL_4 FIR_CALL_4
#define FIR_CALL_16 FIR_CALL_8 FIR_CALL_8
#define FIR_CALL_32 FIR_CALL_16 FIR_CALL_16
#define FIR_CALL_64 FIR_CALL_32 FIR_CALL_32
#define FIR_CALL_128 FIR_CALL_64 FIR_CALL_64
#define FIR_BATCH_ASM(BODY) \
    "push {r4-r9, r11, lr}\nmov r5, r1\nmov r6, r2\nmov r7, r3\n" \
    "ldr r8, [sp, #32]\nlsrs r4, r0, #7\nbeq.w 2f\n1:\n" \
    BODY "subs r4, #1\nbne.w 1b\n2: pop {r4-r9, r11, pc}\n"
