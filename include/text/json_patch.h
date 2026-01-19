/**
 * @file json_patch.h
 * @brief JSON Patch (RFC 6902) operations on JSON DOM
 *
 * This header provides functions for applying JSON Patch operations to
 * JSON DOM trees. JSON Patch allows modifying JSON documents using a
 * sequence of operations (add, remove, replace, move, copy, test).
 */

#ifndef GHOTI_IO_TEXT_JSON_PATCH_H
#define GHOTI_IO_TEXT_JSON_PATCH_H

#include <text/macros.h>
#include <text/json.h>
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
 * - "move": Move value from "from" to "path" (from must exist, from != path prefix)
 * - "copy": Copy value from "from" to "path" (from must exist)
 * - "test": Test that value at path equals "value" (fails if not equal)
 *
 * @param root Root JSON value to apply patch to (must not be NULL)
 * @param patch_array JSON array of patch operations (must not be NULL, must be TEXT_JSON_ARRAY)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return TEXT_JSON_OK on success, error code on failure
 */
TEXT_API text_json_status text_json_patch_apply(
    text_json_value* root,
    const text_json_value* patch_array,
    text_json_error* err
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_PATCH_H */
