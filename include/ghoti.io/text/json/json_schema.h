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
 * Supported schema keywords:
 * - type: null, boolean, object, array, number, integer, string - and arrays
 *   of those. "integer" constrains the value rather than naming a distinct
 *   JSON type, so 5 and 5.0 satisfy it and 5.5 does not.
 * - properties: Object property schemas (recursive validation)
 * - required: List of required property names
 * - items: Array item schema (all items must match)
 * - enum: Array of allowed values (exact match)
 * - const: Single allowed value (exact match)
 * - minimum/maximum: Numeric constraints
 * - minLength/maxLength: String length constraints
 * - minItems/maxItems: Array size constraints
 * - uniqueItems: Array elements must be pairwise distinct, by structural
 *   equality
 * - exclusiveMinimum/exclusiveMaximum: Strict numeric bounds
 * - multipleOf: Exact divisibility; the divisor must be greater than zero
 * - minProperties/maxProperties: Object size constraints
 * - dependentRequired: One property's presence requires others
 * - allOf/anyOf/oneOf/not: Boolean applicators. oneOf is exactly one, so two
 *   matching branches is a failure
 * - if/then/else: "if" selects rather than asserts; an absent branch is no
 *   constraint
 * - $ref, with $defs and definitions: same-document JSON Pointer references
 *   ("#" and "#/..."). Recursive references work; targets are compiled once
 *   and shared. A reference that does not resolve, an external URI and a
 *   named anchor are all refused at compile time
 * - prefixItems and additionalItems, with draft-07's array-valued "items"
 *   compiling to the same thing
 * - contains, minContains, maxContains
 * - additionalProperties, propertyNames
 * - dependentSchemas, and draft-07's "dependencies" in either of its forms
 * - Boolean schemas: "true" accepts everything and "false" nothing, anywhere
 *   a schema is allowed, the root included
 *
 * - pattern, patternProperties: only when the caller supplies a
 *   regular-expression provider through GTEXT_JSON_Schema_Options. Without
 *   one they remain in the unsupported list below, because this library has
 *   no regular-expression engine of its own and inventing a half one would
 *   be the silent-mis-validation failure the strict check exists to prevent
 *
 * Unsupported standard keywords (rejected at compile time):
 * - pattern, patternProperties, when no provider was supplied
 * - unevaluatedItems, unevaluatedProperties - these need annotation results
 *   to be collected across applicators, which nothing here does yet
 * - $recursiveRef, $dynamicRef - the 2019-09 and 2020-12 dynamic-scope
 *   references
 * - format, contentEncoding, contentMediaType, contentSchema
 *
 * Note on $ref depth: a schema that refers to itself without consuming any
 * instance, such as {"$ref":"#"}, compiles successfully and fails validation
 * with GTEXT_JSON_E_DEPTH rather than recursing without bound.
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
 * @brief A regular-expression engine, supplied by the caller.
 *
 * JSON Schema's `pattern` and `patternProperties` are regular expressions,
 * and this library has no engine. Rather than acquire a dependency on one -
 * which every caller would then pay for, including the many who never write a
 * `pattern` - the engine arrives through this vtable, and the two keywords
 * are enforced when one is present and refused when it is not.
 *
 * Three obligations, and each is somewhere a validator can quietly get it
 * wrong:
 *
 * - **The dialect is ECMA-262 with the `u` flag.** JSON Schema core section
 *   6.4 says so. A schema author writing `\d` means ECMA-262's `\d`, which
 *   is ASCII, and not Python's or .NET's, which are not.
 * - **The match is a search, not an anchored match.** The pattern need only
 *   match *somewhere* in the string, so a provider that anchors rejects
 *   instances the specification accepts. This is the single most common
 *   mistake in the wild.
 * - **Both strings are UTF-8 and may contain a NUL.** Lengths are given;
 *   neither is terminated for you.
 *
 * `ctx` is passed to every call, so one implementation can serve several
 * independent configurations. Everything reachable from it must outlive every
 * compiled schema that used it: compiled patterns are freed when the schema
 * is, which is after the caller has stopped thinking about the provider.
 */
typedef struct {
  /**
   * User-defined, passed to each call.
   */
  void * ctx;

  /**
   * Compile one pattern. Return 0 on success, having stored an opaque handle
   * in `*out_regex`; return non-zero on failure.
   *
   * On failure, write a description into `message` - at most
   * `message_capacity` bytes including the terminator, never more - and store
   * the byte offset within the pattern at which it went wrong in
   * `*out_offset`. Both are copied out before the call returns, so neither
   * needs to outlive it: the message reaches the caller in
   * GTEXT_JSON_Error::context_snippet and the offset in
   * GTEXT_JSON_Error::offset.
   *
   * A refusal that has no position - "this pattern needs an engine whose
   * worst case is exponential" is about the whole of it - stores `(size_t)-1`
   * for the offset, and the caller sees `(size_t)-1` rather than 0, so that
   * nothing points at a character that is not the problem.
   *
   * `pattern` is UTF-8 of `pattern_len` bytes and is not NUL-terminated.
   */
  int (*compile_fn)(void * ctx, const char * pattern, size_t pattern_len,
      void ** out_regex, char * message, size_t message_capacity,
      size_t * out_offset);

  /**
   * Search `subject` for `regex`. Return a positive value if it matches
   * anywhere, 0 if it does not, and a **negative** value if the search could
   * not be completed - a step or memory limit reached, an allocation failed.
   *
   * The negative case exists because it is a third answer and not a second
   * one. A pattern that exhausts its budget has not told you the instance is
   * invalid; reporting that as "no match" turns a denial-of-service defence
   * into a wrong validation result. It surfaces as GTEXT_JSON_E_LIMIT, which
   * is neither GTEXT_JSON_OK nor GTEXT_JSON_E_SCHEMA.
   *
   * `subject` is UTF-8 of `subject_len` bytes and is not NUL-terminated.
   */
  int (*search_fn)(
      void * ctx, void * regex, const char * subject, size_t subject_len);

  /**
   * Release a handle `compile_fn` produced. Called once per handle, when the
   * compiled schema is freed. Never called with NULL.
   */
  void (*free_fn)(void * ctx, void * regex);
} GTEXT_JSON_Regex_Provider;

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

  /**
   * The regular-expression engine `pattern` and `patternProperties` are
   * enforced with, or NULL for none.
   *
   * With a provider, both keywords are compiled at schema-compile time and
   * checked at validation time. Without one, both are refused at compile time
   * the way any other unenforceable keyword is - unless
   * allow_unsupported_keywords is set, which ignores them along with the rest.
   *
   * The pointer is borrowed, and so is everything reachable from its `ctx`.
   * Both must outlive every schema compiled with them, because the compiled
   * patterns are released when the schema is freed.
   */
  const GTEXT_JSON_Regex_Provider * regex;
} GTEXT_JSON_Schema_Options;

/**
 * @brief Get the default schema compilation options
 *
 * @return Options with allow_unsupported_keywords set to false and no
 *   regular-expression provider.
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
 *   NULL; an object, or a boolean, which is a schema accepting everything or
 *   nothing)
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
 *   name. When a `pattern` or `patternProperties` regular expression fails to
 *   compile, the code is GTEXT_JSON_E_INVALID - a malformed pattern is a
 *   malformed schema, the same fault as `"properties": 3` - context_snippet
 *   holds the provider's own message, and `offset` the byte offset it
 *   reported within the pattern, or `(size_t)-1` if it reported none. Either
 *   way, free it with gtext_json_error_free().
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
