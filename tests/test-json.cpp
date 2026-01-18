#include <gtest/gtest.h>
#include <text/text.h>
#include <text/json.h>
#include <text/json_dom.h>
#include <cmath>
#include <cstring>

// Include internal header for testing internal functions
extern "C" {
#include "../src/json_internal.h"
}

/**
 * Test default parse options match specification (strict JSON by default)
 */
TEST(JsonTests, ParseOptionsDefault) {
    text_json_parse_options opts = text_json_parse_options_default();

    // Strictness / extensions - all should be off (strict JSON)
    EXPECT_EQ(opts.allow_comments, 0);
    EXPECT_EQ(opts.allow_trailing_commas, 0);
    EXPECT_EQ(opts.allow_nonfinite_numbers, 0);
    EXPECT_EQ(opts.allow_single_quotes, 0);
    EXPECT_EQ(opts.allow_unescaped_controls, 0);

    // Unicode / input handling
    EXPECT_EQ(opts.allow_leading_bom, 1);  // default on
    EXPECT_EQ(opts.validate_utf8, 1);      // default on
    EXPECT_EQ(opts.normalize_unicode, 0);   // v2 feature, off by default

    // Duplicate keys
    EXPECT_EQ(opts.dupkeys, TEXT_JSON_DUPKEY_ERROR);

    // Limits - should be 0 (library defaults)
    EXPECT_EQ(opts.max_depth, 0u);
    EXPECT_EQ(opts.max_string_bytes, 0u);
    EXPECT_EQ(opts.max_container_elems, 0u);
    EXPECT_EQ(opts.max_total_bytes, 0u);

    // Number fidelity / representations
    EXPECT_EQ(opts.preserve_number_lexeme, 1);  // preserve for round-trip
    EXPECT_EQ(opts.parse_int64, 1);
    EXPECT_EQ(opts.parse_uint64, 1);
    EXPECT_EQ(opts.parse_double, 1);
    EXPECT_EQ(opts.allow_big_decimal, 0);
}

/**
 * Test default write options match specification (compact output)
 */
TEST(JsonTests, WriteOptionsDefault) {
    text_json_write_options opts = text_json_write_options_default();

    // Formatting
    EXPECT_EQ(opts.pretty, 0);              // compact output
    EXPECT_EQ(opts.indent_spaces, 2);       // default indent
    EXPECT_STREQ(opts.newline, "\n");       // default newline

    // Escaping
    EXPECT_EQ(opts.escape_solidus, 0);
    EXPECT_EQ(opts.escape_unicode, 0);
    EXPECT_EQ(opts.escape_all_non_ascii, 0);

    // Canonical / deterministic
    EXPECT_EQ(opts.sort_object_keys, 0);    // preserve insertion order
    EXPECT_EQ(opts.canonical_numbers, 0);   // preserve original lexeme
    EXPECT_EQ(opts.canonical_strings, 0);    // preserve original escapes

    // Extensions
    EXPECT_EQ(opts.allow_nonfinite_numbers, 0);
}

/**
 * Test that text_json_free can be called with NULL
 */
TEST(JsonTests, FreeNullValue) {
    // Should not crash
    text_json_free(nullptr);
    EXPECT_TRUE(true);  // If we get here, it worked
}

/**
 * Test standard escape sequence decoding
 */
TEST(JsonTests, StringEscapeSequences) {
    char output[256];
    size_t output_len;
    json_position pos = {0, 1, 1};

    // Test each standard escape
    struct {
        const char* input;
        const char* expected;
        size_t expected_len;
    } tests[] = {
        {"\\\"", "\"", 1},
        {"\\\\", "\\", 1},
        {"\\/", "/", 1},
        {"\\b", "\b", 1},
        {"\\f", "\f", 1},
        {"\\n", "\n", 1},
        {"\\r", "\r", 1},
        {"\\t", "\t", 1},
        {"hello\\nworld", "hello\nworld", 11},
        {"a\\tb\\nc", "a\tb\nc", 5}
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;
        output_len = 0;

        text_json_status status = json_decode_string(
            tests[i].input,
            strlen(tests[i].input),
            output,
            sizeof(output),
            &output_len,
            &pos,
            0,  // don't validate UTF-8 for these tests
            JSON_UTF8_REJECT
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;
        EXPECT_EQ(output_len, tests[i].expected_len) << "Wrong length for: " << tests[i].input;
        EXPECT_EQ(memcmp(output, tests[i].expected, output_len), 0)
            << "Wrong output for: " << tests[i].input;
    }
}

/**
 * Test Unicode escape sequence decoding
 */
TEST(JsonTests, StringUnicodeEscapes) {
    char output[256];
    size_t output_len;
    json_position pos = {0, 1, 1};

    // Test Unicode escapes
    struct {
        const char* input;
        const char* expected;
        size_t expected_len;
    } tests[] = {
        {"\\u0041", "A", 1},           // U+0041 = 'A'
        {"\\u00E9", "\xC3\xA9", 2},    // U+00E9 = 'é' (UTF-8)
        {"\\u20AC", "\xE2\x82\xAC", 3} // U+20AC = '€' (UTF-8)
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;
        output_len = 0;

        text_json_status status = json_decode_string(
            tests[i].input,
            strlen(tests[i].input),
            output,
            sizeof(output),
            &output_len,
            &pos,
            0,
            JSON_UTF8_REJECT
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;
        EXPECT_EQ(output_len, tests[i].expected_len) << "Wrong length for: " << tests[i].input;
        EXPECT_EQ(memcmp(output, tests[i].expected, output_len), 0)
            << "Wrong output for: " << tests[i].input;
    }
}

/**
 * Test surrogate pair decoding
 */
TEST(JsonTests, StringSurrogatePairs) {
    char output[256];
    size_t output_len;
    json_position pos = {0, 1, 1};

    // U+1F600 = 😀 (grinning face)
    // High surrogate: U+D83D, Low surrogate: U+DE00
    const char* input = "\\uD83D\\uDE00";
    const char* expected = "\xF0\x9F\x98\x80";  // UTF-8 for U+1F600
    size_t expected_len = 4;

    text_json_status status = json_decode_string(
        input,
        strlen(input),
        output,
        sizeof(output),
        &output_len,
        &pos,
        0,
        JSON_UTF8_REJECT
    );

    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(output_len, expected_len);
    EXPECT_EQ(memcmp(output, expected, expected_len), 0);
}

/**
 * Test invalid escape sequences are rejected
 */
TEST(JsonTests, StringInvalidEscapes) {
    char output[256];
    size_t output_len;
    json_position pos = {0, 1, 1};

    // Invalid escape sequences
    const char* invalid_escapes[] = {
        "\\x",      // Invalid escape character
        "\\u",      // Incomplete Unicode escape
        "\\u12",    // Incomplete Unicode escape
        "\\u12G",   // Invalid hex digit
        "\\uD83D",  // High surrogate without low surrogate
        "\\uDE00"   // Low surrogate without high surrogate
    };

    for (size_t i = 0; i < sizeof(invalid_escapes) / sizeof(invalid_escapes[0]); ++i) {
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;
        output_len = 0;

        text_json_status status = json_decode_string(
            invalid_escapes[i],
            strlen(invalid_escapes[i]),
            output,
            sizeof(output),
            &output_len,
            &pos,
            0,
            JSON_UTF8_REJECT
        );

        EXPECT_NE(status, TEXT_JSON_OK) << "Should reject: " << invalid_escapes[i];
    }
}

/**
 * Test position tracking during string decoding
 */
TEST(JsonTests, StringPositionTracking) {
    char output[256];
    size_t output_len;
    json_position pos = {0, 1, 1};

    const char* input = "hello\\nworld";
    text_json_status status = json_decode_string(
        input,
        strlen(input),
        output,
        sizeof(output),
        &output_len,
        &pos,
        0,
        JSON_UTF8_REJECT
    );

    EXPECT_EQ(status, TEXT_JSON_OK);
    // Position should be updated (offset should be at end of input)
    EXPECT_EQ(pos.offset, strlen(input));
}

/**
 * Test buffer overflow protection
 */
TEST(JsonTests, StringBufferOverflowProtection) {
    char output[5];  // Small buffer
    size_t output_len;
    json_position pos = {0, 1, 1};

    // Try to decode a string that would overflow the buffer
    const char* input = "hello world";  // 11 characters > 5 buffer size
    text_json_status status = json_decode_string(
        input,
        strlen(input),
        output,
        sizeof(output),
        &output_len,
        &pos,
        0,
        JSON_UTF8_REJECT
    );

    EXPECT_EQ(status, TEXT_JSON_E_LIMIT);
}

/**
 * Test buffer overflow protection with Unicode escape
 */
TEST(JsonTests, StringBufferOverflowUnicode) {
    char output[2];  // Very small buffer
    size_t output_len;
    json_position pos = {0, 1, 1};

    // Unicode escape produces 3 bytes (€), but buffer is only 2
    const char* input = "\\u20AC";
    text_json_status status = json_decode_string(
        input,
        strlen(input),
        output,
        sizeof(output),
        &output_len,
        &pos,
        0,
        JSON_UTF8_REJECT
    );

    EXPECT_EQ(status, TEXT_JSON_E_LIMIT);
}

/**
 * Test valid number formats
 */
TEST(JsonTests, NumberValidFormats) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();

    struct {
        const char* input;
        int64_t expected_i64;
        uint64_t expected_u64;
    } tests[] = {
        {"0", 0, 0},
        {"123", 123, 123},
        {"-123", -123, 0},  // negative can't be uint64
        {"0.5", 0, 0},      // has fractional part
        {"123.456", 0, 0},  // has fractional part
        {"1e2", 0, 0},      // has exponent
        {"-1e-2", 0, 0},    // has exponent
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;
        EXPECT_TRUE(num.flags & JSON_NUMBER_HAS_LEXEME) << "Should preserve lexeme: " << tests[i].input;
        EXPECT_STREQ(num.lexeme, tests[i].input) << "Lexeme mismatch: " << tests[i].input;

        if (tests[i].expected_i64 != 0 || strcmp(tests[i].input, "0") == 0) {
            if (num.flags & JSON_NUMBER_HAS_I64) {
                EXPECT_EQ(num.i64, tests[i].expected_i64) << "int64 mismatch: " << tests[i].input;
            }
        }

        if (tests[i].expected_u64 != 0 || strcmp(tests[i].input, "0") == 0) {
            if (num.flags & JSON_NUMBER_HAS_U64) {
                EXPECT_EQ(num.u64, tests[i].expected_u64) << "uint64 mismatch: " << tests[i].input;
            }
        }

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test invalid number formats are rejected
 */
TEST(JsonTests, NumberInvalidFormats) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();

    const char* invalid_numbers[] = {
        "01",        // Leading zero
        "1.",        // Trailing decimal point
        ".1",        // Leading decimal point (without leading zero)
        "-",         // Just minus sign
        "--1",       // Double minus
        "1e",        // Incomplete exponent
        "1e+",       // Incomplete exponent
        "1e-",       // Incomplete exponent
        "abc",       // Not a number
    };

    for (size_t i = 0; i < sizeof(invalid_numbers) / sizeof(invalid_numbers[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            invalid_numbers[i],
            strlen(invalid_numbers[i]),
            &num,
            &pos,
            &opts
        );

        EXPECT_NE(status, TEXT_JSON_OK) << "Should reject: " << invalid_numbers[i];
        EXPECT_EQ(status, TEXT_JSON_E_BAD_NUMBER) << "Should return BAD_NUMBER for: " << invalid_numbers[i];

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test int64 boundary values and overflow detection
 */
TEST(JsonTests, NumberInt64Boundaries) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();

    struct {
        const char* input;
        int64_t expected;
        int should_have_i64;
    } tests[] = {
        {"9223372036854775807", INT64_MAX, 1},      // Max int64
        {"-9223372036854775808", INT64_MIN, 1},     // Min int64
        {"9223372036854775808", 0, 0},              // Overflow (max + 1)
        {"-9223372036854775809", 0, 0},             // Underflow (min - 1)
        {"0", 0, 1},
        {"-1", -1, 1},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;

        if (tests[i].should_have_i64) {
            EXPECT_TRUE(num.flags & JSON_NUMBER_HAS_I64) << "Should have int64: " << tests[i].input;
            EXPECT_EQ(num.i64, tests[i].expected) << "int64 value mismatch: " << tests[i].input;
        } else {
            // Overflow case - may or may not have int64, but if it does, it's wrong
            if (num.flags & JSON_NUMBER_HAS_I64) {
                EXPECT_NE(num.i64, tests[i].expected) << "Should not have correct int64 due to overflow: " << tests[i].input;
            }
        }

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test uint64 boundary values and overflow detection
 */
TEST(JsonTests, NumberUint64Boundaries) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();

    struct {
        const char* input;
        uint64_t expected;
        int should_have_u64;
    } tests[] = {
        {"18446744073709551615", UINT64_MAX, 1},     // Max uint64
        {"18446744073709551616", 0, 0},              // Overflow (max + 1)
        {"0", 0, 1},
        {"123", 123, 1},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;

        if (tests[i].should_have_u64) {
            EXPECT_TRUE(num.flags & JSON_NUMBER_HAS_U64) << "Should have uint64: " << tests[i].input;
            EXPECT_EQ(num.u64, tests[i].expected) << "uint64 value mismatch: " << tests[i].input;
        }

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test double parsing
 */
TEST(JsonTests, NumberDoubleParsing) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();

    struct {
        const char* input;
        double expected;
        double tolerance;
    } tests[] = {
        {"0.0", 0.0, 0.0},
        {"123.456", 123.456, 0.001},
        {"-123.456", -123.456, 0.001},
        {"1e2", 100.0, 0.0},
        {"1.5e-2", 0.015, 0.0001},
        {"-1.5e-2", -0.015, 0.0001},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;
        EXPECT_TRUE(num.flags & JSON_NUMBER_HAS_DOUBLE) << "Should have double: " << tests[i].input;
        EXPECT_NEAR(num.dbl, tests[i].expected, tests[i].tolerance)
            << "Double value mismatch: " << tests[i].input;

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test nonfinite number parsing (when enabled)
 */
TEST(JsonTests, NumberNonfiniteNumbers) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_nonfinite_numbers = 1;

    struct {
        const char* input;
        int is_nan;
        int is_inf;
        int is_neg_inf;
    } tests[] = {
        {"NaN", 1, 0, 0},
        {"Infinity", 0, 1, 0},
        {"-Infinity", 0, 0, 1},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;
        EXPECT_TRUE(num.flags & JSON_NUMBER_HAS_DOUBLE) << "Should have double: " << tests[i].input;
        EXPECT_TRUE(num.flags & JSON_NUMBER_IS_NONFINITE) << "Should be nonfinite: " << tests[i].input;

        if (tests[i].is_nan) {
            EXPECT_TRUE(std::isnan(num.dbl)) << "Should be NaN: " << tests[i].input;
        } else if (tests[i].is_inf) {
            EXPECT_TRUE(std::isinf(num.dbl) && num.dbl > 0) << "Should be +Infinity: " << tests[i].input;
        } else if (tests[i].is_neg_inf) {
            EXPECT_TRUE(std::isinf(num.dbl) && num.dbl < 0) << "Should be -Infinity: " << tests[i].input;
        }

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test that nonfinite numbers are rejected when disabled
 */
TEST(JsonTests, NumberNonfiniteRejected) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_nonfinite_numbers = 0;

    const char* nonfinite[] = {
        "NaN",
        "Infinity",
        "-Infinity",
    };

    for (size_t i = 0; i < sizeof(nonfinite) / sizeof(nonfinite[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            nonfinite[i],
            strlen(nonfinite[i]),
            &num,
            &pos,
            &opts
        );

        EXPECT_NE(status, TEXT_JSON_OK) << "Should reject nonfinite when disabled: " << nonfinite[i];
        EXPECT_EQ(status, TEXT_JSON_E_BAD_NUMBER) << "Should return BAD_NUMBER: " << nonfinite[i];

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test lexeme preservation
 */
TEST(JsonTests, NumberLexemePreservation) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();
    opts.preserve_number_lexeme = 1;

    const char* numbers[] = {
        "0",
        "123",
        "-456",
        "123.456",
        "1e10",
        "-1.5e-2",
    };

    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        text_json_status status = json_parse_number(
            numbers[i],
            strlen(numbers[i]),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << numbers[i];
        EXPECT_TRUE(num.flags & JSON_NUMBER_HAS_LEXEME) << "Should preserve lexeme: " << numbers[i];
        EXPECT_STREQ(num.lexeme, numbers[i]) << "Lexeme mismatch: " << numbers[i];
        EXPECT_EQ(num.lexeme_len, strlen(numbers[i])) << "Lexeme length mismatch: " << numbers[i];

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test position tracking during number parsing
 */
TEST(JsonTests, NumberPositionTracking) {
    json_number num;
    json_position pos = {0, 1, 1};
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "123.456";
    text_json_status status = json_parse_number(
        input,
        strlen(input),
        &num,
        &pos,
        &opts
    );

    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(pos.offset, strlen(input));
    EXPECT_EQ(pos.col, (int)strlen(input) + 1);  // col is 1-based

    // Clean up
    json_number_destroy(&num);
}

/**
 * Test lexer correctly identifies all token types in valid JSON
 */
TEST(JsonTests, LexerTokenTypes) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "{}[]:,";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Test LBRACE
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACE);
    json_token_cleanup(&token);

    // Test RBRACE
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACE);
    json_token_cleanup(&token);

    // Test LBRACKET
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACKET);
    json_token_cleanup(&token);

    // Test RBRACKET
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACKET);
    json_token_cleanup(&token);

    // Test COLON
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_COLON);
    json_token_cleanup(&token);

    // Test COMMA
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_COMMA);
    json_token_cleanup(&token);

    // Test EOF
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_EOF);
    json_token_cleanup(&token);
}

/**
 * Test lexer keyword tokenization
 */
TEST(JsonTests, LexerKeywords) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "null true false";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Test null
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NULL);
    json_token_cleanup(&token);

    // Test true
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_TRUE);
    json_token_cleanup(&token);

    // Test false
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_FALSE);
    json_token_cleanup(&token);
}

/**
 * Test lexer string tokenization with escape sequences
 */
TEST(JsonTests, LexerStringTokenization) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "\"hello\\nworld\"";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.data.string.value_len, 11u);  // "hello\nworld" = 11 chars
    EXPECT_EQ(memcmp(token.data.string.value, "hello\nworld", 11), 0);
    json_token_cleanup(&token);
}

/**
 * Test lexer number tokenization
 */
TEST(JsonTests, LexerNumberTokenization) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "123 -456 789.012";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Test integer
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_TRUE(token.data.number.flags & JSON_NUMBER_HAS_I64);
    EXPECT_EQ(token.data.number.i64, 123);
    json_token_cleanup(&token);

    // Test negative integer
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_TRUE(token.data.number.flags & JSON_NUMBER_HAS_I64);
    EXPECT_EQ(token.data.number.i64, -456);
    json_token_cleanup(&token);

    // Test decimal
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_TRUE(token.data.number.flags & JSON_NUMBER_HAS_DOUBLE);
    EXPECT_NEAR(token.data.number.dbl, 789.012, 0.001);
    json_token_cleanup(&token);
}

/**
 * Test comment lexing (single-line and multi-line)
 */
TEST(JsonTests, LexerComments) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_comments = 1;

    const char* input = "// comment\n123 /* multi\nline */ 456";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should skip comment and get first number
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_EQ(token.data.number.i64, 123);
    json_token_cleanup(&token);

    // Should skip multi-line comment and get second number
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_EQ(token.data.number.i64, 456);
    json_token_cleanup(&token);
}

/**
 * Test that comments are rejected when disabled
 */
TEST(JsonTests, LexerCommentsRejected) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_comments = 0;

    const char* input = "// comment\n123";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should treat // as invalid token
    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test position tracking accuracy
 */
TEST(JsonTests, LexerPositionTracking) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "{\n  \"key\": 123\n}";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // LBRACE at line 1, col 1
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACE);
    EXPECT_EQ(token.pos.line, 1);
    EXPECT_EQ(token.pos.col, 1);
    json_token_cleanup(&token);

    // STRING at line 2, col 3
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.pos.line, 2);
    EXPECT_EQ(token.pos.col, 3);
    json_token_cleanup(&token);

    // COLON at line 2
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_COLON);
    json_token_cleanup(&token);

    // NUMBER at line 2
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    json_token_cleanup(&token);

    // RBRACE at line 3, col 1
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACE);
    EXPECT_EQ(token.pos.line, 3);
    EXPECT_EQ(token.pos.col, 1);
    json_token_cleanup(&token);
}

/**
 * Test extension tokens (NaN, Infinity, -Infinity) when enabled
 */
TEST(JsonTests, LexerExtensionTokens) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_nonfinite_numbers = 1;

    const char* input = "NaN Infinity -Infinity";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Test NaN
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NAN);
    json_token_cleanup(&token);

    // Test Infinity
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_INFINITY);
    json_token_cleanup(&token);

    // Test -Infinity
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NEG_INFINITY);
    json_token_cleanup(&token);
}

/**
 * Test that extension tokens are rejected when disabled
 */
TEST(JsonTests, LexerExtensionTokensRejected) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_nonfinite_numbers = 0;

    const char* input = "NaN";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should treat NaN as invalid token (not a keyword)
    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test whitespace handling
 */
TEST(JsonTests, LexerWhitespaceHandling) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "  {  }  [  ]  ";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should skip leading whitespace
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACE);
    json_token_cleanup(&token);

    // Should skip whitespace between tokens
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACE);
    json_token_cleanup(&token);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACKET);
    json_token_cleanup(&token);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACKET);
    json_token_cleanup(&token);
}

/**
 * Test lexer error reporting with accurate positions
 */
TEST(JsonTests, LexerErrorReporting) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();

    const char* input = "123 @ invalid";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should successfully parse number
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    json_token_cleanup(&token);

    // Should fail on invalid character
    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    EXPECT_EQ(status, TEXT_JSON_E_BAD_TOKEN);
    // Position should be accurate
    EXPECT_EQ(token.pos.offset, 4u);  // After "123 "
    json_token_cleanup(&token);
}

/**
 * Test single-quote strings when enabled
 */
TEST(JsonTests, LexerSingleQuoteStrings) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_single_quotes = 1;

    const char* input = "'hello world'";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.data.string.value_len, 11u);
    EXPECT_EQ(memcmp(token.data.string.value, "hello world", 11), 0);
    json_token_cleanup(&token);
}

/**
 * Test that single-quote strings are rejected when disabled
 */
TEST(JsonTests, LexerSingleQuoteStringsRejected) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_single_quotes = 0;

    const char* input = "'hello'";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test value creation for null
 */
TEST(JsonTests, ValueCreationNull) {
    text_json_value* val = text_json_new_null();
    ASSERT_NE(val, nullptr);

    // Verify type
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_NULL);

    // Value can be freed without crashing
    text_json_free(val);
}

/**
 * Test value creation for boolean
 */
TEST(JsonTests, ValueCreationBool) {
    // Test true
    text_json_value* val_true = text_json_new_bool(1);
    ASSERT_NE(val_true, nullptr);
    EXPECT_EQ(text_json_typeof(val_true), TEXT_JSON_BOOL);
    int bool_val = 0;
    text_json_status status = text_json_get_bool(val_true, &bool_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_NE(bool_val, 0);
    text_json_free(val_true);

    // Test false
    text_json_value* val_false = text_json_new_bool(0);
    ASSERT_NE(val_false, nullptr);
    EXPECT_EQ(text_json_typeof(val_false), TEXT_JSON_BOOL);
    bool_val = 1;
    status = text_json_get_bool(val_false, &bool_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(bool_val, 0);
    text_json_free(val_false);
}

/**
 * Test value creation for string
 */
TEST(JsonTests, ValueCreationString) {
    const char* test_str = "Hello, World!";
    size_t test_len = strlen(test_str);

    text_json_value* val = text_json_new_string(test_str, test_len);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_STRING);
    const char* out_str = nullptr;
    size_t out_len = 0;
    text_json_status status = text_json_get_string(val, &out_str, &out_len);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_len, test_len);
    EXPECT_EQ(memcmp(out_str, test_str, test_len), 0);
    text_json_free(val);

    // Test empty string
    text_json_value* val_empty = text_json_new_string("", 0);
    ASSERT_NE(val_empty, nullptr);
    EXPECT_EQ(text_json_typeof(val_empty), TEXT_JSON_STRING);
    out_str = nullptr;
    out_len = 1;  // Set to non-zero to verify it gets set to 0
    status = text_json_get_string(val_empty, &out_str, &out_len);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_len, 0u);
    text_json_free(val_empty);

    // Test string with null bytes
    const char* null_str = "a\0b\0c";
    size_t null_len = 5;
    text_json_value* val_null = text_json_new_string(null_str, null_len);
    ASSERT_NE(val_null, nullptr);
    EXPECT_EQ(text_json_typeof(val_null), TEXT_JSON_STRING);
    out_str = nullptr;
    out_len = 0;
    status = text_json_get_string(val_null, &out_str, &out_len);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_len, null_len);
    EXPECT_EQ(memcmp(out_str, null_str, null_len), 0);
    text_json_free(val_null);
}

/**
 * Test value creation for number from lexeme
 */
TEST(JsonTests, ValueCreationNumberFromLexeme) {
    const char* lexeme = "123.456";
    size_t lexeme_len = strlen(lexeme);

    text_json_value* val = text_json_new_number_from_lexeme(lexeme, lexeme_len);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_NUMBER);
    const char* out_lexeme = nullptr;
    size_t out_len = 0;
    text_json_status status = text_json_get_number_lexeme(val, &out_lexeme, &out_len);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_len, lexeme_len);
    EXPECT_STREQ(out_lexeme, lexeme);
    text_json_free(val);
}

/**
 * Test value creation for number from int64
 */
TEST(JsonTests, ValueCreationNumberI64) {
    int64_t test_val = 12345;
    text_json_value* val = text_json_new_number_i64(test_val);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_NUMBER);
    int64_t out_val = 0;
    text_json_status status = text_json_get_i64(val, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_val, test_val);
    text_json_free(val);

    // Test negative
    int64_t test_neg = -67890;
    text_json_value* val_neg = text_json_new_number_i64(test_neg);
    ASSERT_NE(val_neg, nullptr);
    EXPECT_EQ(text_json_typeof(val_neg), TEXT_JSON_NUMBER);
    out_val = 0;
    status = text_json_get_i64(val_neg, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_val, test_neg);
    text_json_free(val_neg);

    // Test zero
    text_json_value* val_zero = text_json_new_number_i64(0);
    ASSERT_NE(val_zero, nullptr);
    EXPECT_EQ(text_json_typeof(val_zero), TEXT_JSON_NUMBER);
    out_val = 1;  // Set to non-zero to verify it gets set to 0
    status = text_json_get_i64(val_zero, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_val, 0);
    text_json_free(val_zero);
}

/**
 * Test value creation for number from uint64
 */
TEST(JsonTests, ValueCreationNumberU64) {
    uint64_t test_val = 12345;
    text_json_value* val = text_json_new_number_u64(test_val);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_NUMBER);
    uint64_t out_val = 0;
    text_json_status status = text_json_get_u64(val, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_val, test_val);
    text_json_free(val);

    // Test large value
    uint64_t test_large = UINT64_MAX;
    text_json_value* val_large = text_json_new_number_u64(test_large);
    ASSERT_NE(val_large, nullptr);
    EXPECT_EQ(text_json_typeof(val_large), TEXT_JSON_NUMBER);
    out_val = 0;
    status = text_json_get_u64(val_large, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(out_val, test_large);
    text_json_free(val_large);
}

/**
 * Test value creation for number from double
 */
TEST(JsonTests, ValueCreationNumberDouble) {
    double test_val = 123.456;
    text_json_value* val = text_json_new_number_double(test_val);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_NUMBER);
    double out_val = 0.0;
    text_json_status status = text_json_get_double(val, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_NEAR(out_val, test_val, 0.001);
    text_json_free(val);

    // Test negative
    double test_neg = -789.012;
    text_json_value* val_neg = text_json_new_number_double(test_neg);
    ASSERT_NE(val_neg, nullptr);
    EXPECT_EQ(text_json_typeof(val_neg), TEXT_JSON_NUMBER);
    out_val = 0.0;
    status = text_json_get_double(val_neg, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_NEAR(out_val, test_neg, 0.001);
    text_json_free(val_neg);

    // Test zero
    text_json_value* val_zero = text_json_new_number_double(0.0);
    ASSERT_NE(val_zero, nullptr);
    EXPECT_EQ(text_json_typeof(val_zero), TEXT_JSON_NUMBER);
    out_val = 1.0;  // Set to non-zero to verify it gets set to 0
    status = text_json_get_double(val_zero, &out_val);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_DOUBLE_EQ(out_val, 0.0);
    text_json_free(val_zero);
}

/**
 * Test value creation for array
 */
TEST(JsonTests, ValueCreationArray) {
    text_json_value* val = text_json_new_array();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(val), 0u);
    text_json_free(val);
}

/**
 * Test value creation for object
 */
TEST(JsonTests, ValueCreationObject) {
    text_json_value* val = text_json_new_object();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(val), 0u);
    text_json_free(val);
}

/**
 * Test memory cleanup via text_json_free
 */
TEST(JsonTests, ValueMemoryCleanup) {
    // Create multiple values and verify they're cleaned up
    text_json_value* null_val = text_json_new_null();
    text_json_value* bool_val = text_json_new_bool(1);
    text_json_value* str_val = text_json_new_string("test", 4);
    text_json_value* num_val = text_json_new_number_i64(42);
    text_json_value* arr_val = text_json_new_array();
    text_json_value* obj_val = text_json_new_object();

    ASSERT_NE(null_val, nullptr);
    ASSERT_NE(bool_val, nullptr);
    ASSERT_NE(str_val, nullptr);
    ASSERT_NE(num_val, nullptr);
    ASSERT_NE(arr_val, nullptr);
    ASSERT_NE(obj_val, nullptr);

    // Free all values (should not crash or leak)
    text_json_free(null_val);
    text_json_free(bool_val);
    text_json_free(str_val);
    text_json_free(num_val);
    text_json_free(arr_val);
    text_json_free(obj_val);

    // If we get here, cleanup worked
    EXPECT_TRUE(true);
}

/**
 * Test accessor error cases - wrong type access
 */
TEST(JsonTests, AccessorWrongType) {
    // Create a string value
    text_json_value* str_val = text_json_new_string("test", 4);
    ASSERT_NE(str_val, nullptr);

    // Try to get bool from string - should fail
    int bool_out = 0;
    text_json_status status = text_json_get_bool(str_val, &bool_out);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // Try to get number from string - should fail
    int64_t i64_out = 0;
    status = text_json_get_i64(str_val, &i64_out);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // Try to get array size from string - should return 0
    EXPECT_EQ(text_json_array_size(str_val), 0u);

    text_json_free(str_val);
}

/**
 * Test accessor error cases - null pointer handling
 */
TEST(JsonTests, AccessorNullPointer) {
    // text_json_typeof should handle NULL
    EXPECT_EQ(text_json_typeof(nullptr), TEXT_JSON_NULL);

    // Other accessors should return error for NULL
    int bool_out = 0;
    text_json_status status = text_json_get_bool(nullptr, &bool_out);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    const char* str_out = nullptr;
    size_t str_len = 0;
    status = text_json_get_string(nullptr, &str_out, &str_len);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // Array/object size should return 0 for NULL
    EXPECT_EQ(text_json_array_size(nullptr), 0u);
    EXPECT_EQ(text_json_object_size(nullptr), 0u);

    // Array/object get should return NULL for NULL
    EXPECT_EQ(text_json_array_get(nullptr, 0), nullptr);
    EXPECT_EQ(text_json_object_value(nullptr, 0), nullptr);
    EXPECT_EQ(text_json_object_key(nullptr, 0, nullptr), nullptr);
    EXPECT_EQ(text_json_object_get(nullptr, "key", 3), nullptr);
}

/**
 * Test array access with bounds checking
 */
TEST(JsonTests, ArrayAccessBounds) {
    text_json_value* arr = text_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Empty array - should return NULL for any index
    EXPECT_EQ(text_json_array_get(arr, 0), nullptr);
    EXPECT_EQ(text_json_array_get(arr, 1), nullptr);

    // Note: We can't test with actual elements yet since array mutation
    // functions (text_json_array_push) haven't been implemented (Task 12).
    // This test verifies bounds checking works for empty arrays.

    text_json_free(arr);
}

/**
 * Test object access - key lookup and iteration
 */
TEST(JsonTests, ObjectAccess) {
    text_json_value* obj = text_json_new_object();
    ASSERT_NE(obj, nullptr);

    // Empty object - should return NULL for any key/index
    EXPECT_EQ(text_json_object_get(obj, "key", 3), nullptr);
    EXPECT_EQ(text_json_object_value(obj, 0), nullptr);
    EXPECT_EQ(text_json_object_key(obj, 0, nullptr), nullptr);

    // Note: We can't test with actual key-value pairs yet since object mutation
    // functions (text_json_object_put) haven't been implemented (Task 12).
    // This test verifies bounds checking works for empty objects.

    text_json_free(obj);
}

/**
 * Test number accessor error cases - missing representations
 */
TEST(JsonTests, NumberAccessorMissingRepresentations) {
    // Create number from lexeme only (no numeric representations)
    text_json_value* num = text_json_new_number_from_lexeme("123.456", 7);
    ASSERT_NE(num, nullptr);

    // Lexeme should be available
    const char* lexeme = nullptr;
    size_t lexeme_len = 0;
    text_json_status status = text_json_get_number_lexeme(num, &lexeme, &lexeme_len);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(lexeme, "123.456");

    // int64 should not be available (created from lexeme only)
    int64_t i64_out = 0;
    status = text_json_get_i64(num, &i64_out);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // uint64 should not be available
    uint64_t u64_out = 0;
    status = text_json_get_u64(num, &u64_out);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // double should not be available
    double dbl_out = 0.0;
    status = text_json_get_double(num, &dbl_out);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    text_json_free(num);
}

/**
 * Test duplicate key handling - ERROR policy
 */
TEST(JsonTests, DuplicateKeyError) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_ERROR;

    const char* input = R"({"key": 1, "key": 2})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    EXPECT_EQ(value, nullptr);
    EXPECT_EQ(err.code, TEXT_JSON_E_DUPKEY);
    EXPECT_STREQ(err.message, "Duplicate key in object");
}

/**
 * Test duplicate key handling - FIRST_WINS policy
 */
TEST(JsonTests, DuplicateKeyFirstWins) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_FIRST_WINS;

    const char* input = R"({"key": 1, "key": 2})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(text_json_typeof(value), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(value), 1u);

    // Should have first value (1)
    const text_json_value* val = text_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_NUMBER);

    int64_t i64_out = 0;
    text_json_status status = text_json_get_i64(val, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);

    text_json_free(value);
}

/**
 * Test duplicate key handling - LAST_WINS policy
 */
TEST(JsonTests, DuplicateKeyLastWins) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_LAST_WINS;

    const char* input = R"({"key": 1, "key": 2})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(text_json_typeof(value), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(value), 1u);

    // Should have last value (2)
    const text_json_value* val = text_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_NUMBER);

    int64_t i64_out = 0;
    text_json_status status = text_json_get_i64(val, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);

    text_json_free(value);
}

/**
 * Test duplicate key handling - COLLECT policy (single value to array)
 */
TEST(JsonTests, DuplicateKeyCollectSingle) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_COLLECT;

    const char* input = R"({"key": 1, "key": 2})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(text_json_typeof(value), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(value), 1u);

    // Should have array with [1, 2]
    const text_json_value* val = text_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(val), 2u);

    // First element should be 1
    const text_json_value* elem0 = text_json_array_get(val, 0);
    ASSERT_NE(elem0, nullptr);
    int64_t i64_out = 0;
    text_json_status status = text_json_get_i64(elem0, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);

    // Second element should be 2
    const text_json_value* elem1 = text_json_array_get(val, 1);
    ASSERT_NE(elem1, nullptr);
    status = text_json_get_i64(elem1, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);

    text_json_free(value);
}

/**
 * Test duplicate key handling - COLLECT policy (array to array)
 */
TEST(JsonTests, DuplicateKeyCollectArray) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_COLLECT;

    const char* input = R"({"key": [1, 2], "key": 3})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    if (!value) {
        printf("DEBUG: Parse failed: code=%d, message=%s, offset=%zu, line=%d, col=%d\n", 
               err.code, err.message ? err.message : "(null)", err.offset, err.line, err.col);
        printf("DEBUG: Input around offset: ");
        size_t start = err.offset > 10 ? err.offset - 10 : 0;
        size_t len = strlen(input);
        size_t end = err.offset + 10 < len ? err.offset + 10 : len;
        for (size_t i = start; i < end; i++) {
            printf("%c", input[i]);
        }
        printf("\n");
    }
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(text_json_typeof(value), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(value), 1u);

    // Should have array with [1, 2, 3]
    const text_json_value* val = text_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(val), 3u);

    // Check elements
    const text_json_value* elem0 = text_json_array_get(val, 0);
    ASSERT_NE(elem0, nullptr);
    int64_t i64_out = 0;
    text_json_status status = text_json_get_i64(elem0, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);

    const text_json_value* elem1 = text_json_array_get(val, 1);
    ASSERT_NE(elem1, nullptr);
    status = text_json_get_i64(elem1, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);

    const text_json_value* elem2 = text_json_array_get(val, 2);
    ASSERT_NE(elem2, nullptr);
    status = text_json_get_i64(elem2, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    text_json_free(value);
}

/**
 * Test duplicate key handling - multiple duplicates with COLLECT
 */
TEST(JsonTests, DuplicateKeyCollectMultiple) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_COLLECT;

    const char* input = R"({"key": 1, "key": 2, "key": 3})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(text_json_typeof(value), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(value), 1u);

    // Should have array with [1, 2, 3]
    const text_json_value* val = text_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(val), 3u);

    text_json_free(value);
}

/**
 * Test duplicate key handling - nested objects
 */
TEST(JsonTests, DuplicateKeyNested) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_LAST_WINS;

    const char* input = R"({"outer": {"key": 1, "key": 2}, "outer": {"key": 3}})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(text_json_typeof(value), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(value), 1u);

    // Should have last outer object
    const text_json_value* outer = text_json_object_get(value, "outer", 5);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(text_json_typeof(outer), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(outer), 1u);

    // Inner object should have last value (3)
    const text_json_value* inner = text_json_object_get(outer, "key", 3);
    ASSERT_NE(inner, nullptr);
    int64_t i64_out = 0;
    text_json_status status = text_json_get_i64(inner, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    text_json_free(value);
}

/**
 * Test duplicate key handling - different value types with COLLECT
 */
TEST(JsonTests, DuplicateKeyCollectDifferentTypes) {
    text_json_parse_options opts = text_json_parse_options_default();
    opts.dupkeys = TEXT_JSON_DUPKEY_COLLECT;

    const char* input = R"({"key": "first", "key": 42, "key": true})";
    text_json_error err;
    text_json_value* value = text_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(text_json_typeof(value), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(value), 1u);

    // Should have array with ["first", 42, true]
    const text_json_value* val = text_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(val), 3u);

    // First element should be string "first"
    const text_json_value* elem0 = text_json_array_get(val, 0);
    ASSERT_NE(elem0, nullptr);
    EXPECT_EQ(text_json_typeof(elem0), TEXT_JSON_STRING);
    const char* str_out = nullptr;
    size_t str_len = 0;
    text_json_status status = text_json_get_string(elem0, &str_out, &str_len);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(str_len, 5u);
    EXPECT_STREQ(str_out, "first");

    // Second element should be number 42
    const text_json_value* elem1 = text_json_array_get(val, 1);
    ASSERT_NE(elem1, nullptr);
    EXPECT_EQ(text_json_typeof(elem1), TEXT_JSON_NUMBER);
    int64_t i64_out = 0;
    status = text_json_get_i64(elem1, &i64_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 42);

    // Third element should be boolean true
    const text_json_value* elem2 = text_json_array_get(val, 2);
    ASSERT_NE(elem2, nullptr);
    EXPECT_EQ(text_json_typeof(elem2), TEXT_JSON_BOOL);
    int bool_out = 0;
    status = text_json_get_bool(elem2, &bool_out);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_NE(bool_out, 0);

    text_json_free(value);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
