/**
 * @file
 *
 * JSON Schema validation (core subset).
 *
 * This header provides functions for compiling and validating JSON values
 * against JSON Schema documents. This implementation supports a pragmatic
 * core subset of JSON Schema features.
 *
 * The engine enforces a core subset, and **refuses schemas it cannot fully
 * enforce**. A schema using a standard keyword from the list of unsupported
 * keywords below fails to compile with GTEXT_JSON_E_SCHEMA_UNSUPPORTED and
 * the offending keyword named in the error's context_snippet. This is
 * deliberate: silently ignoring an assertion keyword makes invalid data
 * validate clean, which is worse than refusing the schema outright.
 *
 * Genuinely unknown keywords - vendor extensions, and the annotation
 * keywords title, description, default, examples, $comment, readOnly,
 * writeOnly and deprecated - are ignored, as JSON Schema requires. So are
 * $schema, $id, $defs, definitions, $anchor and $vocabulary, which cannot
 * change which instances are valid while $ref is unsupported.
 *
 * Callers that genuinely want the old behavior can set
 * allow_unsupported_keywords in GTEXT_JSON_Schema_Options and compile with
 * gtext_json_schema_compile_with_options().
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
 * Unsupported standard keywords (rejected at compile time):
 * - Applicators: $ref, $recursiveRef, $dynamicRef, allOf, anyOf, oneOf, not,
 *   if, then, else, additionalItems, prefixItems, contains, minContains,
 *   maxContains, additionalProperties, patternProperties, propertyNames,
 *   dependentSchemas, dependentRequired, dependencies, unevaluatedItems,
 *   unevaluatedProperties
 * - Assertions: pattern, format, multipleOf, exclusiveMinimum,
 *   exclusiveMaximum, uniqueItems, minProperties, maxProperties,
 *   contentEncoding, contentMediaType, contentSchema
 *
 * The schema engine is designed to be modular and optional at compile time.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GTEXT_JSON_JSON_SCHEMA_H
#define GHOTI_IO_GTEXT_JSON_JSON_SCHEMA_H

#include <ghoti.io/text/json/json_core.h>
#include <ghoti.io/text/macros.h>
#include <stdbool.h>
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
typedef struct GTEXT_JSON_Schema GTEXT_JSON_Schema;

/**
 * @brief Options controlling schema compilation
 */
typedef struct {
  /**
   * Accept schemas that use standard keywords this implementation does not
   * enforce, ignoring those keywords instead of refusing the schema.
   *
   * Default: false. Setting it restores the behavior of releases before the
   * strict check existed, in which a schema using `$ref`, `allOf`, `pattern`
   * or `additionalProperties` compiled successfully and then validated
   * instances those keywords should have rejected. Set it only when the
   * ignored keywords are known to be decorative.
   */
  bool allow_unsupported_keywords;
} GTEXT_JSON_Schema_Options;

/**
 * @brief Get the default schema compilation options
 *
 * @return Options with allow_unsupported_keywords set to false.
 */
GTEXT_API GTEXT_JSON_Schema_Options gtext_json_schema_options_default(void);

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
 * @param schema_doc JSON value representing the schema document (must not be
 * NULL, must be GTEXT_JSON_OBJECT)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return Compiled schema on success, NULL on failure (check err for details)
 */
GTEXT_API GTEXT_JSON_Schema * gtext_json_schema_compile(
    const GTEXT_JSON_Value * schema_doc, GTEXT_JSON_Error * err);

/**
 * @brief Compile a JSON Schema document with explicit options
 *
 * Identical to gtext_json_schema_compile() except that the caller chooses
 * how unsupported standard keywords are treated.
 *
 * @param schema_doc JSON value representing the schema document (must not be
 *   NULL, must be GTEXT_JSON_OBJECT)
 * @param opts Compilation options, or NULL for the defaults
 * @param err Error output structure (can be NULL if error details not needed).
 *   When a schema is refused for using an unsupported keyword, the code is
 *   GTEXT_JSON_E_SCHEMA_UNSUPPORTED and context_snippet holds the keyword
 *   name; free it with gtext_json_error_free().
 * @return Compiled schema on success, NULL on failure
 */
GTEXT_API GTEXT_JSON_Schema * gtext_json_schema_compile_with_options(
    const GTEXT_JSON_Value * schema_doc,
    const GTEXT_JSON_Schema_Options * opts, GTEXT_JSON_Error * err);

/**
 * @brief Free a compiled schema
 *
 * Releases all memory associated with a compiled schema. After calling
 * this function, the schema pointer is invalid and must not be used.
 *
 * @param schema Schema to free (can be NULL, in which case this is a no-op)
 */
GTEXT_API void gtext_json_schema_free(GTEXT_JSON_Schema * schema);

/**
 * @brief Validate a JSON value against a compiled schema
 *
 * Validates a JSON value against a compiled schema. Returns GTEXT_JSON_OK
 * if the value matches the schema, or GTEXT_JSON_E_SCHEMA if validation fails.
 *
 * Error details are provided in the err structure, including which schema
 * keyword failed and why.
 *
 * @param schema Compiled schema (must not be NULL)
 * @param instance JSON value to validate (must not be NULL)
 * @param err Error output structure (can be NULL if error details not needed)
 * @return GTEXT_JSON_OK if validation succeeds, GTEXT_JSON_E_SCHEMA if
 * validation fails
 */
GTEXT_API GTEXT_JSON_Status gtext_json_schema_validate(
    const GTEXT_JSON_Schema * schema, const GTEXT_JSON_Value * instance,
    GTEXT_JSON_Error * err);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_GTEXT_JSON_JSON_SCHEMA_H
