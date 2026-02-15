/**
 * @file
 *
 * JSON lexer implementation.
 *
 * Tokenizes JSON input into tokens including punctuation, keywords,
 * strings, numbers, and comments (when enabled).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_internal.h"
#include "json_stream_internal.h"

#include <ghoti.io/text/json/json_core.h>
// Skip whitespace characters
static void json_lexer_skip_whitespace(json_lexer * lexer) {
  while (lexer->current_offset < lexer->input_len) {
    // Defensive bounds check before buffer access
    if (!json_check_bounds_offset(lexer->current_offset, lexer->input_len)) {
      break;
    }
    char c = lexer->input[lexer->current_offset];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (c == '\n') {
        json_position_increment_line(&lexer->pos);
        lexer->pos.col = 1;
      }
      else {
        json_position_update_column(&lexer->pos, 1);
      }
      // Check for overflow before incrementing
      if (json_check_add_overflow(lexer->current_offset, 1)) {
        // Overflow - saturate at SIZE_MAX
        lexer->current_offset = SIZE_MAX;
        json_position_update_offset(&lexer->pos, 1);
        break;
      }
      lexer->current_offset++;
      json_position_update_offset(&lexer->pos, 1);
    }
    else {
      break;
    }
  }
}

// Skip a single-line comment (//). Returns 1 if comment was skipped, 0 if not a
// comment.
static int json_lexer_skip_single_line_comment(json_lexer * lexer) {
  // Check for underflow and sufficient length using shared helper
  if (lexer->input_len < 2 || json_check_sub_underflow(lexer->input_len, 2) ||
      lexer->current_offset > lexer->input_len - 2) {
    return 0;
  }
  // Defensive bounds checks before buffer access
  if (!json_check_bounds_offset(lexer->current_offset, lexer->input_len) ||
      !json_check_bounds_offset(lexer->current_offset + 1, lexer->input_len)) {
    return 0;
  }
  if (lexer->input[lexer->current_offset] == '/' &&
      lexer->input[lexer->current_offset + 1] == '/') {
    // Skip to end of line or end of input
    while (lexer->current_offset < lexer->input_len) {
      // Defensive bounds check before buffer access
      if (!json_check_bounds_offset(lexer->current_offset, lexer->input_len)) {
        break;
      }
      if (lexer->input[lexer->current_offset] == '\n') {
        json_position_increment_line(&lexer->pos);
        lexer->pos.col = 1;
        // Check for overflow before incrementing
        if (json_check_add_overflow(lexer->current_offset, 1)) {
          // Overflow - saturate at SIZE_MAX
          lexer->current_offset = SIZE_MAX;
          json_position_update_offset(&lexer->pos, 1);
          return 1;
        }
        lexer->current_offset++;
        json_position_update_offset(&lexer->pos, 1);
        return 1;
      }
      // Check for overflow before incrementing
      if (json_check_add_overflow(lexer->current_offset, 1)) {
        // Overflow - saturate at SIZE_MAX and break
        lexer->current_offset = SIZE_MAX;
        break;
      }
      lexer->current_offset++;
      json_position_update_offset(&lexer->pos, 1);
    }
    // Update position offset to match current_offset
    lexer->pos.offset = lexer->current_offset;
    return 1;
  }
  return 0;
}

// Skip a multi-line comment of the form /* ... */. Returns 1 if skipped, 0 if
// not a comment, -1 on error (unclosed comment).
static int json_lexer_skip_multi_line_comment(json_lexer * lexer) {
  // Check for underflow and sufficient length using shared helper
  if (lexer->input_len < 2 || json_check_sub_underflow(lexer->input_len, 2) ||
      lexer->current_offset > lexer->input_len - 2) {
    return 0;
  }
  // Defensive bounds checks before buffer access
  if (!json_check_bounds_offset(lexer->current_offset, lexer->input_len) ||
      !json_check_bounds_offset(lexer->current_offset + 1, lexer->input_len)) {
    return 0;
  }
  if (lexer->input[lexer->current_offset] == '/' &&
      lexer->input[lexer->current_offset + 1] == '*') {
    // Skip /* and look for */
    // Check for overflow before adding 2
    if (json_check_add_overflow(lexer->current_offset, 2)) {
      // Overflow - saturate at SIZE_MAX
      lexer->current_offset = SIZE_MAX;
      return -1; // Error: unclosed comment
    }
    lexer->current_offset += 2;
    json_position_update_offset(&lexer->pos, 2);
    // Check for underflow: current_offset + 1 could overflow
    while (lexer->current_offset < lexer->input_len) {
      // Defensive bounds check before buffer access
      if (!json_check_bounds_offset(lexer->current_offset, lexer->input_len)) {
        break;
      }
      // Check if we have at least 2 bytes remaining
      if (json_check_sub_underflow(lexer->input_len, lexer->current_offset) ||
          lexer->input_len - lexer->current_offset < 2) {
        break; // Not enough bytes for "*/"
      }
      // Defensive bounds check for current_offset + 1
      if (!json_check_bounds_offset(
              lexer->current_offset + 1, lexer->input_len)) {
        break;
      }
      if (lexer->input[lexer->current_offset] == '*' &&
          lexer->input[lexer->current_offset + 1] == '/') {
        // Found closing */
        // Check for overflow before adding 2
        if (json_check_add_overflow(lexer->current_offset, 2)) {
          // Overflow - saturate at SIZE_MAX
          lexer->current_offset = SIZE_MAX;
          json_position_update_offset(&lexer->pos, 2);
          return 1;
        }
        lexer->current_offset += 2;
        json_position_update_offset(&lexer->pos, 2);
        // Update line/col if needed (we don't track precisely in comments)
        return 1;
      }
      // Defensive bounds check before buffer access
      if (!json_check_bounds_offset(lexer->current_offset, lexer->input_len)) {
        break;
      }
      if (lexer->input[lexer->current_offset] == '\n') {
        json_position_increment_line(&lexer->pos);
        lexer->pos.col = 1;
      }
      else {
        json_position_update_column(&lexer->pos, 1);
      }
      // Check for overflow before incrementing
      if (json_check_add_overflow(lexer->current_offset, 1)) {
        // Overflow - saturate at SIZE_MAX and break
        lexer->current_offset = SIZE_MAX;
        break;
      }
      lexer->current_offset++;
      json_position_update_offset(&lexer->pos, 1);
    }
    // Unclosed comment
    return -1;
  }
  return 0;
}

// Skip comments if enabled
static GTEXT_JSON_Status json_lexer_skip_comments(json_lexer * lexer) {
  if (!lexer->opts || !lexer->opts->allow_comments) {
    return GTEXT_JSON_OK;
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
    }
    else if (multi_result == -1) {
      return GTEXT_JSON_E_BAD_TOKEN; // Unclosed comment
    }
    // Skip whitespace after comments
    if (skipped) {
      json_lexer_skip_whitespace(lexer);
    }
  } while (skipped);

  return GTEXT_JSON_OK;
}

// Check if character is a valid identifier start (for keywords)
static int json_is_identifier_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Check if character is a valid identifier continuation
static int json_is_identifier_cont(char c) {
  return json_is_identifier_start(c) || (c >= '0' && c <= '9');
}

// Check if a partial identifier could be a prefix of a JSON keyword
// Returns 1 if it's a valid prefix, 0 if not
// Note: This function doesn't check for NaN/Infinity prefixes because those
// are extension keywords that depend on allow_nonfinite_numbers option.
// The lexer handles those separately in json_lexer_match_keyword().
static int json_is_keyword_prefix(const char * str, size_t len) {
  if (len == 0) {
    return 0;
  }

  // Check prefixes of standard keywords
  if (str[0] == 't') {
    // Could be "true"
    if (len == 1)
      return 1; // "t"
    if (len == 2 && str[1] == 'r')
      return 1; // "tr"
    if (len == 3 && str[1] == 'r' && str[2] == 'u')
      return 1; // "tru"
    if (len == 4 && str[1] == 'r' && str[2] == 'u' && str[3] == 'e')
      return 1; // "true" (complete)
    return 0;
  }
  if (str[0] == 'f') {
    // Could be "false"
    if (len == 1)
      return 1; // "f"
    if (len == 2 && str[1] == 'a')
      return 1; // "fa"
    if (len == 3 && str[1] == 'a' && str[2] == 'l')
      return 1; // "fal"
    if (len == 4 && str[1] == 'a' && str[2] == 'l' && str[3] == 's')
      return 1; // "fals"
    if (len == 5 && str[1] == 'a' && str[2] == 'l' && str[3] == 's' &&
        str[4] == 'e')
      return 1; // "false" (complete)
    return 0;
  }
  if (str[0] == 'n') {
    // Could be "null"
    if (len == 1)
      return 1; // "n"
    if (len == 2 && str[1] == 'u')
      return 1; // "nu"
    if (len == 3 && str[1] == 'u' && str[2] == 'l')
      return 1; // "nul"
    if (len == 4 && str[1] == 'u' && str[2] == 'l' && str[3] == 'l')
      return 1; // "null" (complete)
    return 0;
  }
  if (str[0] == 'N') {
    // Could be "NaN" (if extensions enabled)
    if (len == 1)
      return 1; // "N"
    if (len == 2 && str[1] == 'a')
      return 1; // "Na"
    if (len == 3 && str[1] == 'a' && str[2] == 'N')
      return 1; // "NaN" (complete)
    return 0;
  }
  if (str[0] == 'I') {
    // Could be "Infinity" (if extensions enabled)
    if (len == 1)
      return 1; // "I"
    if (len == 2 && str[1] == 'n')
      return 1; // "In"
    if (len == 3 && str[1] == 'n' && str[2] == 'f')
      return 1; // "Inf"
    if (len == 4 && str[1] == 'n' && str[2] == 'f' && str[3] == 'i')
      return 1; // "Infi"
    if (len == 5 && str[1] == 'n' && str[2] == 'f' && str[3] == 'i' &&
        str[4] == 'n')
      return 1; // "Infin"
    if (len == 6 && str[1] == 'n' && str[2] == 'f' && str[3] == 'i' &&
        str[4] == 'n' && str[5] == 'i')
      return 1; // "Infini"
    if (len == 7 && str[1] == 'n' && str[2] == 'f' && str[3] == 'i' &&
        str[4] == 'n' && str[5] == 'i' && str[6] == 't')
      return 1; // "Infinit"
    if (len == 8 && str[1] == 'n' && str[2] == 'f' && str[3] == 'i' &&
        str[4] == 'n' && str[5] == 'i' && str[6] == 't' && str[7] == 'y')
      return 1; // "Infinity" (complete)
    return 0;
  }

  return 0;
}

// Try to match a keyword or extension token. Returns 1 if matched, 0 if not.
static int json_lexer_match_keyword(json_lexer * lexer, json_token * token) {
  size_t start = lexer->current_offset;
  size_t len = 0;

  // Read identifier
  // Defensive bounds check before buffer access
  if (start >= lexer->input_len ||
      !json_check_bounds_offset(start, lexer->input_len)) {
    return 0;
  }
  if (!json_is_identifier_start(lexer->input[start])) {
    return 0;
  }

  // Check for underflow in subtraction and overflow in addition
  while (len < lexer->input_len) {
    // Check for underflow: input_len - start
    if (json_check_sub_underflow(lexer->input_len, start) ||
        len >= lexer->input_len - start) {
      break;
    }
    // Check for overflow: start + len
    if (json_check_add_overflow(start, len)) {
      break;
    }
    // Defensive bounds check before buffer access
    if (!json_check_bounds_offset(start + len, lexer->input_len)) {
      break;
    }
    if (!json_is_identifier_cont(lexer->input[start + len])) {
      break;
    }
    // Check for overflow before incrementing len
    if (json_check_add_overflow(len, 1)) {
      break;
    }
    len++;
  }

  if (len == 0) {
    return 0;
  }

  const char * keyword_start = lexer->input + start;

  // Check for standard keywords
  if (json_matches(keyword_start, len, "null")) {
    token->type = JSON_TOKEN_NULL;
    token->pos = lexer->pos;
    token->length = len;
    // Check for overflow before adding start + len
    if (json_check_add_overflow(start, len)) {
      // Overflow - saturate at SIZE_MAX
      lexer->current_offset = SIZE_MAX;
      lexer->pos.offset = SIZE_MAX;
    }
    else {
      lexer->current_offset = start + len;
      lexer->pos.offset = lexer->current_offset;
    }
    json_position_update_column(&lexer->pos, len);
    return 1;
  }
  if (json_matches(keyword_start, len, "true")) {
    token->type = JSON_TOKEN_TRUE;
    token->pos = lexer->pos;
    token->length = len;
    // Check for overflow before adding start + len
    if (json_check_add_overflow(start, len)) {
      // Overflow - saturate at SIZE_MAX
      lexer->current_offset = SIZE_MAX;
      lexer->pos.offset = SIZE_MAX;
    }
    else {
      lexer->current_offset = start + len;
      lexer->pos.offset = lexer->current_offset;
    }
    json_position_update_column(&lexer->pos, len);
    return 1;
  }
  if (json_matches(keyword_start, len, "false")) {
    token->type = JSON_TOKEN_FALSE;
    token->pos = lexer->pos;
    token->length = len;
    // Check for overflow before adding start + len
    if (json_check_add_overflow(start, len)) {
      // Overflow - saturate at SIZE_MAX
      lexer->current_offset = SIZE_MAX;
      lexer->pos.offset = SIZE_MAX;
    }
    else {
      lexer->current_offset = start + len;
      lexer->pos.offset = lexer->current_offset;
    }
    json_position_update_column(&lexer->pos, len);
    return 1;
  }

  // Check for extension tokens (NaN, Infinity)
  // Note: "Infinity" is checked here because it starts with an identifier
  // character.
  // "-Infinity" is checked separately in the number parsing path (see
  // json_lexer_match_neg_infinity()) because it starts with '-'.
  // Always check for these tokens, but return error if not allowed
  if (json_matches(keyword_start, len, "NaN")) {
    if (lexer->opts && lexer->opts->allow_nonfinite_numbers) {
      token->type = JSON_TOKEN_NAN;
      token->pos = lexer->pos;
      token->length = len;
      // Check for overflow before adding start + len
      if (json_check_add_overflow(start, len)) {
        // Overflow - saturate at SIZE_MAX
        lexer->current_offset = SIZE_MAX;
        lexer->pos.offset = SIZE_MAX;
      }
      else {
        lexer->current_offset = start + len;
        lexer->pos.offset = lexer->current_offset;
      }
      json_position_update_column(&lexer->pos, len);
      return 1;
    }
    else {
      // NaN not allowed - return error
      token->type = JSON_TOKEN_ERROR;
      token->pos = lexer->pos;
      token->length = len;
      // Check for overflow before adding start + len
      if (json_check_add_overflow(start, len)) {
        // Overflow - saturate at SIZE_MAX
        lexer->current_offset = SIZE_MAX;
        lexer->pos.offset = SIZE_MAX;
      }
      else {
        lexer->current_offset = start + len;
        lexer->pos.offset = lexer->current_offset;
      }
      json_position_update_column(&lexer->pos, len);
      return GTEXT_JSON_E_NONFINITE;
    }
  }
  if (json_matches(keyword_start, len, "Infinity")) {
    if (lexer->opts && lexer->opts->allow_nonfinite_numbers) {
      token->type = JSON_TOKEN_INFINITY;
      token->pos = lexer->pos;
      token->length = len;
      // Check for overflow before adding start + len
      if (json_check_add_overflow(start, len)) {
        // Overflow - saturate at SIZE_MAX
        lexer->current_offset = SIZE_MAX;
        lexer->pos.offset = SIZE_MAX;
      }
      else {
        lexer->current_offset = start + len;
        lexer->pos.offset = lexer->current_offset;
      }
      json_position_update_column(&lexer->pos, len);
      return 1;
    }
    else {
      // Infinity not allowed - return error
      token->type = JSON_TOKEN_ERROR;
      token->pos = lexer->pos;
      token->length = len;
      // Check for overflow before adding start + len
      if (json_check_add_overflow(start, len)) {
        // Overflow - saturate at SIZE_MAX
        lexer->current_offset = SIZE_MAX;
        lexer->pos.offset = SIZE_MAX;
      }
      else {
        lexer->current_offset = start + len;
        lexer->pos.offset = lexer->current_offset;
      }
      json_position_update_column(&lexer->pos, len);
      return GTEXT_JSON_E_NONFINITE;
    }
  }

  return 0;
}

// Try to match -Infinity (special case, starts with -).
// This is separate from the "Infinity" check in json_lexer_match_keyword()
// because "-Infinity" starts with a minus sign, which is not an identifier
// character. Therefore it cannot be matched by the keyword matcher and must
// be handled in the number parsing path.
// Returns 1 if matched and allowed, GTEXT_JSON_E_NONFINITE if matched but not
// allowed, 0 if not matched.
static int json_lexer_match_neg_infinity(
    json_lexer * lexer, json_token * token) {
  // Always check for -Infinity, but return error if not allowed

  size_t start = lexer->current_offset;
  // Check for underflow and sufficient length using shared helper
  if (lexer->input_len < 9 || json_check_sub_underflow(lexer->input_len, 9) ||
      start > lexer->input_len - 9) { // "-Infinity" is 9 chars
    return 0;
  }
  // Defensive bounds checks before buffer access
  if (!json_check_bounds_offset(start, lexer->input_len) ||
      !json_check_bounds_offset(start + 1, lexer->input_len)) {
    return 0;
  }

  if (lexer->input[start] == '-' &&
      json_matches(lexer->input + start + 1, 8, "Infinity")) {
    if (lexer->opts && lexer->opts->allow_nonfinite_numbers) {
      token->type = JSON_TOKEN_NEG_INFINITY;
      token->pos = lexer->pos;
      token->length = 9;
      // Check for overflow before adding start + 9
      if (json_check_add_overflow(start, 9)) {
        // Overflow - saturate at SIZE_MAX
        lexer->current_offset = SIZE_MAX;
        lexer->pos.offset = SIZE_MAX;
      }
      else {
        lexer->current_offset = start + 9;
        lexer->pos.offset = lexer->current_offset;
      }
      json_position_update_column(&lexer->pos, 9);
      return 1;
    }
    else {
      // -Infinity not allowed - return error
      token->type = JSON_TOKEN_ERROR;
      token->pos = lexer->pos;
      token->length = 9;
      // Check for overflow before adding start + 9
      if (json_check_add_overflow(start, 9)) {
        // Overflow - saturate at SIZE_MAX
        lexer->current_offset = SIZE_MAX;
        lexer->pos.offset = SIZE_MAX;
      }
      else {
        lexer->current_offset = start + 9;
        lexer->pos.offset = lexer->current_offset;
      }
      json_position_update_column(&lexer->pos, 9);
      return GTEXT_JSON_E_NONFINITE;
    }
  }

  return 0;
}

// Parse a string token
static GTEXT_JSON_Status json_lexer_parse_string(
    json_lexer * lexer, json_token * token) {
  json_token_buffer * tb = lexer->token_buffer;
  size_t start = lexer->current_offset;
  char quote_char;
  int resuming = 0;

  // Check if resuming from incomplete string
  if (tb && tb->type == JSON_TOKEN_BUFFER_STRING) {
    resuming = 1;
    // We're resuming - the opening quote and partial content are already in the
    // buffer Determine quote character from buffered data
    if (tb->buffer_used > 0 && tb->buffer[0] == '"') {
      quote_char = '"';
    }
    else if (tb->buffer_used > 0 && tb->buffer[0] == '\'') {
      quote_char = '\'';
    }
    else {
      // Invalid state - buffer should have opening quote
      json_token_buffer_clear(tb);
      return GTEXT_JSON_E_BAD_TOKEN;
    }
    // When resuming, we continue parsing from current_offset
    // The buffer already has opening quote and previous content, we'll append
    // new content string_start tracks where we start in the current input chunk
  }
  else {
    // Starting new string
    // Defensive bounds check before buffer access
    if (start >= lexer->input_len ||
        !json_check_bounds_offset(start, lexer->input_len)) {
      return GTEXT_JSON_E_BAD_TOKEN;
    }
    quote_char = lexer->input[start];

    // Check for single quotes (if enabled)
    int allow_single = lexer->opts && lexer->opts->allow_single_quotes;
    if (quote_char != '"' && (!allow_single || quote_char != '\'')) {
      return GTEXT_JSON_E_BAD_TOKEN;
    }

    // Initialize token buffer if available
    if (tb) {
      json_token_buffer_clear(tb);
      tb->type = JSON_TOKEN_BUFFER_STRING;
      tb->start_offset = start;
      // Append opening quote to buffer
      GTEXT_JSON_Status status = json_token_buffer_append(tb, &quote_char, 1);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
    // Check for overflow before incrementing start
    if (json_check_add_overflow(start, 1)) {
      return GTEXT_JSON_E_INVALID; // Overflow
    }
    start++; // Move past opening quote
  }

  size_t string_start = start;
  size_t string_end = string_start;

  // Restore escape sequence state if resuming
  int in_escape = 0;
  int unicode_escape_remaining = 0;
  int high_surrogate_seen = 0;

  if (resuming && tb) {
    in_escape = tb->parse_state.string_state.in_escape;
    unicode_escape_remaining =
        tb->parse_state.string_state.unicode_escape_remaining;
    high_surrogate_seen = tb->parse_state.string_state.high_surrogate_seen;
  }

  // Find closing quote, tracking escape sequences
  while (string_end < lexer->input_len) {
    // Defensive bounds check before buffer access
    if (!json_check_bounds_offset(string_end, lexer->input_len)) {
      break;
    }
    char c = lexer->input[string_end];

    if (unicode_escape_remaining > 0) {
      // Parsing Unicode escape - accumulate hex digits
      if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')) {
        unicode_escape_remaining--;
        string_end++;
        // Append to buffer if available
        if (tb) {
          GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
          if (status != GTEXT_JSON_OK) {
            return status;
          }
        }
        continue;
      }
      else {
        // Invalid hex digit in Unicode escape
        if (tb) {
          json_token_buffer_clear(tb);
        }
        return GTEXT_JSON_E_BAD_UNICODE;
      }
    }

    if (in_escape) {
      in_escape = 0;
      if (c == 'u') {
        // Start of Unicode escape - need 4 more hex digits
        unicode_escape_remaining = 4;
        string_end++;
        // Append to buffer if available
        if (tb) {
          GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
          if (status != GTEXT_JSON_OK) {
            return status;
          }
        }
        continue;
      }
      else {
        // Standard escape character - consume it
        string_end++;
        // Append to buffer if available
        if (tb) {
          GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
          if (status != GTEXT_JSON_OK) {
            return status;
          }
        }
        continue;
      }
    }

    if (c == '\\') {
      in_escape = 1;
      string_end++;
      // Append to buffer if available
      if (tb) {
        GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
      continue;
    }

    if (c == quote_char) {
      // Found closing quote
      // Append closing quote to buffer if available
      if (tb) {
        GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
      break;
    }

    // Regular character
    string_end++;
    // Append to buffer if available
    if (tb) {
      GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
      if (status != GTEXT_JSON_OK) {
        return status;
      }
    }
  }

  // Check if string is incomplete
  if (string_end >= lexer->input_len) {
    // String is incomplete - preserve state and return incomplete status
    if (tb) {
      tb->parse_state.string_state.in_escape = in_escape;
      tb->parse_state.string_state.unicode_escape_remaining =
          unicode_escape_remaining;
      tb->parse_state.string_state.high_surrogate_seen = high_surrogate_seen;
    }
    // Advance current_offset to string_end so the incomplete token gets removed
    // from input buffer
    lexer->current_offset = string_end;
    return GTEXT_JSON_E_INCOMPLETE;
  }

  // String is complete
  // string_start to string_end is the string content (without quotes)
  // Check for underflow in subtraction
  if (json_check_sub_underflow(string_end, string_start)) {
    if (tb) {
      json_token_buffer_clear(tb);
    }
    return GTEXT_JSON_E_INVALID; // Underflow
  }
  size_t string_content_len = string_end - string_start;

  // Get the complete string content from buffer or input
  const char * string_content;
  size_t string_content_actual_len;

  // If we used the buffer (either resuming or started buffering in this chunk),
  // use it
  if (tb && tb->is_buffered && tb->buffer_used >= 2) {
    // Use buffered content - buffer has: [opening quote][content][closing
    // quote] We need just the content without quotes for decoding
    string_content = tb->buffer + 1;                 // Skip opening quote
    string_content_actual_len = tb->buffer_used - 2; // Exclude both quotes
  }
  else {
    // Use input directly (no buffering was used - string was complete in one
    // chunk)
    string_content = lexer->input + string_start;
    string_content_actual_len = string_content_len;
  }

  // Check for integer overflow in token_length calculation
  size_t token_length;
  if (resuming && tb && tb->is_buffered) {
    token_length = tb->buffer_used; // Includes both quotes
  }
  else {
    size_t start_pos = start;
    if (resuming) {
      if (!tb) {
        return GTEXT_JSON_E_BAD_TOKEN;
      }
      start_pos = tb->start_offset;
    }
    // Check for overflow: string_end + 1
    if (json_check_add_overflow(string_end, 1)) {
      if (tb) {
        json_token_buffer_clear(tb);
      }
      return GTEXT_JSON_E_INVALID; // Overflow
    }
    size_t end_plus_one = string_end + 1;
    // Check for underflow: end_plus_one - start_pos
    if (json_check_sub_underflow(end_plus_one, start_pos)) {
      if (tb) {
        json_token_buffer_clear(tb);
      }
      return GTEXT_JSON_E_INVALID; // Underflow
    }
    token_length = end_plus_one - start_pos; // Include quotes
  }

  // Decode the string
  // Allocate buffer for decoded string (worst case: same size as input)
  // Check for integer overflow in allocation size using shared helper
  if (json_check_string_length_overflow(string_content_actual_len)) {
    if (tb) {
      json_token_buffer_clear(tb);
    }
    return GTEXT_JSON_E_LIMIT; // String too large
  }
  size_t decode_capacity =
      string_content_actual_len + 1; // +1 for null terminator
  char * decoded = (char *)malloc(decode_capacity);
  if (!decoded) {
    if (tb) {
      json_token_buffer_clear(tb);
    }
    return GTEXT_JSON_E_OOM;
  }

  json_position decode_pos = lexer->pos;
  if (resuming && tb) {
    decode_pos.offset = tb->start_offset + 1; // After opening quote
  }
  else {
    decode_pos.offset = string_start;
  }
  decode_pos.col++; // After opening quote

  size_t decoded_len;
  GTEXT_JSON_Status status =
      json_decode_string(string_content, string_content_actual_len, decoded,
          decode_capacity, &decoded_len, &decode_pos,
          lexer->opts ? lexer->opts->validate_utf8 : 1, JSON_UTF8_REJECT,
          lexer->opts ? lexer->opts->allow_unescaped_controls : 0);

  if (status != GTEXT_JSON_OK) {
    free(decoded);
    if (tb) {
      json_token_buffer_clear(tb);
    }
    return status;
  }

  token->type = JSON_TOKEN_STRING;
  token->pos = lexer->pos;
  token->length = token_length;
  token->data.string.value = decoded;
  token->data.string.value_len = decoded_len;
  if (resuming && tb) {
    token->data.string.original_start = tb->start_offset + 1;
    token->data.string.original_len = string_content_actual_len;
  }
  else {
    token->data.string.original_start = string_start;
    token->data.string.original_len = string_content_len;
  }

  // Clear token buffer since string is complete
  if (tb) {
    json_token_buffer_clear(tb);
  }

  // Update lexer position
  // Check for overflow before adding string_end + 1
  if (json_check_add_overflow(string_end, 1)) {
    // Overflow - saturate at SIZE_MAX
    lexer->current_offset = SIZE_MAX;
    lexer->pos.offset = SIZE_MAX;
  }
  else {
    lexer->current_offset = string_end + 1;
    lexer->pos.offset = lexer->current_offset;
  }
  json_position_update_column(&lexer->pos, token_length);

  return GTEXT_JSON_OK;
}

// Parse a number token
static GTEXT_JSON_Status json_lexer_parse_number(
    json_lexer * lexer, json_token * token) {
  json_token_buffer * tb = lexer->token_buffer;
  size_t start = lexer->current_offset;
  size_t end = start;
  int resuming = 0;

  // Check if resuming from incomplete number
  int has_dot = 0;
  int has_exp = 0;
  int exp_sign_seen = 0;
  int starts_with_minus = 0;

  if (tb && tb->type == JSON_TOKEN_BUFFER_NUMBER) {
    resuming = 1;
    // Restore number state
    has_dot = tb->parse_state.number_state.has_dot;
    has_exp = tb->parse_state.number_state.has_exp;
    exp_sign_seen = tb->parse_state.number_state.exp_sign_seen;
    starts_with_minus = tb->parse_state.number_state.starts_with_minus;

    // If resuming and we have a minus sign but no dot/exp, check if we're in
    // the middle of -Infinity
    if (starts_with_minus && !has_dot && !has_exp && tb->buffer_used > 0 &&
        tb->buffer_used < 9 && lexer->opts &&
        lexer->opts->allow_nonfinite_numbers) {
      // Check if what we have so far matches a prefix of "-Infinity"
      const char * infinity_str = "-Infinity";
      int is_infinity_prefix = 1;
      for (size_t i = 0; i < tb->buffer_used && i < 9; i++) {
        if (tb->buffer[i] != infinity_str[i]) {
          is_infinity_prefix = 0;
          break;
        }
      }
      if (is_infinity_prefix) {
        // We're resuming an incomplete -Infinity - continue reading
        // We already have tb->buffer_used characters in the buffer
        // We need to read the remaining (9 - tb->buffer_used) characters
        size_t chars_needed = 9 - tb->buffer_used;

        while (chars_needed > 0 && end < lexer->input_len) {
          if (!json_check_bounds_offset(end, lexer->input_len)) {
            break;
          }
          char next_c = lexer->input[end];
          size_t expected_pos = tb->buffer_used; // Position in "-Infinity" string
          if (expected_pos < 9 && next_c == infinity_str[expected_pos]) {
            end++;
            // Append to buffer if available
            GTEXT_JSON_Status status =
                json_token_buffer_append(tb, &next_c, 1);
            if (status != GTEXT_JSON_OK) {
              return status;
            }
            chars_needed--;
          }
          else {
            // Not matching -Infinity, break and let validation handle it
            break;
          }
        }
        // If we've read all 9 characters, we can skip the main loop
        // and go straight to validation
        if (tb->buffer_used >= 9) {
          // Set end to indicate we've processed all input for this token
          // The validation below will handle the complete -Infinity
          // We need to make sure end reflects that we've read everything
          // Actually, we should just let the loop run, but it will break
          // immediately since we've already read all characters
        }
      }
    }
  }
  else {
    // Starting new number - initialize token buffer if available
    if (tb) {
      json_token_buffer_clear(tb);
      tb->type = JSON_TOKEN_BUFFER_NUMBER;
      tb->start_offset = start;
    }
  }

  // Determine number end by finding first non-number character
  // Numbers can contain: digits, '.', 'e', 'E', '+', '-'
  // When resuming with has_exp=1 but exp_sign_seen=0, we might need to handle
  // exponent sign first
  while (end < lexer->input_len) {
    // Defensive bounds check before buffer access
    if (!json_check_bounds_offset(end, lexer->input_len)) {
      break;
    }
    char c = lexer->input[end];

    // If resuming with exponent but no sign seen yet, check for exponent sign
    // first
    if (resuming && has_exp && !exp_sign_seen && (c == '+' || c == '-')) {
      exp_sign_seen = 1;
      end++;
      // Append to buffer if available
      if (tb) {
        GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
      continue;
    }

    if (c >= '0' && c <= '9') {
      end++;
      // Append to buffer if available
      if (tb) {
        GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
      continue;
    }
    if (c == '.' && !has_dot && !has_exp) {
      has_dot = 1;
      end++;
      // Append to buffer if available
      if (tb) {
        GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
      continue;
    }
    if ((c == 'e' || c == 'E') && !has_exp) {
      has_exp = 1;
      exp_sign_seen = 0;
      end++;
      // Append to buffer if available
      if (tb) {
        GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
      // Exponent can have + or -
      // Defensive bounds check before buffer access
      if (end < lexer->input_len &&
          json_check_bounds_offset(end, lexer->input_len) &&
          (lexer->input[end] == '+' || lexer->input[end] == '-')) {
        exp_sign_seen = 1;
        end++;
        // Append to buffer if available
        if (tb) {
          // Defensive bounds check for end - 1
          if (end > 0 && json_check_bounds_offset(end - 1, lexer->input_len)) {
            GTEXT_JSON_Status status =
                json_token_buffer_append(tb, &lexer->input[end - 1], 1);
            if (status != GTEXT_JSON_OK) {
              return status;
            }
          }
        }
      }
      continue;
    }
    if (c == '-' && end == start) {
      // Leading minus sign
      starts_with_minus = 1;
      end++;
      // Append to buffer if available
      if (tb) {
        GTEXT_JSON_Status status = json_token_buffer_append(tb, &c, 1);
        if (status != GTEXT_JSON_OK) {
          return status;
        }
      }
      continue;
    }

    // Check if this could be part of -Infinity (if enabled and we started with
    // minus) When we encounter a non-number character after '-', check if it's
    // 'I' (start of "Infinity")
    if (starts_with_minus && !has_dot && !has_exp && lexer->opts &&
        lexer->opts->allow_nonfinite_numbers && (end == start + 1) &&
        c == 'I') {
      // After "-", we have "I" - could be start of "Infinity"
      // Read the rest of "-Infinity" if available
      const char * infinity_rest = "nfinity";
      size_t infinity_rest_len = 7;
      size_t read_pos = 0;

      // Read as many characters as we can
      while (read_pos < infinity_rest_len && end < lexer->input_len) {
        if (!json_check_bounds_offset(end, lexer->input_len)) {
          break;
        }
        char next_c = lexer->input[end];
        if (next_c == infinity_rest[read_pos]) {
          end++;
          read_pos++;
          // Append to buffer if available
          if (tb) {
            GTEXT_JSON_Status status = json_token_buffer_append(tb, &next_c, 1);
            if (status != GTEXT_JSON_OK) {
              return status;
            }
          }
        }
        else {
          // Not matching -Infinity, break and let validation handle it
          break;
        }
      }
      // After reading "-Infinity" (or as much as available), break out of the
      // main loop The validation check below will handle complete vs incomplete
      // -Infinity
      break;
    }

    // Not part of number
    break;
  }

  // Check if number might be incomplete (at EOF) - do this BEFORE validation
  // because an incomplete number (like just "-" or "123") is not invalid, just
  // incomplete
  if (end >= lexer->input_len && end > start) {
    // We're at EOF - check if the number might continue
    // Defensive check: ensure end - 1 is valid (end > start guarantees end >=
    // 1)
    // Special case: if we only have "-" (just a minus sign), it's incomplete
    size_t total_len = resuming && tb ? tb->buffer_used : (end - start);
    if (total_len == 1) {
      // Defensive bounds check before buffer access
      char first_char = resuming && tb
          ? tb->buffer[0]
          : (json_check_bounds_offset(start, lexer->input_len)
                    ? lexer->input[start]
                    : '\0');
      if (first_char == '-') {
        // Just a minus sign - incomplete, need digits
        if (tb) {
          tb->parse_state.number_state.has_dot = has_dot;
          tb->parse_state.number_state.has_exp = has_exp;
          tb->parse_state.number_state.exp_sign_seen = exp_sign_seen;
          tb->parse_state.number_state.starts_with_minus = starts_with_minus;
        }
        lexer->current_offset = end;
        return GTEXT_JSON_E_INCOMPLETE;
      }
    }

    // Determine the last character of the number
    // When resuming, check the buffer's last character (it has the complete
    // token so far) When not resuming, check the input's last character
    char last_char;
    if (resuming && tb && tb->buffer_used > 0) {
      // Resuming - buffer has the complete token so far
      last_char = tb->buffer[tb->buffer_used - 1];
    }
    else {
      // Not resuming - check input's last character
      // At this point, end == input_len (loop ensures end <= input_len, and we
      // check end >= input_len) So end - 1 == input_len - 1, which is the last
      // valid index Defensive bounds check before buffer access
      if (end > 0 && json_check_bounds_offset(end - 1, lexer->input_len)) {
        last_char = lexer->input[end - 1];
      }
      else {
        last_char = '\0'; // Fallback if bounds check fails
      }
    }

    // If ends with '.', 'e', 'E', '+', or '-' (exponent sign), clearly
    // incomplete For '-', check if it's an exponent sign (has_exp) - if so,
    // it's incomplete
    if (last_char == '.' || last_char == 'e' || last_char == 'E' ||
        last_char == '+' || (last_char == '-' && has_exp)) {
      // Number ends with incomplete indicator - preserve state and return
      // incomplete
      if (tb) {
        tb->parse_state.number_state.has_dot = has_dot;
        tb->parse_state.number_state.has_exp = has_exp;
        tb->parse_state.number_state.exp_sign_seen = exp_sign_seen;
        tb->parse_state.number_state.starts_with_minus = starts_with_minus;
      }
      // Advance current_offset to end so the incomplete token gets removed from
      // input buffer
      lexer->current_offset = end;
      return GTEXT_JSON_E_INCOMPLETE;
    }

    // In streaming mode, if number ends with a digit at EOF, it might continue
    // with '.', 'e', or 'E' in the next chunk, so treat as incomplete
    // EXCEPTIONS when resuming:
    // 1. If number has an exponent with sign and digits, it's complete
    // (exponent is complete) NOTE: A number with a dot but no exponent can
    // still continue with 'e' or 'E', so it's incomplete
    if (lexer->streaming_mode && last_char >= '0' && last_char <= '9') {
      // Check if number is actually complete when resuming
      if (resuming && tb) {
        // A number with a dot but no exponent can still continue with 'e' or
        // 'E' So we don't treat it as complete here - it might continue (We
        // used to treat it as complete, but that was wrong) In streaming mode,
        // numbers ending with a digit at EOF are always incomplete because they
        // could continue with more digits (e.g., "1.5e+2" could become
        // "1.5e+20") The only exception is when we're in non-streaming mode
        // (finish() was called) Check if number ends with exponent sign - if
        // so, it's incomplete
        if (has_exp && exp_sign_seen) {
          char last_char_in_buffer = tb->buffer[tb->buffer_used - 1];
          if (last_char_in_buffer == '+' || last_char_in_buffer == '-') {
            // Ends with exponent sign - incomplete, need digits
            tb->parse_state.number_state.has_dot = has_dot;
            tb->parse_state.number_state.has_exp = has_exp;
            tb->parse_state.number_state.exp_sign_seen = exp_sign_seen;
            tb->parse_state.number_state.starts_with_minus = starts_with_minus;
            lexer->current_offset = end;
            return GTEXT_JSON_E_INCOMPLETE;
          }
          // Has exponent with sign and digits, but ends with digit at EOF
          // In streaming mode, this is incomplete (could continue with more
          // digits) Fall through to preserve state and return incomplete
        }
        {
          // Preserve state - number might continue
          tb->parse_state.number_state.has_dot = has_dot;
          tb->parse_state.number_state.has_exp = has_exp;
          tb->parse_state.number_state.exp_sign_seen = exp_sign_seen;
          tb->parse_state.number_state.starts_with_minus = starts_with_minus;
          // Advance current_offset to end so the incomplete token gets removed
          // from input buffer
          lexer->current_offset = end;
          return GTEXT_JSON_E_INCOMPLETE;
        }
      }
      else {
        // Not resuming - preserve state, number might continue
        if (tb) {
          tb->parse_state.number_state.has_dot = has_dot;
          tb->parse_state.number_state.has_exp = has_exp;
          tb->parse_state.number_state.exp_sign_seen = exp_sign_seen;
          tb->parse_state.number_state.starts_with_minus = starts_with_minus;
        }
        // Advance current_offset to end so the incomplete token gets removed
        // from input buffer
        lexer->current_offset = end;
        return GTEXT_JSON_E_INCOMPLETE;
      }
    }

    // At EOF but number looks complete - parse it
  }

  // Check if number is valid (only for complete numbers)
  size_t total_len =
      resuming && tb ? (tb->buffer_used + (end - start)) : (end - start);

  // Before validating, check if this might be -Infinity (complete or prefix)
  // This handles the case where -Infinity wasn't caught by
  // json_lexer_match_neg_infinity because it was called before we had enough
  // characters, or in streaming mode We need to check if what we have matches
  // "-Infinity" or is a prefix of it
  if (starts_with_minus && total_len >= 1 && total_len <= 9 && lexer->opts &&
      lexer->opts->allow_nonfinite_numbers) {
    const char * number_content;
    size_t content_len;
    if (resuming && tb && tb->buffer_used > 0) {
      number_content = tb->buffer;
      content_len = tb->buffer_used;
    }
    else {
      number_content = lexer->input + start;
      content_len = end - start;
    }
    // Check if it matches "-Infinity" or is a prefix of it
    const char * infinity_str = "-Infinity";
    size_t infinity_len = 9;
    int is_prefix = 1;
    size_t check_len =
        (content_len < infinity_len) ? content_len : infinity_len;
    for (size_t i = 0; i < check_len; i++) {
      if (number_content[i] != infinity_str[i]) {
        is_prefix = 0;
        break;
      }
    }
    if (is_prefix) {
      if (content_len == 9) {
        // Complete -Infinity - let json_parse_number handle it (it will
        // recognize it) Make sure we use the buffered content if resuming
        if (resuming && tb && tb->buffer_used > 0) {
          // We have complete -Infinity in buffer, use it for parsing
          // The validation and parsing below will handle it
        }
      }
      else {
        // Incomplete -Infinity prefix - treat as incomplete
        // Need to ensure characters are buffered
        if (tb) {
          // Mark that we're buffering a number
          if (tb->type == JSON_TOKEN_BUFFER_NONE) {
            tb->type = JSON_TOKEN_BUFFER_NUMBER;
            tb->start_offset = start;
          }
          // If not resuming, buffer the characters we've read
          if (!resuming && content_len > 0) {
            for (size_t i = 0; i < content_len; i++) {
              if (start + i >= lexer->input_len)
                break;
              GTEXT_JSON_Status buf_status =
                  json_token_buffer_append(tb, &lexer->input[start + i], 1);
              if (buf_status != GTEXT_JSON_OK) {
                json_token_buffer_clear(tb);
                return buf_status;
              }
            }
            tb->is_buffered = 1;
          }
          tb->parse_state.number_state.has_dot = has_dot;
          tb->parse_state.number_state.has_exp = has_exp;
          tb->parse_state.number_state.exp_sign_seen = exp_sign_seen;
          tb->parse_state.number_state.starts_with_minus = starts_with_minus;
        }
        // Advance current_offset to end so the incomplete token gets removed
        // from input buffer The token buffer has the data, so it's safe to mark
        // input as processed
        lexer->current_offset = end;
        return GTEXT_JSON_E_INCOMPLETE;
      }
    }
  }

  // Defensive bounds check before buffer access
  char first_char_check = resuming && tb
      ? tb->buffer[0]
      : (json_check_bounds_offset(start, lexer->input_len) ? lexer->input[start]
                                                           : '\0');
  if (total_len == 0 || (total_len == 1 && first_char_check == '-')) {
    if (tb) {
      json_token_buffer_clear(tb);
    }
    return GTEXT_JSON_E_BAD_NUMBER;
  }

  // Number is complete - get the complete number content
  // If we appended to buffer in the loop, use buffer; otherwise use input
  // directly
  const char * number_content;
  size_t number_len;

  // Use buffer if: resuming (buffer has previous chunk data) OR buffer was used
  // in this chunk
  if (tb && tb->buffer_used > 0 && (resuming || tb->is_buffered)) {
    // Use buffered content (was appended in loop above)
    number_content = tb->buffer;
    number_len = tb->buffer_used;
  }
  else {
    // Use input directly (no buffering was needed)
    number_content = lexer->input + start;
    number_len = end - start;
  }

  // Parse the number
  json_position num_pos = lexer->pos;
  GTEXT_JSON_Status status = json_parse_number(
      number_content, number_len, &token->data.number, &num_pos, lexer->opts);

  if (status != GTEXT_JSON_OK) {
    if (tb) {
      json_token_buffer_clear(tb);
    }
    return status;
  }

  token->type = JSON_TOKEN_NUMBER;
  token->pos = lexer->pos;
  if (resuming && tb) {
    token->length = tb->buffer_used;
  }
  else {
    token->length = number_len;
  }

  // Clear token buffer since number is complete
  if (tb) {
    json_token_buffer_clear(tb);
  }

  // Update lexer position
  // When resuming from buffer, if end == start (no new input consumed),
  // we should NOT advance past the non-number character - it needs to be
  // processed as the next token (e.g., closing bracket, comma, etc.)
  // The next call to json_lexer_next() will process it
  if (resuming && end == start) {
    // We completed the number from the buffer, but didn't consume any new input
    // The character at position 0 is the non-number character that ended the
    // number We should NOT advance past it - leave it for the next token
  }
  lexer->current_offset = end;
  lexer->pos.offset = lexer->current_offset;
  json_position_update_column(&lexer->pos, token->length);

  return GTEXT_JSON_OK;
}

GTEXT_INTERNAL_API GTEXT_JSON_Status json_lexer_init(json_lexer * lexer,
    const char * input, size_t input_len, const GTEXT_JSON_Parse_Options * opts,
    int streaming_mode) {
  // Defensive NULL pointer checks
  if (!lexer) {
    return GTEXT_JSON_E_INVALID;
  }
  if (!input && input_len > 0) {
    return GTEXT_JSON_E_INVALID;
  }

  lexer->input = input;
  lexer->input_len = input_len;
  lexer->current_offset = 0;
  lexer->pos.offset = 0;
  lexer->pos.line = 1;
  lexer->pos.col = 1;
  lexer->opts = opts;
  lexer->streaming_mode = streaming_mode ? 1 : 0;
  lexer->token_buffer = NULL; // Set by caller if needed

  // Skip leading BOM if enabled
  if (opts && opts->allow_leading_bom && input_len >= 3 &&
      (unsigned char)input[0] == 0xEF && (unsigned char)input[1] == 0xBB &&
      (unsigned char)input[2] == 0xBF) {
    lexer->current_offset = 3;
    lexer->pos.offset = 3;
    lexer->pos.col = 4;
  }

  return GTEXT_JSON_OK;
}

GTEXT_INTERNAL_API GTEXT_JSON_Status json_lexer_next(
    json_lexer * lexer, json_token * token) {
  // Defensive NULL pointer checks
  if (!lexer) {
    return GTEXT_JSON_E_INVALID;
  }
  if (!token) {
    return GTEXT_JSON_E_INVALID;
  }
  // Additional defensive check: ensure input is valid if we have input_len > 0
  if (lexer->input_len > 0 && !lexer->input) {
    return GTEXT_JSON_E_INVALID;
  }

  // Initialize token
  memset(token, 0, sizeof(*token));
  token->type = JSON_TOKEN_ERROR;

  // Check if we're resuming from an incomplete token
  if (lexer->token_buffer &&
      lexer->token_buffer->type != JSON_TOKEN_BUFFER_NONE) {
    // We're resuming - directly call the appropriate parse function
    if (lexer->token_buffer->type == JSON_TOKEN_BUFFER_STRING) {
      return json_lexer_parse_string(lexer, token);
    }
    else if (lexer->token_buffer->type == JSON_TOKEN_BUFFER_NUMBER) {
      return json_lexer_parse_number(lexer, token);
    }
  }

  // Skip whitespace
  json_lexer_skip_whitespace(lexer);

  // Skip comments (if enabled)
  GTEXT_JSON_Status status = json_lexer_skip_comments(lexer);
  if (status != GTEXT_JSON_OK) {
    return status;
  }

  // Check for EOF
  if (lexer->current_offset >= lexer->input_len) {
    token->type = JSON_TOKEN_EOF;
    token->pos = lexer->pos;
    token->length = 0;
    return GTEXT_JSON_OK;
  }

  size_t start = lexer->current_offset;
  // Defensive bounds check before buffer access
  if (!json_check_bounds_offset(start, lexer->input_len)) {
    token->type = JSON_TOKEN_ERROR;
    token->pos = lexer->pos;
    token->length = 0;
    return GTEXT_JSON_E_INVALID;
  }
  char c = lexer->input[start];

  // Punctuation tokens
  switch (c) {
  case '{':
    token->type = JSON_TOKEN_LBRACE;
    token->pos = lexer->pos;
    token->length = 1;
    // Check for overflow before incrementing
    if (json_check_add_overflow(lexer->current_offset, 1)) {
      lexer->current_offset = SIZE_MAX;
    }
    else {
      lexer->current_offset++;
    }
    json_position_update_offset(&lexer->pos, 1);
    json_position_update_column(&lexer->pos, 1);
    return GTEXT_JSON_OK;

  case '}':
    token->type = JSON_TOKEN_RBRACE;
    token->pos = lexer->pos;
    token->length = 1;
    // Check for overflow before incrementing
    if (json_check_add_overflow(lexer->current_offset, 1)) {
      lexer->current_offset = SIZE_MAX;
    }
    else {
      lexer->current_offset++;
    }
    json_position_update_offset(&lexer->pos, 1);
    json_position_update_column(&lexer->pos, 1);
    return GTEXT_JSON_OK;

  case '[':
    token->type = JSON_TOKEN_LBRACKET;
    token->pos = lexer->pos;
    token->length = 1;
    // Check for overflow before incrementing
    if (json_check_add_overflow(lexer->current_offset, 1)) {
      lexer->current_offset = SIZE_MAX;
    }
    else {
      lexer->current_offset++;
    }
    json_position_update_offset(&lexer->pos, 1);
    json_position_update_column(&lexer->pos, 1);
    return GTEXT_JSON_OK;

  case ']':
    token->type = JSON_TOKEN_RBRACKET;
    token->pos = lexer->pos;
    token->length = 1;
    // Check for overflow before incrementing
    if (json_check_add_overflow(lexer->current_offset, 1)) {
      lexer->current_offset = SIZE_MAX;
    }
    else {
      lexer->current_offset++;
    }
    json_position_update_offset(&lexer->pos, 1);
    json_position_update_column(&lexer->pos, 1);
    return GTEXT_JSON_OK;

  case ':':
    token->type = JSON_TOKEN_COLON;
    token->pos = lexer->pos;
    token->length = 1;
    // Check for overflow before incrementing
    if (json_check_add_overflow(lexer->current_offset, 1)) {
      lexer->current_offset = SIZE_MAX;
    }
    else {
      lexer->current_offset++;
    }
    json_position_update_offset(&lexer->pos, 1);
    json_position_update_column(&lexer->pos, 1);
    return GTEXT_JSON_OK;

  case ',':
    token->type = JSON_TOKEN_COMMA;
    token->pos = lexer->pos;
    token->length = 1;
    // Check for overflow before incrementing
    if (json_check_add_overflow(lexer->current_offset, 1)) {
      lexer->current_offset = SIZE_MAX;
    }
    else {
      lexer->current_offset++;
    }
    json_position_update_offset(&lexer->pos, 1);
    json_position_update_column(&lexer->pos, 1);
    return GTEXT_JSON_OK;
  }

  // String tokens
  if (c == '"' ||
      (lexer->opts && lexer->opts->allow_single_quotes && c == '\'')) {
    return json_lexer_parse_string(lexer, token);
  }

  // Number tokens (including -Infinity special case)
  // Note: -Infinity is checked here because it starts with '-', which is
  // also the start of negative numbers. It's gated behind
  // allow_nonfinite_numbers (same as "Infinity" in the keyword path).
  if (c == '-' || (c >= '0' && c <= '9')) {
    // Check for -Infinity first (if enabled)
    if (c == '-') {
      int neg_inf_result = json_lexer_match_neg_infinity(lexer, token);
      if (neg_inf_result == 1) {
        return GTEXT_JSON_OK;
      }
      else if (neg_inf_result == GTEXT_JSON_E_NONFINITE) {
        return GTEXT_JSON_E_NONFINITE;
      }
      // Otherwise (0), continue to number parsing
    }
    return json_lexer_parse_number(lexer, token);
  }

  // Keyword tokens (true, false, null, NaN, Infinity)
  int keyword_result = json_lexer_match_keyword(lexer, token);
  if (keyword_result == 1) {
    return GTEXT_JSON_OK;
  }
  else if (keyword_result == GTEXT_JSON_E_NONFINITE) {
    return GTEXT_JSON_E_NONFINITE;
  }

  // In streaming mode, check if we have a partial keyword prefix
  // This allows keywords to span chunk boundaries
  if (lexer->streaming_mode && json_is_identifier_start(c)) {
    // We have an identifier start character but keyword didn't match
    // Check if it's a valid keyword prefix - if so, it's incomplete, not bad
    size_t prefix_len = 0;
    while (prefix_len < lexer->input_len) {
      // Check for underflow: input_len - start
      if (json_check_sub_underflow(lexer->input_len, start) ||
          prefix_len >= lexer->input_len - start) {
        break;
      }
      // Check for overflow: start + prefix_len
      if (json_check_add_overflow(start, prefix_len)) {
        break;
      }
      // Defensive bounds check before buffer access
      if (!json_check_bounds_offset(start + prefix_len, lexer->input_len)) {
        break;
      }
      if (!json_is_identifier_cont(lexer->input[start + prefix_len])) {
        break;
      }
      // Check for overflow before incrementing prefix_len
      if (json_check_add_overflow(prefix_len, 1)) {
        break;
      }
      prefix_len++;
    }

    if (prefix_len > 0 &&
        json_is_keyword_prefix(lexer->input + start, prefix_len)) {
      // Valid keyword prefix but incomplete - need more input
      // Don't advance current_offset - keep the partial keyword in the buffer
      // The stream parser will handle this by not marking input as processed
      return GTEXT_JSON_E_INCOMPLETE;
    }
  }

  // Unknown token
  token->type = JSON_TOKEN_ERROR;
  token->pos = lexer->pos;
  token->length = 1;
  return GTEXT_JSON_E_BAD_TOKEN;
}

GTEXT_INTERNAL_API void json_token_cleanup(json_token * token) {
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
