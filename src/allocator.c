/**
 * @file
 *
 * The default allocator, and the accessors that make a NULL allocator mean
 * "use the default" everywhere in the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */


#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/macros.h>

/*
 * The default allocator is cutil's.  text used to reimplement it - four static
 * functions wrapping the C library, with the same zero-size and overflow
 * guarantees - which is exactly the duplication that sharing GCU_Allocator
 * removes.  gcu_allocator_default() already makes both guarantees, and the
 * accessors below already treat NULL as "use the default", so a caller sees no
 * difference.
 */

GTEXT_API const GTEXT_Allocator * gtext_allocator_default(void) {
  return gcu_allocator_default();
}

GTEXT_API void * gtext_allocator_malloc(
    const GTEXT_Allocator * allocator, size_t size) {
  if (!allocator) {
    allocator = gcu_allocator_default();
  }
  return allocator->malloc_fn(allocator->ctx, size);
}

GTEXT_API void * gtext_allocator_calloc(
    const GTEXT_Allocator * allocator, size_t nitems, size_t size) {
  if (!allocator) {
    allocator = gcu_allocator_default();
  }
  return allocator->calloc_fn(allocator->ctx, nitems, size);
}

GTEXT_API void * gtext_allocator_realloc(
    const GTEXT_Allocator * allocator, void * ptr, size_t size) {
  if (!allocator) {
    allocator = gcu_allocator_default();
  }
  return allocator->realloc_fn(allocator->ctx, ptr, size);
}

GTEXT_API void gtext_allocator_free(
    const GTEXT_Allocator * allocator, void * ptr) {
  if (!ptr) {
    return;
  }
  if (!allocator) {
    allocator = gcu_allocator_default();
  }
  allocator->free_fn(allocator->ctx, ptr);
}
