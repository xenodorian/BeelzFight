/*
 * docker/gcc9-compat.h
 *
 * Compatibility shim for building modern KallistiOS (KOS) with the prebuilt
 * sh-elf GCC 9.3.0 / arm-eabi GCC 8.4.0 cross-toolchain used by
 * docker/Dockerfile.romdev (see the long comment at the top of that file for
 * why an older prebuilt toolchain is used instead of building one fresh).
 *
 * Modern KOS (kernel/arch/dreamcast/include/arch/arch.h and several kernel
 * source files) calls a handful of identifiers -- __align_up(),
 * __is_aligned(), __builtin_is_aligned(), __builtin_set_thread_pointer() --
 * that are NOT provided by any KOS or newlib header. They are meant to be
 * supplied directly by the compiler: KallistiOS's own toolchain builder
 * (utils/kos-chain) applies patches to GCC that add exactly these as real
 * compiler builtins (see __KOS_GCC_PATCHLEVEL__ in arch.h, which gates
 * related functionality on having a KOS-patched GCC of a given vintage).
 * The 2020-era prebuilt toolchain used here predates those patches, so the
 * identifiers are simply undeclared -- which only warns
 * (-Wimplicit-function-declaration) at compile time but fails at link time
 * (undefined reference), since no such symbols exist anywhere.
 *
 * This header defines portable, standard-C equivalents of all four so that
 * an unpatched (or older-patched) GCC can still build modern KOS correctly.
 * It is force-included into every compilation (KOS_CFLAGS += -include
 * .../gcc9-compat.h in environ.sh) rather than patched into KOS's own
 * headers, so upgrading KOS or rebuilding this image against a real
 * KOS-patched from-source toolchain (the preferred path -- see
 * Dockerfile.romdev) needs no changes here: this file simply stops being
 * `-include`d.
 */
#ifndef BEELZFIGHT_GCC9_COMPAT_H
#define BEELZFIGHT_GCC9_COMPAT_H

/* This header is force-included (-include) into every KOS compile, which
   also means every assembled .S file (preprocessed with cpp, then handed
   to the assembler as-is). None of the C content below is valid assembly,
   so skip it entirely in that context. */
#ifndef __ASSEMBLER__

#include <stdint.h>

#if !defined(__align_up)
#define __align_up(x, a) \
    ((__typeof__(x))(((uintptr_t)(x) + ((uintptr_t)(a) - 1)) & ~((uintptr_t)(a) - 1)))
#endif

#if !defined(__is_aligned)
#define __is_aligned(x, a) \
    ((((uintptr_t)(x)) & ((uintptr_t)(a) - 1)) == 0)
#endif

#if !defined(__builtin_is_aligned)
#define __builtin_is_aligned(x, a) __is_aligned(x, a)
#endif

#if !defined(__builtin_set_thread_pointer)
/* KOS uses this on SH4 to set the GBR register (its thread/TLS pointer) --
   see kernel/arch/dreamcast/kernel/tls_static.c. `ldc Rm, GBR` is the SH4
   instruction that loads GBR from a general register. */
#if defined(__sh__)
static __inline__ void __builtin_set_thread_pointer(void *ptr) {
    __asm__ __volatile__("ldc %0, gbr" : : "r"(ptr));
}
#endif
#endif

#endif /* !__ASSEMBLER__ */

#endif /* BEELZFIGHT_GCC9_COMPAT_H */
