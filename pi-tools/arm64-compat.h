// ARM64 GCC cross-compilation compatibility header
// Provides __wfe() intrinsic that Clang has but GCC cross-compiler lacks

#ifndef ARM64_COMPAT_H
#define ARM64_COMPAT_H

#if defined(__aarch64__) && defined(__GNUC__) && !defined(__clang__)
// GCC cross-compiler doesn't provide __wfe() built-in like Clang does
// Define it using inline assembly
__attribute__((always_inline)) static inline void __wfe(void) {
    __asm__ volatile("wfe" ::: "memory");
}
#endif

#endif // ARM64_COMPAT_H
