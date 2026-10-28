/* Compile-only ABI measurement. Never link this object into the executable.
 * Include the unchanged implementation to measure its private structures.
 */
#include <limits.h>
#include "../ms2x.c"

#if !defined(__mips__) || !defined(__MIPSEL__) || !defined(__mips_soft_float)
#error "Omega2 requires little-endian MIPS with soft-float"
#endif
#if !defined(_MIPS_SIM) || !defined(_ABIO32) || _MIPS_SIM != _ABIO32
#error "Omega2 requires the MIPS o32 ABI"
#endif
_Static_assert(CHAR_BIT == 8, "8-bit bytes required");
_Static_assert(sizeof(void *) == 4U, "32-bit pointers required");
_Static_assert(sizeof(size_t) == 4U, "32-bit size_t required");
_Static_assert(sizeof(int) == 4U && sizeof(long) == 4U, "ILP32 required");
_Static_assert(sizeof(uint64_t) == 8U, "64-bit logical time required");
_Static_assert(offsetof(ms2x_frame, data) == 5U, "check payload layout");

/* Read with objcopy --dump-section .ms2x_abi=...; 11 little-endian uint32s. */
const uint32_t ms2x_omega2_abi[11] __attribute__((section(".ms2x_abi"))) = {
    (uint32_t)sizeof(void *), (uint32_t)sizeof(size_t),
    (uint32_t)sizeof(ms2x_frame), (uint32_t)_Alignof(ms2x_frame),
    (uint32_t)sizeof(ms2x_tx), (uint32_t)_Alignof(ms2x_tx),
    (uint32_t)sizeof(ms2x_engine), (uint32_t)_Alignof(ms2x_engine),
    (uint32_t)sizeof(ms2x_firmware), (uint32_t)sizeof(uint64_t),
    (uint32_t)offsetof(ms2x_tx, sequence)
};