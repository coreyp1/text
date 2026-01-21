/**
 * @file json_lexer.c
 * @brief JSON lexer implementation
 *
 * Tokenizes JSON input into tokens including punctuation, keywords,
 * strings, numbers, and comments (when enabled).
 */

#include "json_internal.h"
#include <ghoti.io/text/json/json_core.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

// Safely update column position, checking for overflow
static void json_lexer_update_col(json_position* pos, size_t increment) {
    if (increment > (size_t)INT_MAX || pos->col > INT_MAX - (int)increment) {
        // Overflow would occur - clamp to INT_MAX
        pos->col = INT_MAX;
    } else {
        pos->col += (int)increment;
    }
}

// Safely increment line number, checking for overflow
static void json_lexer_increment_line(json_position* pos) {
    if (pos->line < INT_MAX) {
        pos->line++;
    }
    // If already at INT_MAX, don't increment (avoid overflow)
}

// Skip whitespace characters
static void json_lexer_skip_whitespace(json_lexer* lexer) {
    while (lexer->current_offset < lexer->input_len) {
        char c = lexer->input[lexer->current_offset];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (c == '\n') {
                json_lexer_increment_line(&lexer->pos);
                lexer->pos.col = 1;
            } else {
                json_lexer_update_col(&lexer->pos, 1);
            }
            lexer->pos.offset = ++lexer->current_offset;
        } else {
            break;
        }
    }
}

// Skip a single-line comment (//). Returns 1 if comment was skipped, 0 if not a comment.
static int json_lexer_skip_single_line_comment(json_lexer* lexer) {
    // Check for overflow and sufficient length
    if (lexer->current_offset > lexer->input_len - 2 || lexer->input_len < 2) {
        return 0;
    }
    if (lexer->input[lexer->current_offset] == '/' &&
        lexer->input[lexer->current_offset + 1] == '/') {
        // Skip to end of line or end of input
        while (lexer->current_offset < lexer->input_len) {
            if (lexer->input[lexer->current_offset] == '\n') {
                json_lexer_increment_line(&lexer->pos);
                lexer->pos.col = 1;
                lexer->pos.offset = ++lexer->current_offset;
                return 1;
            }
            lexer->current_offset++;
        }
        lexer->pos.offset = lexer->current_offset;
        return 1;
    }
    return 0;
}

// Skip a multi-line comment of the form /* ... */. Returns 1 if skipped, 0 if not a comment, -1 on error (unclosed comment).
static int json_lexer_skip_multi_line_comment(json_lexer* lexer) {
    // Check for overflow and sufficient length
    if (lexer->current_offset > lexer->input_len - 2 || lexer->input_len < 2) {
        return 0;
    }
    if (lexer->input[lexer->current_offset] == '/' &&
        lexer->input[lexer->current_offset + 1] == '*') {
        // Skip /* and look for */
        lexer->current_offset += 2;
        // Check for overflow: current_offset + 1 could overflow
        while (lexer->current_offset < lexer->input_len - 1) {
            if (lexer->input[lexer->current_offset] == '*' &&
                lexer->input[lexer->current_offset + 1] == '/') {
                // Found closing */
                lexer->current_offset += 2;
                lexer->pos.offset = lexer->current_offset;
                // Update line/col if needed (we don't track precisely in comments)
                return 1;
            }
            if (lexer->input[lexer->current_offset] == '\n') {
                json_lexer_increment_line(&lexer->pos);
                lexer->pos.col = 1;
            } else {
                json_lexer_update_col(&lexer->pos, 1);
            }
            lexer->current_offset++;
        }
        // Unclosed comment
        return -1;
    }
    return 0;
}

// Skip comments if enabled
static text_json_status json_lexer_skip_comments(json_lexer* lexer) {
    if (!lexer->opts || !lexer->opts->allow_comments) {
        return TEXT_JSON_OK;
    }

    int skipped;
    do {
        skipped = 0;
        // Try single-line comment
        if (json_lexer_skip_single_line_comment(lexer)) {
            skipped = 1;
        }
        // Try multi-line comment
        int multi_result = json_lexer_skip_multi_line_comment(lexer);
        if (multi_result == 1) {
            skipped = 1;
        } else if (multi_result == -1) {
            return TEXT_JSON_E_BAD_TOKEN;  // Unclosed comment
        }
        // Skip whitespace after comments
        if (skipped) {
            json_lexer_skip_whitespace(lexer);
        }
    } while (skipped);

    return TEXT_JSON_OK;
}

// Check if character is a valid identifier start (for keywords)
static int json_is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Check if character is a valid identifier continuation
static int json_is_identifier_cont(char c) {
    return json_is_identifier_start(c) || (c >= '0' && c <= '9');
}

// Try to match a keyword or extension token. Returns 1 if matched, 0 if not.
static int json_lexer_match_keyword(json_lexer* lexer, json_token* token) {
    size_t start = lexer->current_offset;
    size_t len = 0;

    // Read identifier
    if (start >= lexer->input_len || !json_is_identifier_start(lexer->input[start])) {
        return 0;
    }

    // Check for overflow: start + len could overflow size_t
    while (len < lexer->input_len - start &&
           json_is_identifier_cont(lexer->input[start + len])) {
        len++;
    }

    if (len == 0) {
        return 0;
    }

    const char* keyword_start = lexer->input + start;

    // Check for standard keywords
    if (json_matches(keyword_start, len, "null")) {
        token->type = JSON_TOKEN_NULL;
        token->pos = lexer->pos;
        token->length = len;
        lexer->current_offset = start + len;
        lexer->pos.offset = lexer->current_offset;
        json_lexer_update_col(&lexer->pos, len);
        return 1;
    }
    if (json_matches(keyword_start, len, "true")) {
        token->type = JSON_TOKEN_TRUE;
        token->pos = lexer->pos;
        token->length = len;
        lexer->current_offset = start + len;
        lexer->pos.offset = lexer->current_offset;
        json_lexer_update_col(&lexer->pos, len);
        return 1;
    }
    if (json_matches(keyword_start, len, "false")) {
        token->type = JSON_TOKEN_FALSE;
        token->pos = lexer->pos;
        token->length = len;
        lexer->current_offset = start + len;
        lexer->pos.offset = lexer->current_offset;
        json_lexer_update_col(&lexer->pos, len);
        return 1;
    }

    // Check for extension tokens (if enabled)
    // Note: "Infinity" is checked here because it starts with an identifier character.
    // "-Infinity" is checked separately in the number parsing path (see
    // json_lexer_match_neg_infinity()) because it starts with '-'.
    // Both are gated behind allow_nonfinite_numbers.
    if (lexer->opts && lexer->opts->allow_nonfinite_numbers) {
        if (json_matches(keyword_start, len, "NaN")) {
            token->type = JSON_TOKEN_NAN;
            token->pos = lexer->pos;
            token->length = len;
            lexer->current_offset = start + len;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, len);
            return 1;
        }
        if (json_matches(keyword_start, len, "Infinity")) {
            token->type = JSON_TOKEN_INFINITY;
            token->pos = lexer->pos;
            token->length = len;
            lexer->current_offset = start + len;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, len);
            return 1;
        }
    }

    return 0;
}

// Try to match -Infinity (special case, starts with -).
// This is separate from the "Infinity" check in json_lexer_match_keyword()
// because "-Infinity" starts with a minus sign, which is not an identifier
// character. Therefore it cannot be matched by the keyword matcher and must
// be handled in the number parsing path. Both are gated behind the same
// option: allow_nonfinite_numbers.
// Returns 1 if matched, 0 if not.
static int json_lexer_match_neg_infinity(json_lexer* lexer, json_token* token) {
    // Gate behind same option as "Infinity" keyword
    if (!lexer->opts || !lexer->opts->allow_nonfinite_numbers) {
        return 0;
    }

    size_t start = lexer->current_offset;
    // Check for overflow and sufficient length
    if (start > lexer->input_len - 9 || lexer->input_len < 9) {  // "-Infinity" is 9 chars
        return 0;
    }

    if (lexer->input[start] == '-' &&
        json_matches(lexer->input + start + 1, 8, "Infinity")) {
        token->type = JSON_TOKEN_NEG_INFINITY;
        token->pos = lexer->pos;
        token->length = 9;
        lexer->current_offset = start + 9;
        lexer->pos.offset = lexer->current_offset;
        json_lexer_update_col(&lexer->pos, 9);
        return 1;
    }

    return 0;
}

// Parse a string token
static text_json_status json_lexer_parse_string(json_lexer* lexer, json_token* token) {
    size_t start = lexer->current_offset;
    char quote_char = lexer->input[start];

    // Check for single quotes (if enabled)
    int allow_single = lexer->opts && lexer->opts->allow_single_quotes;
    if (quote_char != '"' && (!allow_single || quote_char != '\'')) {
        return TEXT_JSON_E_BAD_TOKEN;
    }

    size_t string_start = start + 1;  // After opening quote
    size_t string_end = string_start;
    int escaped = 0;

    // Find closing quote
    while (string_end < lexer->input_len) {
        if (escaped) {
            escaped = 0;
            string_end++;
            continue;
        }
        if (lexer->input[string_end] == '\\') {
            escaped = 1;
            string_end++;
            continue;
        }
        if (lexer->input[string_end] == quote_char) {
            break;  // Found closing quote
        }
        string_end++;
    }

    if (string_end >= lexer->input_len) {
        return TEXT_JSON_E_BAD_TOKEN;  // Unclosed string
    }

    // string_start to string_end is the string content (without quotes)
    size_t string_content_len = string_end - string_start;

    // Check for integer overflow in token_length calculation
    if (string_end + 1 < start) {
        return TEXT_JSON_E_INVALID;  // Underflow (shouldn't happen, but be safe)
    }
    size_t token_length = string_end + 1 - start;  // Include quotes

    // Decode the string
    // Allocate buffer for decoded string (worst case: same size as input)
    // Check for integer overflow in allocation size
    if (string_content_len > SIZE_MAX - 1) {
        return TEXT_JSON_E_LIMIT;  // String too large
    }
    size_t decode_capacity = string_content_len + 1;  // +1 for null terminator
    char* decoded = (char*)malloc(decode_capacity);
    if (!decoded) {
        return TEXT_JSON_E_OOM;
    }

    json_position decode_pos = lexer->pos;
    decode_pos.offset = string_start;
    decode_pos.col++;  // After opening quote

    size_t decoded_len;
    text_json_status status = json_decode_string(
        lexer->input + string_start,
        string_content_len,
        decoded,
        decode_capacity,
        &decoded_len,
        &decode_pos,
        lexer->opts ? lexer->opts->validate_utf8 : 1,
        JSON_UTF8_REJECT,
        lexer->opts ? lexer->opts->allow_unescaped_controls : 0
    );

    if (status != TEXT_JSON_OK) {
        free(decoded);
        return status;
    }

    token->type = JSON_TOKEN_STRING;
    token->pos = lexer->pos;
    token->length = token_length;
    token->data.string.value = decoded;
    token->data.string.value_len = decoded_len;
    token->data.string.original_start = string_start;
    token->data.string.original_len = string_content_len;

    // Update lexer position
    lexer->current_offset = start + token_length;
    lexer->pos.offset = lexer->current_offset;
    json_lexer_update_col(&lexer->pos, token_length);

    return TEXT_JSON_OK;
}

// Parse a number token
static text_json_status json_lexer_parse_number(json_lexer* lexer, json_token* token) {
    size_t start = lexer->current_offset;
    size_t end = start;

    // Determine number end by finding first non-number character
    // Numbers can contain: digits, '.', 'e', 'E', '+', '-'
    int has_dot = 0;
    int has_exp = 0;

    while (end < lexer->input_len) {
        char c = lexer->input[end];
        if (c >= '0' && c <= '9') {
            end++;
            continue;
        }
        if (c == '.' && !has_dot && !has_exp) {
            has_dot = 1;
            end++;
            continue;
        }
        if ((c == 'e' || c == 'E') && !has_exp) {
            has_exp = 1;
            end++;
            // Exponent can have + or -
            if (end < lexer->input_len &&
                (lexer->input[end] == '+' || lexer->input[end] == '-')) {
                end++;
            }
            continue;
        }
        if (c == '-' && end == start) {
            // Leading minus sign
            end++;
            continue;
        }
        // Not part of number
        break;
    }

    if (end == start || (end == start + 1 && lexer->input[start] == '-')) {
        return TEXT_JSON_E_BAD_NUMBER;
    }

    size_t number_len = end - start;

    // Parse the number
    json_position num_pos = lexer->pos;
    text_json_status status = json_parse_number(
        lexer->input + start,
        number_len,
        &token->data.number,
        &num_pos,
        lexer->opts
    );

    if (status != TEXT_JSON_OK) {
        return status;
    }

    token->type = JSON_TOKEN_NUMBER;
    token->pos = lexer->pos;
    token->length = number_len;

    // Update lexer position
    lexer->current_offset = end;
    lexer->pos.offset = lexer->current_offset;
    json_lexer_update_col(&lexer->pos, number_len);

    return TEXT_JSON_OK;
}

TEXT_INTERNAL_API text_json_status json_lexer_init(
    json_lexer* lexer,
    const char* input,
    size_t input_len,
    const text_json_parse_options* opts
) {
    if (!lexer || !input) {
        return TEXT_JSON_E_INVALID;
    }

    lexer->input = input;
    lexer->input_len = input_len;
    lexer->current_offset = 0;
    lexer->pos.offset = 0;
    lexer->pos.line = 1;
    lexer->pos.col = 1;
    lexer->opts = opts;

    // Skip leading BOM if enabled
    if (opts && opts->allow_leading_bom && input_len >= 3 &&
        (unsigned char)input[0] == 0xEF &&
        (unsigned char)input[1] == 0xBB &&
        (unsigned char)input[2] == 0xBF) {
        lexer->current_offset = 3;
        lexer->pos.offset = 3;
        lexer->pos.col = 4;
    }

    return TEXT_JSON_OK;
}

TEXT_INTERNAL_API text_json_status json_lexer_next(json_lexer* lexer, json_token* token) {
    if (!lexer || !token) {
        return TEXT_JSON_E_INVALID;
    }

    // Initialize token
    memset(token, 0, sizeof(*token));
    token->type = JSON_TOKEN_ERROR;

    // Skip whitespace
    json_lexer_skip_whitespace(lexer);

    // Skip comments (if enabled)
    text_json_status status = json_lexer_skip_comments(lexer);
    if (status != TEXT_JSON_OK) {
        return status;
    }

    // Check for EOF
    if (lexer->current_offset >= lexer->input_len) {
        token->type = JSON_TOKEN_EOF;
        token->pos = lexer->pos;
        token->length = 0;
        return TEXT_JSON_OK;
    }

    size_t start = lexer->current_offset;
    char c = lexer->input[start];

    // Punctuation tokens
    switch (c) {
        case '{':
            token->type = JSON_TOKEN_LBRACE;
            token->pos = lexer->pos;
            token->length = 1;
            lexer->current_offset++;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, 1);
            return TEXT_JSON_OK;

        case '}':
            token->type = JSON_TOKEN_RBRACE;
            token->pos = lexer->pos;
            token->length = 1;
            lexer->current_offset++;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, 1);
            return TEXT_JSON_OK;

        case '[':
            token->type = JSON_TOKEN_LBRACKET;
            token->pos = lexer->pos;
            token->length = 1;
            lexer->current_offset++;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, 1);
            return TEXT_JSON_OK;

        case ']':
            token->type = JSON_TOKEN_RBRACKET;
            token->pos = lexer->pos;
            token->length = 1;
            lexer->current_offset++;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, 1);
            return TEXT_JSON_OK;

        case ':':
            token->type = JSON_TOKEN_COLON;
            token->pos = lexer->pos;
            token->length = 1;
            lexer->current_offset++;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, 1);
            return TEXT_JSON_OK;

        case ',':
            token->type = JSON_TOKEN_COMMA;
            token->pos = lexer->pos;
            token->length = 1;
            lexer->current_offset++;
            lexer->pos.offset = lexer->current_offset;
            json_lexer_update_col(&lexer->pos, 1);
            return TEXT_JSON_OK;
    }

    // String tokens
    if (c == '"' || (lexer->opts && lexer->opts->allow_single_quotes && c == '\'')) {
        return json_lexer_parse_string(lexer, token);
    }

    // Number tokens (including -Infinity special case)
    // Note: -Infinity is checked here because it starts with '-', which is
    // also the start of negative numbers. It's gated behind allow_nonfinite_numbers
    // (same as "Infinity" in the keyword path).
    if (c == '-' || (c >= '0' && c <= '9')) {
        // Check for -Infinity first (if enabled)
        if (c == '-' && json_lexer_match_neg_infinity(lexer, token)) {
            return TEXT_JSON_OK;
        }
        return json_lexer_parse_number(lexer, token);
    }

    // Keyword tokens (true, false, null, NaN, Infinity)
    if (json_lexer_match_keyword(lexer, token)) {
        return TEXT_JSON_OK;
    }

    // Unknown token
    token->type = JSON_TOKEN_ERROR;
    token->pos = lexer->pos;
    token->length = 1;
    return TEXT_JSON_E_BAD_TOKEN;
}

TEXT_INTERNAL_API void json_token_cleanup(json_token* token) {
    if (!token) {
        return;
    }

    switch (token->type) {
        case JSON_TOKEN_STRING:
            if (token->data.string.value) {
                free(token->data.string.value);
                token->data.string.value = NULL;
                token->data.string.value_len = 0;
            }
            break;

        case JSON_TOKEN_NUMBER:
            json_number_destroy(&token->data.number);
            break;

        default:
            break;
    }
}
