/**
 * @file json_schema_internal.h
 * @brief Internal helper functions for JSON schema operations
 *
 * This header contains documentation for static helper functions used
 * internally within json_schema.c. These functions are not part of the
 * public API and are only visible within json_schema.c.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_JSON_SCHEMA_INTERNAL_H
#define GHOTI_IO_GTEXT_JSON_SCHEMA_INTERNAL_H

#include "json_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Schema Node Management
// ============================================================================

/**
 * @brief Free a schema node recursively
 *
 * @fn static void json_schema_node_free(json_schema_node * node)
 *
 * @note This is a static function defined in json_schema.c
 */

/**
 * @brief Parse type keyword
 *
 * @fn static GTEXT_JSON_Status json_schema_parse_type(json_schema_node * node, const GTEXT_JSON_Value * type_value, GTEXT_JSON_Error * err)
 *
 * @note This is a static function defined in json_schema.c
 */

/**
 * @brief Compile a schema node from a JSON value
 *
 * @fn static GTEXT_JSON_Status json_schema_compile_node(json_schema_node * node, const GTEXT_JSON_Value * schema_doc, json_context * ctx, GTEXT_JSON_Error * err)
 *
 * @note This is a static function defined in json_schema.c
 */

/**
 * @brief Validate a value against a schema node
 *
 * @fn static GTEXT_JSON_Status json_schema_validate_node(const json_schema_node * node, const GTEXT_JSON_Value * instance, GTEXT_JSON_Error * err)
 *
 * @note This is a static function defined in json_schema.c
 */

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GTEXT_JSON_SCHEMA_INTERNAL_H */
