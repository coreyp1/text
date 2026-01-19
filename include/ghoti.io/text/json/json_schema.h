/**
 * @file json_schema.h
 * @brief JSON Schema validation (core subset)
 *
 * This header provides functions for compiling and validating JSON values
 * against JSON Schema documents. This implementation supports a pragmatic
 * core subset of JSON Schema features.
 *
 * Supported schema keywords (core subset):
 * - type: Validate value type (null, boolean, number, string, array, object)
 * - properties: Object property schemas (recursive validation)
 * - required: List of required property names
 * - items: Array item schema (all items must match)
 * - enum: Array of allowed values (exact match)
 * - const: Single allowed value (exact match)
 * - minimum/maximum: Numeric constraints
 * - minLength/maxLength: String length constraints
 * - minItems/maxItems: Array size constraints
 *
 * The schema engine is designed to be modular and optional at compile time.
 */

#ifndef GHOTI_IO_TEXT_JSON_SCHEMA_H
#define GHOTI_IO_TEXT_JSON_SCHEMA_H

#include <ghoti.io/text/macros.h>
#include <ghoti.io/text/json/json_core.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque schema structure
 *
 * Represents a compiled JSON Schema. The structure is opaque to users;
 * all interaction is through the API functions.
 */
typedef struct text_json_schema text_json_schema;

/**
 * @brief Compile a JSON Schema document into a compiled schema
 *
 * Parses and validates a JSON Schema document, compiling it into an
 * internal representation for efficient validation. The schema document
 * must be a valid JSON object.
 *
 * The compiled schema is independent of the original schema document;
 * the document can be freed after compilation.
 *
 * @param schema_doc JSON value representing the schema document (must not be NULL, must be TEXT_JSON_OBJECT)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return Compiled schema on success, NULL on failure (check err for details)
 */
TEXT_API text_json_schema* text_json_schema_compile(
    const text_json_value* schema_doc,
    text_json_error* err
);

/**
 * @brief Free a compiled schema
 *
 * Releases all memory associated with a compiled schema. After calling
 * this function, the schema pointer is invalid and must not be used.
 *
 * @param schema Schema to free (can be NULL, in which case this is a no-op)
 */
TEXT_API void text_json_schema_free(text_json_schema* schema);

/**
 * @brief Validate a JSON value against a compiled schema
 *
 * Validates a JSON value against a compiled schema. Returns TEXT_JSON_OK
 * if the value matches the schema, or TEXT_JSON_E_SCHEMA if validation fails.
 *
 * Error details are provided in the err structure, including which schema
 * keyword failed and why.
 *
 * @param schema Compiled schema (must not be NULL)
 * @param instance JSON value to validate (must not be NULL)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return TEXT_JSON_OK if validation succeeds, TEXT_JSON_E_SCHEMA if validation fails
 */
TEXT_API text_json_status text_json_schema_validate(
    const text_json_schema* schema,
    const text_json_value* instance,
    text_json_error* err
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_SCHEMA_H */
