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
 * The shape of the embedded 2020-12 meta-schemas.
 *
 * Hand-written; only metaschema/metaschema_docs.c is generated.
 */

#ifndef GHOTI_IO_GTEXT_SRC_JSON_METASCHEMA_METASCHEMA_INTERNAL_H
#define GHOTI_IO_GTEXT_SRC_JSON_METASCHEMA_METASCHEMA_INTERNAL_H

#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One published document, under the URI that names it
 *
 * `text` is the document's bytes exactly as published, not NUL-terminated as
 * far as `len` is concerned - the array has a terminator because it is a C
 * string literal, and `len` is its length without one.
 */
typedef struct {
  const char * uri; ///< Absolute, no fragment
  const char * text;
  size_t len;
} json_metaschema_doc;

/**
 * The nine documents of the 2020-12 dialect: the root meta-schema, the seven
 * vocabulary meta-schemas its `allOf` references, and format-assertion, which
 * describes the dialect's one optional vocabulary and so is referenced by
 * schemas that declare it rather than by the root.
 */
extern const json_metaschema_doc json_metaschema_docs[];
extern const size_t json_metaschema_doc_count;

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_SRC_JSON_METASCHEMA_METASCHEMA_INTERNAL_H
