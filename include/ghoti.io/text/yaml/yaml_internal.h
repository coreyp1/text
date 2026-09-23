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

/*
 * Internal YAML header exposed for test builds.
 * This mirrors `src/yaml/yaml_internal.h` and is intentionally minimal.
 */
#ifndef GHOTI_IO_GTEXT_YAML_YAML_INTERNAL_H
#define GHOTI_IO_GTEXT_YAML_YAML_INTERNAL_H

#include <ghoti.io/text/allocator.h>
#include <stddef.h>
#include <ghoti.io/text/macros.h>

typedef struct GTEXT_YAML_CharReader GTEXT_YAML_CharReader;

GTEXT_INTERNAL_API GTEXT_YAML_CharReader * gtext_yaml_char_reader_new(
	const char * data,
	size_t len,
	const GTEXT_Allocator * alloc
);
GTEXT_INTERNAL_API void gtext_yaml_char_reader_free(GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API int gtext_yaml_char_reader_peek(GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API int gtext_yaml_char_reader_consume(GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API size_t gtext_yaml_char_reader_offset(const GTEXT_YAML_CharReader * r);
GTEXT_INTERNAL_API void gtext_yaml_char_reader_position(
	const GTEXT_YAML_CharReader * r,
	int * line,
	int * col
);

#endif // GHOTI_IO_GTEXT_YAML_YAML_INTERNAL_H
