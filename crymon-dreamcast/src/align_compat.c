/*
 * Compatibility shim for a stock (non-dc-chain-patched) sh-elf-gcc.
 *
 * KOS's kernel calls __is_aligned()/__align_up()/__builtin_is_aligned()
 * as if they were compiler-provided alignment builtins (this is how
 * Clang's real __builtin_is_aligned/__builtin_align_up behave -- generic
 * over both integers and pointers). This GCC (13.2, unpatched) doesn't
 * implement them: it emits a warning ("built-in function ... was assumed
 * to be a runtime function") and generates a plain external call instead,
 * which is why the KOS build only fails at link time, not compile time.
 * dc-chain's own patched toolchain carries a newlib-side implementation
 * of these for exactly this reason (see KOS's own
 * "Outdated toolchain: not patched..." build warning).
 *
 * On SH4's calling convention, every argument these are called with
 * across KOS's source (plain integers and pointers alike) is a single
 * 32-bit value passed in one integer register, so one uint32_t-based
 * definition here is ABI-correct for every call site regardless of
 * whether the caller passed an integer or a pointer.
 */

#include <stdint.h>

int __is_aligned(uint32_t value, uint32_t align) {
    return (value & (align - 1)) == 0;
}

uint32_t __align_up(uint32_t value, uint32_t align) {
    return (value + (align - 1)) & ~(align - 1);
}

int __builtin_is_aligned(uint32_t value, uint32_t align) {
    return (value & (align - 1)) == 0;
}
