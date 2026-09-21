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
 * Locale-independent number conversion, internal to the library.
 *
 * printf and strtod both read LC_NUMERIC, and in a good many locales its
 * decimal separator is a comma. Every format here has to convert numbers
 * without caring what locale the calling program happens to be in: the
 * separator belongs to the format, not to the user's language settings.
 * Three places had gone wrong on their own for want of somewhere shared to
 * put this, and each failed differently.
 *
 *   - A YAML "a: 0.1" came back as the *string* "0.1" rather than a float,
 *     because strtod stopped at a "." the locale did not recognise and the
 *     resolver takes a partial parse to mean "not a number".
 *   - gtext_json_new_number_double(0.1) wrote its lexeme with a bare
 *     snprintf, so the document it went into read "0,1" - not JSON at all.
 *   - A parsed JSON number quietly ended up with no double value, because
 *     the same partial parse left the has-double flag clear.
 *
 * The JSON writer already had the printf half of the answer, twice, behind
 * a portability #ifdef. It lives here now and everything uses it, on the
 * same reasoning as text_file_io_internal.h: keeping it in one place means
 * a fix to it reaches all three formats rather than one.
 */

#ifndef GHOTI_IO_GTEXT_TEXT_NUMBER_INTERNAL_H
#define GHOTI_IO_GTEXT_TEXT_NUMBER_INTERNAL_H

#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief snprintf, always in the C locale.
 *
 * Same contract as snprintf: returns the length the output would have had,
 * or a negative value on failure. Intended for numeric conversions - it is
 * LC_NUMERIC that is pinned, not the whole locale.
 *
 * @param buf Destination buffer (must not be NULL)
 * @param buf_size Size of @p buf in bytes (must not be 0)
 * @param format printf format string (must not be NULL)
 * @return Bytes the result needed, or negative on failure
 */
GTEXT_INTERNAL_API int gtext_number_format(
    char * buf, size_t buf_size, const char * format, ...);

/**
 * @brief strtod, always in the C locale.
 *
 * Same contract as strtod, including setting errno on range errors, so a
 * caller can go on checking endptr and errno as it did before.
 *
 * @param s String to convert (must not be NULL)
 * @param end Set to the first unconverted character, or NULL if not wanted
 * @return The converted value, or 0 on failure
 */
GTEXT_INTERNAL_API double gtext_number_strtod(const char * s, char ** end);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GTEXT_TEXT_NUMBER_INTERNAL_H */
