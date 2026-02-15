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
  static char version_string[32];
  static int initialized = 0;

  if (!initialized) {
    snprintf(version_string, sizeof(version_string),
      "%" PRIu32 ".%" PRIu32 ".%" PRIu32,
      (uint32_t)GTEXT_VERSION_MAJOR,
      (uint32_t)GTEXT_VERSION_MINOR,
      (uint32_t)GTEXT_VERSION_PATCH);
    initialized = 1;
  }

  return version_string;
}
