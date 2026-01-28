/**
 * @file
 *
 * JSON Patch (RFC 6902) and JSON Merge Patch (RFC 7386) operations on JSON DOM.
 *
 * This header provides functions for applying JSON Patch and JSON Merge Patch
 * operations to JSON DOM trees.
 *
 * JSON Patch (RFC 6902) allows modifying JSON documents using a sequence of
 * operations (add, remove, replace, move, copy, test).
 *
 * JSON Merge Patch (RFC 7386) allows modifying JSON documents by merging a
 * patch document into a target document recursively.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_JSON_PATCH_H
#define GHOTI_IO_GTEXT_JSON_PATCH_H

#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/macros.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply a JSON Patch to a JSON DOM tree
 *
 * Applies a sequence of patch operations to a JSON value. The patch
 * is represented as a JSON array of operation objects. Operations
 * are applied in order, and if any operation fails, the entire patch
 * application fails (atomicity).
 *
 * JSON Patch format (RFC 6902):
 * - Patch is a JSON array of operation objects
 * - Each operation has an "op" field (add, remove, replace, move, copy, test)
 * - Each operation has a "path" field (JSON Pointer, RFC 6901)
 * - Operations may have "from", "value" fields as needed
 *
 * Operation types:
 * - "add": Add a value at path (replaces if exists, inserts into arrays)
 * - "remove": Remove value at path (must exist)
 * - "replace": Replace value at path (must exist)
 * - "move": Move value from "from" to "path" (from must exist, from != path
 * prefix)
 * - "copy": Copy value from "from" to "path" (from must exist)
 * - "test": Test that value at path equals "value" (fails if not equal)
 *
 * @param root Root JSON value to apply patch to (must not be NULL)
 * @param patch_array JSON array of patch operations (must not be NULL, must be
 * GTEXT_JSON_ARRAY)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return GTEXT_JSON_OK on success, error code on failure
 */
GTEXT_API GTEXT_JSON_Status gtext_json_patch_apply(GTEXT_JSON_Value * root,
    const GTEXT_JSON_Value * patch_array, GTEXT_JSON_Error * err);

/**
 * @brief Apply a JSON Merge Patch to a JSON DOM tree
 *
 * Applies a JSON Merge Patch (RFC 7386) to a JSON value. The patch document
 * is merged recursively into the target document.
 *
 * JSON Merge Patch semantics (RFC 7386):
 * - If the patch is not an object, it replaces the target entirely
 * - If the patch is an object:
 *   - If the target is not an object, it is treated as an empty object first
 *   - For each member in the patch:
 *     - If the value is null, the member is removed from the target
 *     - If the value is non-null, it is recursively merged into the target
 * - Arrays are replaced entirely (not merged)
 *
 * Examples:
 * - Target: {"a":"b"}, Patch: {"a":"c"} -> Result: {"a":"c"}
 * - Target: {"a":"b"}, Patch: {"b":"c"} -> Result: {"a":"b","b":"c"}
 * - Target: {"a":"b"}, Patch: {"a":null} -> Result: {}
 * - Target: {"a":["b"]}, Patch: {"a":"c"} -> Result: {"a":"c"}
 * - Target: ["a","b"], Patch: ["c","d"] -> Result: ["c","d"]
 * - Target: {"a":"foo"}, Patch: null -> Result: null
 *
 * @param target Target JSON value to apply patch to (must not be NULL)
 * @param patch Patch JSON value to merge into target (must not be NULL)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return GTEXT_JSON_OK on success, error code on failure
 */
GTEXT_API GTEXT_JSON_Status gtext_json_merge_patch(GTEXT_JSON_Value * target,
    const GTEXT_JSON_Value * patch, GTEXT_JSON_Error * err);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_JSON_PATCH_H
