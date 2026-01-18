/**
 * @file json_internal.h
 * @brief Internal definitions for JSON module implementation
 *
 * This header contains internal-only definitions used by the JSON module
 * implementation. It should not be included by external code.
 */

#ifndef GHOTI_IO_TEXT_JSON_INTERNAL_H
#define GHOTI_IO_TEXT_JSON_INTERNAL_H

#include <text/json.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Position tracking structure for string processing
 */
typedef struct {
    size_t offset;       ///< Byte offset from start
    int line;            ///< Line number (1-based)
    int col;             ///< Column number (1-based, byte-based)
} json_position;

/**
 * @brief UTF-8 handling mode
 */
typedef enum {
    JSON_UTF8_REJECT,    ///< Reject invalid UTF-8 sequences
    JSON_UTF8_REPLACE,   ///< Replace invalid sequences with replacement character
    JSON_UTF8_VERBATIM   ///< Allow invalid sequences verbatim
} json_utf8_mode;

/**
 * @brief Check if a length-delimited string exactly equals a null-terminated keyword
 *
 * This utility function compares a length-delimited string (input with length len)
 * against a null-terminated keyword string. Useful for matching JSON keywords
 * like "true", "false", "null", "NaN", "Infinity", etc.
 *
 * @param input Input string (may not be null-terminated)
 * @param len Length of input string
 * @param keyword Null-terminated keyword to match against
 * @return 1 if strings match exactly (case-sensitive), 0 otherwise
 */
int json_matches(const char* input, size_t len, const char* keyword);

/**
 * @brief Decode a JSON string with escape sequences
 *
 * This function decodes a JSON string, handling:
 * - Standard escape sequences (\", \\, \/, \b, \f, \n, \r, \t)
 * - Unicode escapes (\uXXXX)
 * - Surrogate pairs (\uD83D\uDE00)
 *
 * The function performs bounds checking to prevent buffer overflow.
 * If the decoded string would exceed the output buffer capacity,
 * TEXT_JSON_E_LIMIT is returned.
 *
 * @param input Input string (without surrounding quotes)
 * @param input_len Length of input
 * @param output Output buffer
 * @param output_capacity Capacity of output buffer in bytes
 * @param output_len Output: length of decoded string
 * @param pos Input/output: position tracking (can be NULL)
 * @param validate_utf8 Whether to validate UTF-8
 * @param utf8_mode UTF-8 handling mode if validation fails
 * @param allow_unescaped_controls Whether to allow unescaped control characters (0x00-0x1F)
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_decode_string(
    const char* input,
    size_t input_len,
    char* output,
    size_t output_capacity,
    size_t* output_len,
    json_position* pos,
    int validate_utf8,
    json_utf8_mode utf8_mode,
    int allow_unescaped_controls
);

/**
 * @brief Number representation flags
 */
typedef enum {
    JSON_NUMBER_HAS_LEXEME = 1,    ///< Lexeme is preserved
    JSON_NUMBER_HAS_I64 = 2,       ///< int64 representation is valid
    JSON_NUMBER_HAS_U64 = 4,       ///< uint64 representation is valid
    JSON_NUMBER_HAS_DOUBLE = 8,    ///< double representation is valid
    JSON_NUMBER_IS_NONFINITE = 16  ///< Number is NaN, Infinity, or -Infinity
} json_number_flags;

/**
 * @brief Parsed number structure
 *
 * Holds all representations of a parsed number along with flags
 * indicating which representations are valid.
 *
 * This is a temporary parsing structure used internally. When the lexeme
 * is preserved (via preserve_number_lexeme option), memory is allocated
 * with malloc() and must be freed using json_number_destroy().
 *
 * Note: This structure is separate from text_json_value.as.number which
 * uses arena allocation and is automatically cleaned up via text_json_free().
 * When converting from json_number to text_json_value, the data should
 * be copied into the arena and json_number_destroy() should be called
 * to free the temporary structure.
 */
typedef struct {
    char* lexeme;                  ///< Original number lexeme (allocated with malloc)
    size_t lexeme_len;             ///< Length of lexeme
    int64_t i64;                   ///< int64 representation
    uint64_t u64;                  ///< uint64 representation
    double dbl;                    ///< double representation
    unsigned int flags;            ///< Flags indicating valid representations
} json_number;

/**
 * @brief Parse a JSON number token
 *
 * This function parses a JSON number token, performing:
 * - Syntax validation (reject invalid forms like 01, 1., .1, etc.)
 * - Lexeme preservation (store exact token text)
 * - int64 detection and parsing with overflow detection
 * - uint64 detection and parsing
 * - double derivation using strtod
 * - Nonfinite number support (NaN, Infinity, -Infinity) when enabled
 *
 * The function validates the number format according to RFC 8259.
 * Invalid formats are rejected with TEXT_JSON_E_BAD_NUMBER.
 *
 * @param input Input string containing the number
 * @param input_len Length of input
 * @param num Output: parsed number structure
 * @param pos Input/output: position tracking (can be NULL)
 * @param opts Parse options (for nonfinite numbers, etc.)
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_parse_number(
    const char* input,
    size_t input_len,
    json_number* num,
    json_position* pos,
    const text_json_parse_options* opts
);

/**
 * @brief Free resources allocated by json_parse_number()
 *
 * This function frees the lexeme string allocated by json_parse_number()
 * when preserve_number_lexeme was enabled. It should be called when a
 * json_number structure is no longer needed to prevent memory leaks.
 *
 * **When to call:**
 * - After using json_parse_number() standalone (not converting to text_json_value)
 * - After converting json_number data to text_json_value (before discarding json_number)
 * - On error paths where json_parse_number() succeeded but subsequent operations failed
 *
 * **When NOT to call:**
 * - If json_number data has been copied into text_json_value (which uses arena allocation)
 *   In that case, text_json_free() on the root value will clean up all memory.
 *
 * After calling this function, the json_number structure should be considered
 * invalid and should not be used unless json_parse_number() is called again.
 *
 * @param num Number structure to clean up (can be NULL, safe to call multiple times)
 */
void json_number_destroy(json_number* num);

/**
 * @brief JSON token types
 */
typedef enum {
    JSON_TOKEN_EOF,          ///< End of input
    JSON_TOKEN_ERROR,        ///< Error token
    JSON_TOKEN_LBRACE,       ///< {
    JSON_TOKEN_RBRACE,       ///< }
    JSON_TOKEN_LBRACKET,     ///< [
    JSON_TOKEN_RBRACKET,     ///< ]
    JSON_TOKEN_COLON,        ///< :
    JSON_TOKEN_COMMA,        ///< ,
    JSON_TOKEN_NULL,         ///< null keyword
    JSON_TOKEN_TRUE,         ///< true keyword
    JSON_TOKEN_FALSE,        ///< false keyword
    JSON_TOKEN_STRING,       ///< String value
    JSON_TOKEN_NUMBER,       ///< Number value
    JSON_TOKEN_NAN,          ///< NaN (extension)
    JSON_TOKEN_INFINITY,     ///< Infinity (extension)
    JSON_TOKEN_NEG_INFINITY  ///< -Infinity (extension)
} json_token_type;

/**
 * @brief JSON token structure
 *
 * Represents a single token from the lexer, including its type,
 * position information, and value data.
 */
typedef struct {
    json_token_type type;        ///< Token type
    json_position pos;           ///< Position where token starts
    size_t length;               ///< Length of token in input (bytes)

    // Value data (only valid for certain token types)
    union {
        struct {
            char* value;         ///< Decoded string value (allocated, caller must free)
            size_t value_len;    ///< Length of decoded string
        } string;
        json_number number;      ///< Parsed number (temporary, use json_number_destroy)
    } data;
} json_token;

/**
 * @brief JSON lexer structure
 *
 * Internal lexer state for tokenizing JSON input.
 */
typedef struct {
    const char* input;           ///< Input buffer
    size_t input_len;            ///< Total input length
    size_t current_offset;      ///< Current position in input
    json_position pos;           ///< Current position (offset, line, col)
    const text_json_parse_options* opts; ///< Parse options
} json_lexer;

/**
 * @brief Initialize a lexer
 *
 * @param lexer Lexer structure to initialize
 * @param input Input buffer (must remain valid for lexer lifetime)
 * @param input_len Length of input buffer
 * @param opts Parse options (can be NULL for defaults)
 * @return TEXT_JSON_OK on success
 */
text_json_status json_lexer_init(
    json_lexer* lexer,
    const char* input,
    size_t input_len,
    const text_json_parse_options* opts
);

/**
 * @brief Get the next token from the lexer
 *
 * Advances the lexer position and returns the next token.
 * The token's data (string value, number) must be cleaned up
 * by the caller using json_token_cleanup().
 *
 * @param lexer Lexer instance
 * @param token Output token structure
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_lexer_next(json_lexer* lexer, json_token* token);

/**
 * @brief Clean up resources allocated by a token
 *
 * Frees any memory allocated for token data (string values,
 * number lexemes). Should be called after processing a token.
 *
 * @param token Token to clean up
 */
void json_token_cleanup(json_token* token);

// Forward declaration
typedef struct json_context json_context;

// Internal structure definition for text_json_value
// This is needed by the parser to manipulate arrays and objects
struct text_json_value {
    text_json_type type;              ///< Type of this value
    json_context* ctx;                ///< Context (arena) for this value tree

    union {
        int boolean;                  ///< For TEXT_JSON_BOOL
        struct {
            char* data;               ///< String data (null-terminated)
            size_t len;               ///< String length in bytes
        } string;                     ///< For TEXT_JSON_STRING
        struct {
            char* lexeme;             ///< Original number lexeme
            size_t lexeme_len;        ///< Length of lexeme
            int64_t i64;              ///< int64 representation (if valid)
            uint64_t u64;             ///< uint64 representation (if valid)
            double dbl;               ///< double representation (if valid)
            int has_i64;              ///< 1 if i64 is valid
            int has_u64;              ///< 1 if u64 is valid
            int has_dbl;              ///< 1 if dbl is valid
        } number;                     ///< For TEXT_JSON_NUMBER
        struct {
            text_json_value** elems;  ///< Array of value pointers
            size_t count;             ///< Number of elements
            size_t capacity;          ///< Allocated capacity
        } array;                      ///< For TEXT_JSON_ARRAY
        struct {
            struct {
                char* key;            ///< Object key
                size_t key_len;       ///< Key length
                text_json_value* value; ///< Object value
            }* pairs;                 ///< Array of key-value pairs
            size_t count;             ///< Number of pairs
            size_t capacity;          ///< Allocated capacity
        } object;                     ///< For TEXT_JSON_OBJECT
    } as;
};

/**
 * @brief Create a JSON value using an existing context
 *
 * Internal function for parser use. Creates a value that shares
 * the same context (arena) as other values in the parse tree.
 *
 * @param type Type of value to create
 * @param ctx Existing context to use
 * @return New value, or NULL on failure
 */
text_json_value* json_value_new_with_existing_context(text_json_type type, json_context* ctx);

/**
 * @brief Allocate memory from a context's arena
 *
 * Internal function for parser use. Allocates memory from the
 * arena associated with a context.
 *
 * @param ctx Context containing the arena
 * @param size Size in bytes to allocate
 * @param align Alignment requirement (must be power of 2)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* json_arena_alloc_for_context(json_context* ctx, size_t size, size_t align);

/**
 * @brief Add an element to a JSON array
 *
 * Internal function for parser use. Adds an element to an array,
 * growing the array if necessary.
 *
 * @param array Array value (must be TEXT_JSON_ARRAY type)
 * @param element Element value to add
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_array_add_element(text_json_value* array, text_json_value* element);

/**
 * @brief Add a key-value pair to a JSON object
 *
 * Internal function for parser use. Adds a key-value pair to an object,
 * growing the object if necessary.
 *
 * @param object Object value (must be TEXT_JSON_OBJECT type)
 * @param key Key string (will be copied into arena)
 * @param key_len Length of key string
 * @param value Value to associate with key
 * @return TEXT_JSON_OK on success, error code on failure
 */
text_json_status json_object_add_pair(
    text_json_value* object,
    const char* key,
    size_t key_len,
    text_json_value* value
);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_TEXT_JSON_INTERNAL_H */
