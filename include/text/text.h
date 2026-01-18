/**
 * @file text.h
 * @brief Main header for the Ghoti.io Text library
 *
 * This library provides text-based file format parsing and serialization,
 * including JSON, CSV, and configuration formats.
 */

#ifndef GHOTI_IO_TEXT_H
#define GHOTI_IO_TEXT_H

#include <text/macros.h>
#include <stdint.h>

/**
 * @brief Library version information
 */
#define TEXT_VERSION_MAJOR 0
#define TEXT_VERSION_MINOR 0
#define TEXT_VERSION_PATCH 0

/**
 * @brief Get the major version number
 * @return The major version number
 */
TEXT_API uint32_t text_version_major(void);

/**
 * @brief Get the minor version number
 * @return The minor version number
 */
TEXT_API uint32_t text_version_minor(void);

/**
 * @brief Get the patch version number
 * @return The patch version number
 */
TEXT_API uint32_t text_version_patch(void);

/**
 * @brief Get the version string
 * @return A string representation of the version (e.g., "0.0.0")
 */
TEXT_API const char* text_version_string(void);

#endif /* GHOTI_IO_TEXT_H */
