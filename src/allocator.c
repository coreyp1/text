/**
 * @file
 *
 * The default allocator, and the accessors that make a NULL allocator mean
 * "use the default" everywhere in the library.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <stdint.h>
#include <stdlib.h>

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/macros.h>

/*
 * The default allocator forwards to the C library, with the two guarantees
 * the header promises: calloc() failing on overflow, and a zero-size request
 * returning a usable pointer rather than NULL.  glibc's malloc(0) already
 * returns a unique non-NULL pointer, but that is implementation-defined, so
 * the size is rounded up rather than relying on it.
 */

static void * gtext_default_malloc(void * ctx, size_t size) {
  (void)ctx;
  return malloc(size ? size : 1);
}

static void * gtext_default_calloc(void * ctx, size_t nitems, size_t size) {
  (void)ctx;
  if (nitems == 0 || size == 0) {
    return calloc(1, 1);
  }
  /* calloc() is required to detect this itself, but not every platform's
   * does, and a truncated block is far worse than a failed allocation. */
  if (nitems > SIZE_MAX / size) {
    return NULL;
  }
  return calloc(nitems, size);
}

static void * gtext_default_realloc(void * ctx, void * ptr, size_t size) {
  (void)ctx;
  return realloc(ptr, size ? size : 1);
}

static void gtext_default_free(void * ctx, void * ptr) {
  (void)ctx;
  free(ptr);
}

static const GTEXT_Allocator gtext_allocator_stdlib = {
    .ctx = NULL,
    .malloc_fn = gtext_default_malloc,
    .calloc_fn = gtext_default_calloc,
    .realloc_fn = gtext_default_realloc,
    .free_fn = gtext_default_free,
};

GTEXT_API const GTEXT_Allocator * gtext_allocator_default(void) {
  return &gtext_allocator_stdlib;
}

GTEXT_API void * gtext_allocator_malloc(
    const GTEXT_Allocator * allocator, size_t size) {
  if (!allocator) {
    allocator = &gtext_allocator_stdlib;
  }
  return allocator->malloc_fn(allocator->ctx, size);
}

GTEXT_API void * gtext_allocator_calloc(
    const GTEXT_Allocator * allocator, size_t nitems, size_t size) {
  if (!allocator) {
    allocator = &gtext_allocator_stdlib;
  }
  return allocator->calloc_fn(allocator->ctx, nitems, size);
}

GTEXT_API void * gtext_allocator_realloc(
    const GTEXT_Allocator * allocator, void * ptr, size_t size) {
  if (!allocator) {
    allocator = &gtext_allocator_stdlib;
  }
  return allocator->realloc_fn(allocator->ctx, ptr, size);
}

GTEXT_API void gtext_allocator_free(
    const GTEXT_Allocator * allocator, void * ptr) {
  if (!ptr) {
    return;
  }
  if (!allocator) {
    allocator = &gtext_allocator_stdlib;
  }
  allocator->free_fn(allocator->ctx, ptr);
}
