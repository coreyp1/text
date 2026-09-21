/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Text.
 *
 * Ghoti.io Text is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Text is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Main header for the Ghoti.io Text library.
 *
 * This library provides text-based file format parsing and serialization,
 * including JSON, CSV, and configuration formats.
 */

#ifndef GHOTI_IO_GTEXT_TEXT_H
#define GHOTI_IO_GTEXT_TEXT_H

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/macros.h>
#include <stdint.h>


/*
 * Library version information comes from libver.h, which takes it from the
 * generated libver_gen.h: GTEXT_VERSION_MAJOR / _MINOR / _PATCH, plus
 * GTEXT_VERSION_STRING and the packed GTEXT_VERSION_NUMBER. It used to be
 * written out here as three zeros that no build step ever updated.
 */

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

#endif // GHOTI_IO_GTEXT_TEXT_H
