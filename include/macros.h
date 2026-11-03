#ifndef MACROS_H
#define MACROS_H

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define CLAMP(low, high, x)                                                    \
    ((x) > (high) ? (high) : ((x) < (low) ? (low) : (x)))

#define ROUND_UP(x, align) (((x) + (align)-1) & (-(align)))
#define ROUND_UP_PTR(x, align)                                                 \
    ((void*)((((u32)(x)) + (align)-1) & (~((align)-1))))

#define ROUND_DOWN(x, align) ((x) & (-(align)))
#define ROUND_DOWN_PTR(x, align) ((void*)(((u32)(x)) & (~((align)-1))))

#define ARRAY_LENGTH(x) (sizeof((x)) / sizeof((x)[0]))

#define CLEAR_PATH(x) __memclr((x), sizeof((x)))

#define ALIGN(x) __attribute__((aligned(x)))
#define DECL_SECTION(x) __declspec(section x)
#define DECL_WEAK __declspec(weak)
#define DONT_INLINE __attribute__((never_inline))

// Codewarrior tricks for matching decomp
// (Functions are given prototypes for -requireprotos)
#ifdef __MWERKS__
// Force BSS order
#define CW_FORCE_BSS(module, ...)                                              \
    void fake_function(...);                                                   \
    void FORCE_BSS##module##x(void);                                           \
    void FORCE_BSS##module##x(void) { fake_function(__VA_ARGS__); }
// Force strings into pool
#define CW_FORCE_STRINGS(module, ...)                                          \
    void fake_function(...);                                                   \
    void FORCE_STRINGS##module(void);                                          \
    void FORCE_STRINGS##module(void) { fake_function(__VA_ARGS__); }
#else
#define CW_FORCE_BSS(module, ...)
#define CW_FORCE_STRINGS(module, ...)
#endif

// For VSCode
#ifdef __INTELLISENSE__
#define asm
#define __attribute__(x)
#define __declspec(x)
#endif

#endif

/**
 * Codewarrior tricks for matching decomp
 * (Macros generate prototypes to satisfy -requireprotos)
 */

// Compile without matching hacks.
#if defined(NONMATCHING) || defined(COMPAT_ANY)
#define DECOMP_FORCEACTIVE(module, ...)
#define DECOMP_FORCELITERAL(module, ...)
#define DECOMP_FORCEACTIVE_DTOR(module, cls)
#define DECOMP_INLINE
#define DECOMP_DONT_INLINE
// Compile with matching hacks.
// (This version of CW does not support pragmas inside macros.)
#else
// Force reference specific data
#define DECOMP_FORCEACTIVE(module, ...)                                        \
    void fake_function(...);                                                   \
    void CONCAT(FORCEACTIVE##module, __LINE__)(void);                          \
    void CONCAT(FORCEACTIVE##module, __LINE__)(void) {                         \
        fake_function(__VA_ARGS__);                                            \
    }

// Force literal ordering, such as floats in sdata2
#define DECOMP_FORCELITERAL(module, ...)                                       \
    void CONCAT(FORCELITERAL##module, __LINE__)(void);                         \
    void CONCAT(FORCELITERAL##module, __LINE__)(void) {                        \
        (__VA_ARGS__);                                                         \
    }

// Force reference destructor
#define DECOMP_FORCEACTIVE_DTOR(module, cls)                                   \
    void CONCAT(FORCEDTOR##module##cls, __LINE__)(void);                       \
    void CONCAT(FORCEDTOR##module##cls, __LINE__)(void) {                      \
        cls dummy;                                                             \
        dummy.~cls();                                                          \
    }

#define DECOMP_INLINE inline
#define DECOMP_DONT_INLINE __attribute__((never_inline))

#endif
