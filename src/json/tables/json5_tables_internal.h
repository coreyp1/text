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
 * The shape of the generated JSON5 identifier table.
 *
 * Hand-written; tables/json5_ident_tables.c is generated. The enumeration
 * here is what that file names, so a table regenerated against newer data
 * fails to compile rather than silently meaning something else.
 */

#ifndef GHOTI_IO_GTEXT_SRC_JSON_TABLES_JSON5_TABLES_INTERNAL_H
#define GHOTI_IO_GTEXT_SRC_JSON_TABLES_JSON5_TABLES_INTERNAL_H

#include <ghoti.io/text/macros.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Where in an ECMAScript IdentifierName a codepoint may appear
 *
 * ID_Continue is a superset of ID_Start, so one table carries both: START
 * also continues, and a codepoint the table does not mention does neither.
 *
 * Two of ECMAScript's own additions are in the lexer rather than here,
 * because they are not Unicode properties: `$` in either position, and `_`
 * as a start (it is Pc, so the table already has it as CONT). ZWNJ and ZWJ
 * need no such handling - they carry ID_Continue in this UCD, so the table
 * admits them where ECMAScript does.
 */
typedef enum {
  GTEXT_JSON5_IDENT_START = 1, ///< May start a name, and may continue one
  GTEXT_JSON5_IDENT_CONT = 2   ///< May continue a name, but not start one
} GTEXT_JSON5_Ident_Class;

/** One run of codepoints sharing a class. */
typedef struct {
  uint32_t lo;
  uint32_t hi;
  uint32_t value;
} GTEXT_JSON5_Ident_Range;

extern const GTEXT_JSON5_Ident_Range gtext_json5_ident[];
extern const size_t gtext_json5_ident_count;

/**
 * @brief One run of codepoints in General_Category Zs
 *
 * ECMAScript's WhiteSpace production includes <USP>, which is Zs, and JSON5
 * takes its whitespace from ECMAScript. No value field: membership is the
 * whole answer.
 *
 * The rest of that production - TAB, VT, FF and ZWNBSP - and the
 * LineTerminators are named by ECMAScript rather than derived from a Unicode
 * property, so they are in the lexer instead.
 */
typedef struct {
  uint32_t lo;
  uint32_t hi;
} GTEXT_JSON5_Space_Range;

extern const GTEXT_JSON5_Space_Range gtext_json5_space[];
extern const size_t gtext_json5_space_count;

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_SRC_JSON_TABLES_JSON5_TABLES_INTERNAL_H
