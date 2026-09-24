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
 * JSONPath (RFC 9535) queries over a JSON DOM.
 *
 * A JSONPath query selects zero or more nodes from a document. Where JSON
 * Pointer (RFC 6901) names exactly one place, a query describes a set: every
 * element of an array, every value in an object, everything at any depth under
 * a name.
 *
 * ```c
 * GTEXT_JSON_Path_Result r;
 * if (gtext_json_path_query(root, "$.store.book[*].author", SIZE_MAX, NULL,
 *         &r, &err) == GTEXT_JSON_OK) {
 *   for (size_t i = 0; i < r.count; i++) {
 *     // r.nodes[i] belongs to the document, not to the result
 *   }
 *   gtext_json_path_result_free(&r);
 * }
 * ```
 *
 * The nodes in a result are borrowed: they belong to the document, which must
 * outlive the result. What the result owns is the array holding them, which
 * @ref gtext_json_path_result_free releases.
 *
 * **What is not implemented.** `match()` and `search()` need an I-Regexp
 * engine, which this library does not have, so a query using either is refused
 * at compile time with @ref GTEXT_JSON_E_PATH_UNSUPPORTED. Everything else in
 * RFC 9535 is here, the filter selector included: `&&`, `||`, `!`,
 * parentheses, the six comparison operators, and `length()`, `count()` and
 * `value()`.
 *
 * A query this build cannot evaluate is refused rather than evaluated as though
 * the construct were absent, for the same reason the schema engine refuses a
 * schema it cannot enforce: silently selecting more nodes than the query asked
 * for is worse than saying no.
 *
 * An **ill-typed** query - `length()` over a multi-node query, `count()` of a
 * literal, a comparison against a non-singular query, a value used as a test -
 * is @ref GTEXT_JSON_E_PATH, because §2.4.2 makes it invalid rather than false.
 */

#ifndef GHOTI_IO_GTEXT_JSON_JSON_PATH_H
#define GHOTI_IO_GTEXT_JSON_JSON_PATH_H

#include <ghoti.io/text/allocator.h>
#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A compiled JSONPath query
 *
 * Opaque. Compile once with @ref gtext_json_path_compile and evaluate against
 * as many documents as you like; release with @ref gtext_json_path_free.
 */
typedef struct GTEXT_JSON_Path GTEXT_JSON_Path;

/**
 * @brief The nodes a query selected, in the order RFC 9535 gives them
 *
 * `nodes` is owned by the result and released by
 * @ref gtext_json_path_result_free. The values it points at are owned by the
 * document.
 *
 * A node may appear more than once: `$[0,0]` selects the first element twice,
 * and the specification says so.
 */
typedef struct {
  const GTEXT_JSON_Value ** nodes; ///< Selected nodes, or NULL when count is 0
  size_t count;                    ///< How many
  const GTEXT_Allocator * alloc;   ///< The allocator `nodes` came from
} GTEXT_JSON_Path_Result;

/**
 * @brief Compile a JSONPath query
 *
 * @param query The query text, beginning with `$`
 * @param len Its length in bytes, or SIZE_MAX to measure it with strlen()
 * @param alloc Allocator for the compiled query, or NULL for the default
 * @param err Filled in on failure, or NULL. `context_snippet` is not set.
 * @return The compiled query, or NULL. The status in @p err is
 *   GTEXT_JSON_E_PATH for a query that is not well-formed,
 *   GTEXT_JSON_E_PATH_UNSUPPORTED for one this build cannot evaluate, and
 *   GTEXT_JSON_E_OOM if it could not be allocated.
 */
GTEXT_API GTEXT_JSON_Path * gtext_json_path_compile(const char * query,
    size_t len, const GTEXT_Allocator * alloc, GTEXT_JSON_Error * err);

/**
 * @brief Release a compiled query
 *
 * @param path The query, or NULL
 */
GTEXT_API void gtext_json_path_free(GTEXT_JSON_Path * path);

/**
 * @brief Evaluate a compiled query against a document
 *
 * @param path The compiled query (must not be NULL)
 * @param root The document to query (must not be NULL)
 * @param out Filled in with the selected nodes; zeroed first, so a result with
 *   nothing in it has count 0 and nodes NULL
 * @return GTEXT_JSON_OK, GTEXT_JSON_E_INVALID for a NULL argument, or
 *   GTEXT_JSON_E_OOM
 */
GTEXT_API GTEXT_JSON_Status gtext_json_path_select(const GTEXT_JSON_Path * path,
    const GTEXT_JSON_Value * root, GTEXT_JSON_Path_Result * out);

/**
 * @brief Compile, evaluate, and release the query
 *
 * For a query used once. A query used repeatedly should be compiled once.
 *
 * @param root The document to query (must not be NULL)
 * @param query The query text
 * @param len Its length in bytes, or SIZE_MAX to measure it with strlen()
 * @param alloc Allocator for both the query and the result, or NULL
 * @param out Filled in with the selected nodes
 * @param err Filled in on failure, or NULL
 * @return As @ref gtext_json_path_compile and @ref gtext_json_path_select
 */
GTEXT_API GTEXT_JSON_Status gtext_json_path_query(const GTEXT_JSON_Value * root,
    const char * query, size_t len, const GTEXT_Allocator * alloc,
    GTEXT_JSON_Path_Result * out, GTEXT_JSON_Error * err);

/**
 * @brief Release the array a result owns
 *
 * Leaves the result zeroed, so calling it twice is safe. The nodes themselves
 * belong to the document and are not touched.
 *
 * @param result The result, or NULL
 */
GTEXT_API void gtext_json_path_result_free(GTEXT_JSON_Path_Result * result);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_JSON_JSON_PATH_H
