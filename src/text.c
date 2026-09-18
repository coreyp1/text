/**
 * @file
 *
 * Main implementation for the Ghoti.io Text library.
 *
 * This file contains version information accessors and any shared
 * utilities used across the text library modules (JSON, CSV, etc.).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/text.h>
GTEXT_API uint32_t gtext_version_major(void) {
  return GTEXT_VERSION_MAJOR;
}

GTEXT_API uint32_t gtext_version_minor(void) {
  return GTEXT_VERSION_MINOR;
}

GTEXT_API uint32_t gtext_version_patch(void) {
  return GTEXT_VERSION_PATCH;
}

GTEXT_API const char * gtext_version_string(void) {
  /* GTEXT_VERSION_STRING is produced by the build from the same three numbers
   * this used to format at run time into a function-local static, guarded by
   * a second static flag.  Two threads calling this at once both saw the flag
   * clear and both wrote the buffer - a data race, and a reader could observe
   * a partially written string.  The values are known at compile time, so the
   * formatting, the buffer and the race all go away together. */
  return GTEXT_VERSION_STRING;
}
