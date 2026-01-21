/**
 * @file macros.h
 * @brief Cross-compiler macros and utilities for the Ghoti.io Text library
 *
 * This header provides cross-compiler macros for common functionality
 * such as marking unused parameters, deprecated functions, and utility
 * macros for array operations.
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
typedef struct text_json_value text_json_value;

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
 * void my_function(int TEXT_MAYBE_UNUSED(param)) {
 *     // param is intentionally unused
 * }
 * @endcode
 */
#if defined(__GNUC__) || defined(__clang__)
#define TEXT_MAYBE_UNUSED(X) __attribute__((unused)) X

#elif defined(_MSC_VER)
#define TEXT_MAYBE_UNUSED(X) (void)(X)

#else
#define TEXT_MAYBE_UNUSED(X) X

#endif

/**
 * @brief Cross-compiler macro for marking a function as deprecated
 *
 * Use this macro to mark functions that are deprecated and will be
 * removed in a future version. Works with GCC, Clang, and MSVC.
 *
 * Example:
 * @code
 * TEXT_DEPRECATED
 * void old_function(void);
 * @endcode
 */
#if defined(__GNUC__) || defined(__clang__)
#define TEXT_DEPRECATED __attribute__((deprecated))

#elif defined(_MSC_VER)
#define TEXT_DEPRECATED __declspec(deprecated)

#else
#define TEXT_DEPRECATED

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
 * TEXT_API void public_function(void);
 * @endcode
 */
#ifdef __cplusplus
#define TEXT_EXTERN extern "C"
#else
#define TEXT_EXTERN
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef TEXT_BUILD
#define TEXT_API TEXT_EXTERN __declspec(dllexport)
#else
#define TEXT_API TEXT_EXTERN __declspec(dllimport)
#endif
#else
#define TEXT_API TEXT_EXTERN __attribute__((visibility("default")))
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
 * TEXT_INTERNAL_API void internal_function(void);
 * @endcode
 */
#ifdef TEXT_TEST_BUILD
#define TEXT_INTERNAL_API TEXT_API
#else
#define TEXT_INTERNAL_API
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
 * size_t count = TEXT_ARRAY_SIZE(arr);  // count = 10
 * @endcode
 */
#ifndef TEXT_ARRAY_SIZE
#define TEXT_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
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
 * unsigned int flags = TEXT_BIT(3);  // flags = 0x08
 * @endcode
 */
#ifndef TEXT_BIT
#define TEXT_BIT(x) (1u << (x))
#endif

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_MACROS_H */
