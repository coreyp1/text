/**
 * @file
 *
 * Allocator abstraction for the Ghoti.io Text library.
 *
 * This is the same four-function vtable that `cutil` defines as
 * `GCU_Allocator` and that the compress, image and model libraries use under
 * local names. `text` declares its own copy rather than including cutil's,
 * because `text` has no cutil dependency and CONVENTIONS.md records that as
 * deliberate - it is the one library in the suite that builds standalone.
 *
 * The copy is not a fork. `GTEXT_Allocator` has the same members in the same
 * order with the same semantics, so it is layout-compatible with
 * `GCU_Allocator` and a program using both libraries can hand the same
 * allocator to each:
 *
 *     const GCU_Allocator * mine = my_arena_allocator();
 *     gtext_json_parse_with_allocator(
 *         src, len, &opts, (const GTEXT_Allocator *)mine, &err);
 *
 * If `text` ever takes a cutil dependency, this becomes
 * `typedef GCU_Allocator GTEXT_Allocator;` and no caller has to change.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_ALLOCATOR_H
#define GHOTI_IO_GTEXT_ALLOCATOR_H

#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A caller-supplied memory management strategy.
 *
 * All four function pointers must be provided. Each receives the `ctx`
 * pointer from this struct as its first argument, so one implementation can
 * serve many independent pools.
 *
 * The semantics match the C standard library equivalents, with two additions
 * that callers rely on:
 *
 * - `calloc_fn` must treat overflow of `nitems * size` as an allocation
 *   failure and return NULL rather than allocating a truncated block.
 * - A zero-size request should return a usable non-NULL pointer rather than
 *   NULL, so that NULL always means failure.
 *
 * The default allocator does both. A custom one is expected to as well,
 * because the library checks for NULL and nothing else.
 *
 * A NULL `GTEXT_Allocator *` anywhere in this library means "use
 * gtext_allocator_default()", so every function taking one may be called
 * without one.
 */
typedef struct GTEXT_Allocator {
  void * ctx; ///< User-defined, passed to each call.
  void * (*malloc_fn)(void * ctx, size_t size);                ///< malloc().
  void * (*calloc_fn)(void * ctx, size_t nitems, size_t size); ///< calloc().
  void * (*realloc_fn)(void * ctx, void * ptr, size_t size);   ///< realloc().
  void (*free_fn)(void * ctx, void * ptr);                     ///< free().
} GTEXT_Allocator;

/**
 * @brief Get the default, stdlib-backed allocator.
 *
 * The returned pointer is to a process-global constant and never needs to be
 * freed. Its `calloc_fn` returns NULL on multiplication overflow, and neither
 * it nor `malloc_fn` returns NULL for a zero-size request.
 *
 * @return A pointer to the default allocator.
 */
GTEXT_API const GTEXT_Allocator * gtext_allocator_default(void);

/**
 * @brief Allocate through an allocator, defaulting when none is supplied.
 *
 * @param allocator The allocator to use, or NULL for the default.
 * @param size The number of bytes requested.
 * @return The allocated memory, or NULL on failure.
 */
GTEXT_API void * gtext_allocator_malloc(
    const GTEXT_Allocator * allocator, size_t size);

/**
 * @brief Allocate zeroed memory through an allocator.
 *
 * @param allocator The allocator to use, or NULL for the default.
 * @param nitems The number of items to allocate.
 * @param size The size of each item.
 * @return The allocated memory, or NULL on failure (including overflow).
 */
GTEXT_API void * gtext_allocator_calloc(
    const GTEXT_Allocator * allocator, size_t nitems, size_t size);

/**
 * @brief Resize an allocation through an allocator.
 *
 * @param allocator The allocator to use, or NULL for the default.
 * @param ptr The allocation to resize, or NULL to allocate afresh.
 * @param size The new size in bytes.
 * @return The resized memory, or NULL on failure, in which case `ptr` is
 *   still valid.
 */
GTEXT_API void * gtext_allocator_realloc(
    const GTEXT_Allocator * allocator, void * ptr, size_t size);

/**
 * @brief Release an allocation through an allocator.
 *
 * @param allocator The allocator to use, or NULL for the default.
 * @param ptr The allocation to release. NULL is ignored.
 */
GTEXT_API void gtext_allocator_free(
    const GTEXT_Allocator * allocator, void * ptr);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_ALLOCATOR_H
