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

#include <stdio.h>
#include <stdlib.h>

#include <ghoti.io/text/text.h>
/**
 * @brief Get the major version number
 * @return The major version number
 */
uint32_t gtext_version_major(void) {
  return GTEXT_VERSION_MAJOR;
}

/**
 * @brief Get the minor version number
 * @return The minor version number
 */
uint32_t gtext_version_minor(void) {
  return GTEXT_VERSION_MINOR;
}

/**
 * @brief Get the patch version number
 * @return The patch version number
 */
uint32_t gtext_version_patch(void) {
  return GTEXT_VERSION_PATCH;
}

/**
 * @brief Get the version string
 * @return A string representation of the version (e.g., "0.0.0")
 */
const char * gtext_version_string(void) {
  static char version_string[32];
  static int initialized = 0;

  if (!initialized) {
    snprintf(version_string, sizeof(version_string), "%u.%u.%u",
        GTEXT_VERSION_MAJOR, GTEXT_VERSION_MINOR, GTEXT_VERSION_PATCH);
    initialized = 1;
  }

  return version_string;
}
