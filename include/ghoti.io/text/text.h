/**
 * @file
 *
 * Main header for the Ghoti.io Text library.
 *
 * This library provides text-based file format parsing and serialization,
 * including JSON, CSV, and configuration formats.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_TEXT_H
#define GHOTI_IO_TEXT_H

#include <ghoti.io/text/macros.h>
#include <stdint.h>

/**
 * @brief Library version information
 */
#define GTEXT_VERSION_MAJOR 0
#define GTEXT_VERSION_MINOR 0
#define GTEXT_VERSION_PATCH 0

/**
 * @brief Get the major version number
 * @return The major version number
 */
GTEXT_API uint32_t gtext_version_major(void);

/**
 * @brief Get the minor version number
 * @return The minor version number
 */
GTEXT_API uint32_t gtext_version_minor(void);

/**
 * @brief Get the patch version number
 * @return The patch version number
 */
GTEXT_API uint32_t gtext_version_patch(void);

/**
 * @brief Get the version string
 * @return A string representation of the version (e.g., "0.0.0")
 */
GTEXT_API const char * gtext_version_string(void);

#endif /* GHOTI_IO_TEXT_H */
