/**
 * @file macros.h
 * @brief Cross-compiler macros and utilities for the Ghoti.io Text library
 *
 * This header provides cross-compiler macros for common functionality
 * such as marking unused parameters, deprecated functions, and utility
 * macros for array operations.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_TEXT_MACROS_H
#define GHOTI_IO_TEXT_MACROS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Typedef prototypes for forward declarations
 *
 * These forward declarations allow headers to reference types before
 * their full definitions are available.
 */
typedef struct GTEXT_JSON_Value GTEXT_JSON_Value;

/**
 * @brief Cross-compiler macro for marking a function parameter as unused
 *
 * Use this macro to suppress compiler warnings about unused parameters.
 * Works with GCC, Clang, and MSVC.
 *
 * @param X The parameter name to mark as unused
 *
 * Example:
 * @code
 * void my_function(int GTEXT_MAYBE_UNUSED(param)) {
 *     // param is intentionally unused
 * }
 * @endcode
 */
#if defined(__GNUC__) || defined(__clang__)
#define GTEXT_MAYBE_UNUSED(X) __attribute__((unused)) X

#elif defined(_MSC_VER)
#define GTEXT_MAYBE_UNUSED(X) (void)(X)

#else
#define GTEXT_MAYBE_UNUSED(X) X

#endif

/**
 * @brief Cross-compiler macro for marking a function as deprecated
 *
 * Use this macro to mark functions that are deprecated and will be
 * removed in a future version. Works with GCC, Clang, and MSVC.
 *
 * Example:
 * @code
 * GTEXT_DEPRECATED
 * void old_function(void);
 * @endcode
 */
#if defined(__GNUC__) || defined(__clang__)
#define GTEXT_DEPRECATED __attribute__((deprecated))

#elif defined(_MSC_VER)
#define GTEXT_DEPRECATED __declspec(deprecated)

#else
#define GTEXT_DEPRECATED

#endif

/**
 * @brief API export macro for cross-platform library symbols
 *
 * Use this macro to mark functions that should be exported from the
 * shared library. Automatically handles Windows DLL export/import
 * and Unix symbol visibility.
 *
 * Example:
 * @code
 * GTEXT_API void public_function(void);
 * @endcode
 */
#ifdef __cplusplus
#define GTEXT_EXTERN extern "C"
#else
#define GTEXT_EXTERN
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef GTEXT_BUILD
#define GTEXT_API GTEXT_EXTERN __declspec(dllexport)
#else
#define GTEXT_API GTEXT_EXTERN __declspec(dllimport)
#endif
#else
#define GTEXT_API GTEXT_EXTERN __attribute__((visibility("default")))
#endif

/**
 * @brief Internal API export macro for testing
 *
 * This macro is used to export internal functions that are needed for testing
 * but should not be part of the public API. These functions are only exported
 * when TEXT_TEST_BUILD is defined during library compilation.
 *
 * Example:
 * @code
 * GTEXT_INTERNAL_API void internal_function(void);
 * @endcode
 */
#ifdef GTEXT_TEST_BUILD
#define GTEXT_INTERNAL_API GTEXT_API
#else
#define GTEXT_INTERNAL_API
#endif

/**
 * @brief Compile-time array size helper
 *
 * Calculates the number of elements in a statically-allocated array.
 * This is a compile-time constant and safe to use in constant expressions.
 *
 * @param a The array (not a pointer)
 * @return The number of elements in the array
 *
 * Example:
 * @code
 * int arr[10];
 * size_t count = GTEXT_ARRAY_SIZE(arr);  // count = 10
 * @endcode
 */
#ifndef GTEXT_ARRAY_SIZE
#define GTEXT_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/**
 * @brief Bit manipulation macro
 *
 * Creates a bitmask with a single bit set at the specified position.
 *
 * @param x The bit position (0-based)
 * @return A bitmask with bit x set
 *
 * Example:
 * @code
 * unsigned int flags = GTEXT_BIT(3);  // flags = 0x08
 * @endcode
 */
#ifndef GTEXT_BIT
#define GTEXT_BIT(x) (1u << (x))
#endif

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_MACROS_H */
