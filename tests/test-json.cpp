#include <gtest/gtest.h>
#include <text/text.h>
#include <text/json.h>
#include <text/json_dom.h>
#include <text/json_writer.h>
#include <text/json_stream.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <limits>

// Include internal header for testing internal functions
extern "C" {
#include "../src/json_internal.h"
}

/**
 * Test default parse options match specification (strict JSON by default)
 */
TEST(ParseOptions, Default) {
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
TEST(WriteOptions, Default) {
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
TEST(MemoryManagement, FreeNullValue) {
    // Should not crash
    text_json_free(nullptr);
    EXPECT_TRUE(true);  // If we get here, it worked
}

/**
 * Test standard escape sequence decoding
 */
TEST(StringHandling, EscapeSequences) {
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
            JSON_UTF8_REJECT,
            0   // don't allow unescaped controls
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
TEST(StringHandling, UnicodeEscapes) {
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
            JSON_UTF8_REJECT,
            0   // don't allow unescaped controls
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
TEST(StringHandling, SurrogatePairs) {
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
        JSON_UTF8_REJECT,
        0   // don't allow unescaped controls
    );

    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(output_len, expected_len);
    EXPECT_EQ(memcmp(output, expected, expected_len), 0);
}

/**
 * Test invalid escape sequences are rejected
 */
TEST(StringHandling, InvalidEscapes) {
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
            JSON_UTF8_REJECT,
            0   // don't allow unescaped controls
        );

        EXPECT_NE(status, TEXT_JSON_OK) << "Should reject: " << invalid_escapes[i];
    }
}

/**
 * Test position tracking during string decoding
 */
TEST(StringHandling, PositionTracking) {
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
        JSON_UTF8_REJECT,
        0   // don't allow unescaped controls
    );

    EXPECT_EQ(status, TEXT_JSON_OK);
    // Position should be updated (offset should be at end of input)
    EXPECT_EQ(pos.offset, strlen(input));
}

/**
 * Test buffer overflow protection
 */
TEST(StringHandling, BufferOverflowProtection) {
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
        JSON_UTF8_REJECT,
        0   // don't allow unescaped controls
    );

    EXPECT_EQ(status, TEXT_JSON_E_LIMIT);
}

/**
 * Test buffer overflow protection with Unicode escape
 */
TEST(StringHandling, BufferOverflowUnicode) {
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
        JSON_UTF8_REJECT,
        0   // don't allow unescaped controls
    );

    EXPECT_EQ(status, TEXT_JSON_E_LIMIT);
}

/**
 * Test valid number formats
 */
TEST(NumberParsing, ValidFormats) {
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
TEST(NumberParsing, InvalidFormats) {
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
TEST(NumberParsing, Int64Boundaries) {
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
TEST(NumberParsing, Uint64Boundaries) {
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
TEST(NumberParsing, DoubleParsing) {
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
TEST(NumberParsing, NonfiniteNumbers) {
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
TEST(NumberParsing, NonfiniteRejected) {
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
TEST(NumberParsing, LexemePreservation) {
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
TEST(NumberParsing, PositionTracking) {
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
TEST(Lexer, TokenTypes) {
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
TEST(Lexer, Keywords) {
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
TEST(Lexer, StringTokenization) {
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
TEST(Lexer, NumberTokenization) {
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
TEST(Lexer, Comments) {
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
TEST(Lexer, CommentsRejected) {
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
TEST(Lexer, PositionTracking) {
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
TEST(Lexer, ExtensionTokens) {
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
TEST(Lexer, ExtensionTokensRejected) {
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
TEST(Lexer, WhitespaceHandling) {
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
TEST(Lexer, ErrorReporting) {
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
TEST(Lexer, SingleQuoteStrings) {
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
TEST(Lexer, SingleQuoteStringsRejected) {
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
 * Test unescaped control characters are rejected by default
 */
TEST(Lexer, UnescapedControlsRejected) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_unescaped_controls = 0;

    // Test with tab character (0x09) - should be rejected
    const char* input = "\"hello\tworld\"";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);

    // Test with newline (0x0A) - should be rejected
    const char* input2 = "\"hello\nworld\"";
    status = json_lexer_init(&lexer, input2, strlen(input2), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);

    // Test with null byte (0x00) - should be rejected
    const char input3[] = "\"hello\0world\"";
    status = json_lexer_init(&lexer, input3, sizeof(input3) - 1, &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test unescaped control characters are allowed when option enabled
 */
TEST(Lexer, UnescapedControlsAllowed) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_unescaped_controls = 1;

    // Test with tab character (0x09) - should be allowed
    const char* input = "\"hello\tworld\"";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.data.string.value_len, 11u);  // "hello\tworld" = 11 bytes
    EXPECT_EQ(memcmp(token.data.string.value, "hello\tworld", 11), 0);
    json_token_cleanup(&token);

    // Test with newline (0x0A) - should be allowed
    const char* input2 = "\"hello\nworld\"";
    status = json_lexer_init(&lexer, input2, strlen(input2), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.data.string.value_len, 11u);  // "hello\nworld" = 11 bytes
    EXPECT_EQ(memcmp(token.data.string.value, "hello\nworld", 11), 0);
    json_token_cleanup(&token);
}

/**
 * Test that all extensions work together
 */
TEST(Lexer, AllExtensionsCombined) {
    json_lexer lexer;
    json_token token;
    text_json_parse_options opts = text_json_parse_options_default();
    opts.allow_comments = 1;
    opts.allow_nonfinite_numbers = 1;
    opts.allow_single_quotes = 1;
    opts.allow_unescaped_controls = 1;

    // Input with comments, single quotes, nonfinite numbers, and unescaped controls
    const char* input = "// comment\n'hello\tworld' Infinity NaN";
    text_json_status status = json_lexer_init(&lexer, input, strlen(input), &opts);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should skip comment and get single-quoted string with tab
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.data.string.value_len, 11u);
    EXPECT_EQ(memcmp(token.data.string.value, "hello\tworld", 11), 0);
    json_token_cleanup(&token);

    // Should get Infinity
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_INFINITY);
    json_token_cleanup(&token);

    // Should get NaN
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NAN);
    json_token_cleanup(&token);
}

/**
 * Test that extensions are opt-in (strict by default)
 */
TEST(Parser, ExtensionsOptIn) {
    text_json_parse_options opts = text_json_parse_options_default();

    // Verify all extensions are off by default
    EXPECT_EQ(opts.allow_comments, 0);
    EXPECT_EQ(opts.allow_trailing_commas, 0);
    EXPECT_EQ(opts.allow_nonfinite_numbers, 0);
    EXPECT_EQ(opts.allow_single_quotes, 0);
    EXPECT_EQ(opts.allow_unescaped_controls, 0);

    // Test that strict JSON is parsed correctly
    const char* strict_json = "{\"key\": \"value\", \"number\": 123}";
    text_json_value* val = text_json_parse(strict_json, strlen(strict_json), &opts, NULL);
    EXPECT_NE(val, nullptr);
    text_json_free(val);

    // Test that extensions are rejected by default
    const char* with_comment = "{\"key\": \"value\" // comment\n}";
    text_json_value* val2 = text_json_parse(with_comment, strlen(with_comment), &opts, NULL);
    EXPECT_EQ(val2, nullptr);  // Should fail because comments are disabled

    const char* with_trailing = "{\"key\": \"value\",}";
    text_json_value* val3 = text_json_parse(with_trailing, strlen(with_trailing), &opts, NULL);
    EXPECT_EQ(val3, nullptr);  // Should fail because trailing commas are disabled
}

/**
 * Test value creation for null
 */
TEST(DOMValueCreation, Null) {
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
TEST(DOMValueCreation, Bool) {
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
TEST(DOMValueCreation, String) {
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
TEST(DOMValueCreation, NumberFromLexeme) {
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
TEST(DOMValueCreation, NumberI64) {
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
TEST(DOMValueCreation, NumberU64) {
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
TEST(DOMValueCreation, NumberDouble) {
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
TEST(DOMValueCreation, Array) {
    text_json_value* val = text_json_new_array();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(val), 0u);
    text_json_free(val);
}

/**
 * Test value creation for object
 */
TEST(DOMValueCreation, Object) {
    text_json_value* val = text_json_new_object();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(text_json_typeof(val), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(val), 0u);
    text_json_free(val);
}

/**
 * Test memory cleanup via text_json_free
 */
TEST(MemoryManagement, ValueCleanup) {
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
TEST(DOMAccessors, WrongType) {
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
TEST(DOMAccessors, NullPointer) {
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
TEST(DOMAccessors, ArrayAccessBounds) {
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
TEST(DOMAccessors, ObjectAccess) {
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
TEST(DOMAccessors, NumberAccessorMissingRepresentations) {
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
 * Test array mutation - push, set, insert, remove
 */
TEST(DOMMutation, ArrayPush) {
    text_json_value* arr = text_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push some elements
    text_json_value* val1 = text_json_new_number_i64(42);
    text_json_value* val2 = text_json_new_string("hello", 5);
    text_json_value* val3 = text_json_new_bool(1);

    EXPECT_EQ(text_json_array_push(arr, val1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val2), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val3), TEXT_JSON_OK);

    // Verify array size and contents
    EXPECT_EQ(text_json_array_size(arr), 3u);
    EXPECT_EQ(text_json_array_get(arr, 0), val1);
    EXPECT_EQ(text_json_array_get(arr, 1), val2);
    EXPECT_EQ(text_json_array_get(arr, 2), val3);

    // Verify values
    int64_t i64_out = 0;
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 0), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 42);

    const char* str_out = nullptr;
    size_t str_len = 0;
    EXPECT_EQ(text_json_get_string(text_json_array_get(arr, 1), &str_out, &str_len), TEXT_JSON_OK);
    EXPECT_EQ(str_len, 5u);
    EXPECT_STREQ(str_out, "hello");

    int bool_out = 0;
    EXPECT_EQ(text_json_get_bool(text_json_array_get(arr, 2), &bool_out), TEXT_JSON_OK);
    EXPECT_NE(bool_out, 0);

    text_json_free(arr);
}

TEST(DOMMutation, ArraySet) {
    text_json_value* arr = text_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push initial elements
    text_json_value* val1 = text_json_new_number_i64(1);
    text_json_value* val2 = text_json_new_number_i64(2);
    text_json_value* val3 = text_json_new_number_i64(3);

    EXPECT_EQ(text_json_array_push(arr, val1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val2), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val3), TEXT_JSON_OK);

    // Replace element at index 1
    text_json_value* new_val = text_json_new_number_i64(99);
    EXPECT_EQ(text_json_array_set(arr, 1, new_val), TEXT_JSON_OK);

    // Verify
    int64_t i64_out = 0;
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 0), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 1), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 99);  // Changed
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 2), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    // Test out of bounds
    text_json_value* val4 = text_json_new_number_i64(4);
    EXPECT_EQ(text_json_array_set(arr, 10, val4), TEXT_JSON_E_INVALID);
    text_json_free(val4);

    text_json_free(arr);
}

TEST(DOMMutation, ArrayInsert) {
    text_json_value* arr = text_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push initial elements
    text_json_value* val1 = text_json_new_number_i64(1);
    text_json_value* val2 = text_json_new_number_i64(2);
    text_json_value* val3 = text_json_new_number_i64(3);

    EXPECT_EQ(text_json_array_push(arr, val1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val2), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val3), TEXT_JSON_OK);

    // Insert at index 1
    text_json_value* new_val = text_json_new_number_i64(99);
    EXPECT_EQ(text_json_array_insert(arr, 1, new_val), TEXT_JSON_OK);

    // Verify: [1, 99, 2, 3]
    EXPECT_EQ(text_json_array_size(arr), 4u);
    int64_t i64_out = 0;
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 0), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 1), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 99);
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 2), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 3), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    // Insert at end (should work like push)
    text_json_value* val_end = text_json_new_number_i64(100);
    EXPECT_EQ(text_json_array_insert(arr, 4, val_end), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_size(arr), 5u);

    // Test out of bounds
    text_json_value* val4 = text_json_new_number_i64(4);
    EXPECT_EQ(text_json_array_insert(arr, 10, val4), TEXT_JSON_E_INVALID);
    text_json_free(val4);

    text_json_free(arr);
}

TEST(DOMMutation, ArrayRemove) {
    text_json_value* arr = text_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push initial elements
    text_json_value* val1 = text_json_new_number_i64(1);
    text_json_value* val2 = text_json_new_number_i64(2);
    text_json_value* val3 = text_json_new_number_i64(3);
    text_json_value* val4 = text_json_new_number_i64(4);

    EXPECT_EQ(text_json_array_push(arr, val1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val2), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val3), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, val4), TEXT_JSON_OK);

    // Remove element at index 1 (value 2)
    EXPECT_EQ(text_json_array_remove(arr, 1), TEXT_JSON_OK);

    // Verify: [1, 3, 4]
    EXPECT_EQ(text_json_array_size(arr), 3u);
    int64_t i64_out = 0;
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 0), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 1), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);
    EXPECT_EQ(text_json_get_i64(text_json_array_get(arr, 2), &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 4);

    // Remove first element
    EXPECT_EQ(text_json_array_remove(arr, 0), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_size(arr), 2u);

    // Remove last element
    EXPECT_EQ(text_json_array_remove(arr, 1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_size(arr), 1u);

    // Test out of bounds
    EXPECT_EQ(text_json_array_remove(arr, 10), TEXT_JSON_E_INVALID);

    text_json_free(arr);
}

TEST(DOMMutation, ObjectPut) {
    text_json_value* obj = text_json_new_object();
    ASSERT_NE(obj, nullptr);

    // Add key-value pairs
    text_json_value* val1 = text_json_new_number_i64(42);
    text_json_value* val2 = text_json_new_string("hello", 5);
    text_json_value* val3 = text_json_new_bool(1);

    EXPECT_EQ(text_json_object_put(obj, "key1", 4, val1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_put(obj, "key2", 4, val2), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_put(obj, "key3", 4, val3), TEXT_JSON_OK);

    // Verify object size
    EXPECT_EQ(text_json_object_size(obj), 3u);

    // Verify values
    const text_json_value* v1 = text_json_object_get(obj, "key1", 4);
    ASSERT_NE(v1, nullptr);
    int64_t i64_out = 0;
    EXPECT_EQ(text_json_get_i64(v1, &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 42);

    const text_json_value* v2 = text_json_object_get(obj, "key2", 4);
    ASSERT_NE(v2, nullptr);
    const char* str_out = nullptr;
    size_t str_len = 0;
    EXPECT_EQ(text_json_get_string(v2, &str_out, &str_len), TEXT_JSON_OK);
    EXPECT_STREQ(str_out, "hello");

    // Replace existing key
    text_json_value* new_val = text_json_new_number_i64(99);
    EXPECT_EQ(text_json_object_put(obj, "key1", 4, new_val), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_size(obj), 3u);  // Size should not change

    const text_json_value* v1_new = text_json_object_get(obj, "key1", 4);
    ASSERT_NE(v1_new, nullptr);
    EXPECT_EQ(text_json_get_i64(v1_new, &i64_out), TEXT_JSON_OK);
    EXPECT_EQ(i64_out, 99);  // Changed

    text_json_free(obj);
}

TEST(DOMMutation, ObjectRemove) {
    text_json_value* obj = text_json_new_object();
    ASSERT_NE(obj, nullptr);

    // Add key-value pairs
    text_json_value* val1 = text_json_new_number_i64(1);
    text_json_value* val2 = text_json_new_number_i64(2);
    text_json_value* val3 = text_json_new_number_i64(3);

    EXPECT_EQ(text_json_object_put(obj, "key1", 4, val1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_put(obj, "key2", 4, val2), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_put(obj, "key3", 4, val3), TEXT_JSON_OK);

    EXPECT_EQ(text_json_object_size(obj), 3u);

    // Remove middle key
    EXPECT_EQ(text_json_object_remove(obj, "key2", 4), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_size(obj), 2u);

    // Verify key2 is gone
    EXPECT_EQ(text_json_object_get(obj, "key2", 4), nullptr);
    EXPECT_NE(text_json_object_get(obj, "key1", 4), nullptr);
    EXPECT_NE(text_json_object_get(obj, "key3", 4), nullptr);

    // Remove first key
    EXPECT_EQ(text_json_object_remove(obj, "key1", 4), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_size(obj), 1u);

    // Remove last key
    EXPECT_EQ(text_json_object_remove(obj, "key3", 4), TEXT_JSON_OK);
    EXPECT_EQ(text_json_object_size(obj), 0u);

    // Try to remove non-existent key
    EXPECT_EQ(text_json_object_remove(obj, "nonexistent", 11), TEXT_JSON_E_INVALID);

    text_json_free(obj);
}

TEST(DOMMutation, NestedStructures) {
    // Test building nested structures
    text_json_value* root = text_json_new_object();
    ASSERT_NE(root, nullptr);

    // Create nested array
    text_json_value* arr = text_json_new_array();
    text_json_value* elem1 = text_json_new_number_i64(1);
    text_json_value* elem2 = text_json_new_number_i64(2);
    EXPECT_EQ(text_json_array_push(arr, elem1), TEXT_JSON_OK);
    EXPECT_EQ(text_json_array_push(arr, elem2), TEXT_JSON_OK);

    // Add array to object
    EXPECT_EQ(text_json_object_put(root, "array", 5, arr), TEXT_JSON_OK);

    // Create nested object
    text_json_value* nested_obj = text_json_new_object();
    text_json_value* nested_val = text_json_new_string("nested", 6);
    EXPECT_EQ(text_json_object_put(nested_obj, "key", 3, nested_val), TEXT_JSON_OK);

    // Add nested object to root
    EXPECT_EQ(text_json_object_put(root, "object", 6, nested_obj), TEXT_JSON_OK);

    // Verify structure
    EXPECT_EQ(text_json_object_size(root), 2u);

    const text_json_value* arr_val = text_json_object_get(root, "array", 5);
    ASSERT_NE(arr_val, nullptr);
    EXPECT_EQ(text_json_typeof(arr_val), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(arr_val), 2u);

    const text_json_value* obj_val = text_json_object_get(root, "object", 6);
    ASSERT_NE(obj_val, nullptr);
    EXPECT_EQ(text_json_typeof(obj_val), TEXT_JSON_OBJECT);
    EXPECT_EQ(text_json_object_size(obj_val), 1u);

    text_json_free(root);
}

TEST(DOMMutation, ErrorCases) {
    // Test error cases for array operations
    text_json_value* arr = text_json_new_array();
    text_json_value* val = text_json_new_number_i64(1);

    // NULL array
    EXPECT_EQ(text_json_array_push(nullptr, val), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_array_set(nullptr, 0, val), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_array_insert(nullptr, 0, val), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_array_remove(nullptr, 0), TEXT_JSON_E_INVALID);

    // NULL value
    EXPECT_EQ(text_json_array_push(arr, nullptr), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_array_set(arr, 0, nullptr), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_array_insert(arr, 0, nullptr), TEXT_JSON_E_INVALID);

    // Wrong type
    text_json_value* obj = text_json_new_object();
    EXPECT_EQ(text_json_array_push(obj, val), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_array_set(obj, 0, val), TEXT_JSON_E_INVALID);

    text_json_free(arr);
    text_json_free(val);
    text_json_free(obj);

    // Test error cases for object operations
    text_json_value* obj2 = text_json_new_object();
    text_json_value* val2 = text_json_new_number_i64(2);

    // NULL object
    EXPECT_EQ(text_json_object_put(nullptr, "key", 3, val2), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_object_remove(nullptr, "key", 3), TEXT_JSON_E_INVALID);

    // NULL key
    EXPECT_EQ(text_json_object_put(obj2, nullptr, 3, val2), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_object_remove(obj2, nullptr, 3), TEXT_JSON_E_INVALID);

    // NULL value
    EXPECT_EQ(text_json_object_put(obj2, "key", 3, nullptr), TEXT_JSON_E_INVALID);

    // Wrong type
    text_json_value* arr2 = text_json_new_array();
    EXPECT_EQ(text_json_object_put(arr2, "key", 3, val2), TEXT_JSON_E_INVALID);
    EXPECT_EQ(text_json_object_remove(arr2, "key", 3), TEXT_JSON_E_INVALID);

    text_json_free(obj2);
    text_json_free(val2);
    text_json_free(arr2);
}

/**
 * Test duplicate key handling - ERROR policy
 */
TEST(DuplicateKeyHandling, Error) {
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
TEST(DuplicateKeyHandling, FirstWins) {
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
TEST(DuplicateKeyHandling, LastWins) {
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
TEST(DuplicateKeyHandling, CollectSingle) {
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
TEST(DuplicateKeyHandling, CollectArray) {
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
TEST(DuplicateKeyHandling, CollectMultiple) {
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
TEST(DuplicateKeyHandling, Nested) {
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
TEST(DuplicateKeyHandling, CollectDifferentTypes) {
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

/**
 * Test sink abstraction - custom callback sink
 */
TEST(SinkAbstraction, CallbackSink) {
    std::string output;

    // Custom write callback that appends to string
    auto write_callback = [](void* user, const char* bytes, size_t len) -> int {
        std::string* str = (std::string*)user;
        str->append(bytes, len);
        return 0;
    };

    text_json_sink sink;
    sink.write = write_callback;
    sink.user = &output;

    // Write some data
    const char* test_data = "Hello, World!";
    int result = sink.write(sink.user, test_data, strlen(test_data));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(output, "Hello, World!");

    // Write more data
    const char* more_data = " Test";
    result = sink.write(sink.user, more_data, strlen(more_data));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(output, "Hello, World! Test");
}

/**
 * Test sink abstraction - growable buffer sink
 */
TEST(SinkAbstraction, GrowableBuffer) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Initially empty
    EXPECT_EQ(text_json_sink_buffer_size(&sink), 0u);
    const char* data = text_json_sink_buffer_data(&sink);
    ASSERT_NE(data, nullptr);
    EXPECT_STREQ(data, "");

    // Write some data
    const char* test1 = "Hello";
    int result = sink.write(sink.user, test1, strlen(test1));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(text_json_sink_buffer_size(&sink), 5u);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "Hello");

    // Write more data
    const char* test2 = ", World!";
    result = sink.write(sink.user, test2, strlen(test2));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(text_json_sink_buffer_size(&sink), 13u);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "Hello, World!");

    // Write large amount of data to test growth
    std::string large_data(1000, 'A');
    result = sink.write(sink.user, large_data.c_str(), large_data.size());
    EXPECT_EQ(result, 0);
    EXPECT_EQ(text_json_sink_buffer_size(&sink), 1013u);

    // Verify data integrity
    data = text_json_sink_buffer_data(&sink);
    EXPECT_EQ(strncmp(data, "Hello, World!", 13), 0);
    EXPECT_EQ(data[1012], 'A');

    // Clean up
    text_json_sink_buffer_free(&sink);
    EXPECT_EQ(sink.write, nullptr);
    EXPECT_EQ(sink.user, nullptr);
}

/**
 * Test sink abstraction - fixed buffer sink
 */
TEST(SinkAbstraction, FixedBuffer) {
    char buffer[64];
    text_json_sink sink;

    text_json_status status = text_json_sink_fixed_buffer(&sink, buffer, sizeof(buffer));
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Initially empty
    EXPECT_EQ(text_json_sink_fixed_buffer_used(&sink), 0u);
    EXPECT_EQ(text_json_sink_fixed_buffer_truncated(&sink), 0);
    EXPECT_STREQ(buffer, "");

    // Write some data
    const char* test1 = "Hello";
    int result = sink.write(sink.user, test1, strlen(test1));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(text_json_sink_fixed_buffer_used(&sink), 5u);
    EXPECT_EQ(text_json_sink_fixed_buffer_truncated(&sink), 0);
    EXPECT_STREQ(buffer, "Hello");

    // Write more data
    const char* test2 = ", World!";
    result = sink.write(sink.user, test2, strlen(test2));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(text_json_sink_fixed_buffer_used(&sink), 13u);
    EXPECT_EQ(text_json_sink_fixed_buffer_truncated(&sink), 0);
    EXPECT_STREQ(buffer, "Hello, World!");

    // Write data that fits exactly
    const char* test3 = " This fits";
    result = sink.write(sink.user, test3, strlen(test3));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(text_json_sink_fixed_buffer_used(&sink), 23u);
    EXPECT_EQ(text_json_sink_fixed_buffer_truncated(&sink), 0);

    // Write data that exceeds buffer (should truncate)
    // After 23 bytes, we have 64 - 23 - 1 = 40 bytes available
    // This string is 50 bytes, so it will exceed and truncate
    const char* test4 = " This is way too long and will definitely be truncated";
    result = sink.write(sink.user, test4, strlen(test4));
    EXPECT_NE(result, 0); // Should return error on truncation
    EXPECT_EQ(text_json_sink_fixed_buffer_truncated(&sink), 1);
    // Should have written up to buffer limit (63 bytes, leaving 1 for null terminator)
    EXPECT_EQ(text_json_sink_fixed_buffer_used(&sink), sizeof(buffer) - 1);

    // Clean up
    text_json_sink_fixed_buffer_free(&sink);
}

/**
 * Test sink abstraction - fixed buffer edge cases
 */
TEST(SinkAbstraction, FixedBufferEdgeCases) {
    // Test with size 1 buffer (only null terminator)
    char tiny_buffer[1];
    text_json_sink sink;

    text_json_status status = text_json_sink_fixed_buffer(&sink, tiny_buffer, 1);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Try to write (should truncate immediately)
    const char* test = "X";
    int result = sink.write(sink.user, test, 1);
    EXPECT_NE(result, 0); // Should return error
    EXPECT_EQ(text_json_sink_fixed_buffer_truncated(&sink), 1);
    EXPECT_EQ(text_json_sink_fixed_buffer_used(&sink), 0u);
    EXPECT_EQ(tiny_buffer[0], '\0');

    // Test invalid parameters
    status = text_json_sink_fixed_buffer(nullptr, tiny_buffer, 1);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    char buf[10];
    status = text_json_sink_fixed_buffer(&sink, nullptr, 10);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    status = text_json_sink_fixed_buffer(&sink, buf, 0);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // Clean up
    text_json_sink_fixed_buffer_free(&sink);
}

/**
 * Test sink abstraction - growable buffer edge cases
 */
TEST(SinkAbstraction, GrowableBufferEdgeCases) {
    // Test invalid parameters
    text_json_status status = text_json_sink_buffer(nullptr);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // Test empty buffer access
    text_json_sink sink;
    status = text_json_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_JSON_OK);

    const char* data = text_json_sink_buffer_data(nullptr);
    EXPECT_EQ(data, nullptr);

    size_t size = text_json_sink_buffer_size(nullptr);
    EXPECT_EQ(size, 0u);

    // Test free with invalid sink
    text_json_sink invalid_sink = {nullptr, nullptr};
    text_json_sink_buffer_free(&invalid_sink); // Should not crash

    // Clean up valid sink
    text_json_sink_buffer_free(&sink);
}

/**
 * Test sink abstraction - error propagation
 */
TEST(SinkAbstraction, ErrorPropagation) {
    // Custom callback that returns error
    auto error_callback = [](void* user, const char* bytes, size_t len) -> int {
        (void)user;
        (void)bytes;
        (void)len;
        return 1; // Error
    };

    text_json_sink sink;
    sink.write = error_callback;
    sink.user = nullptr;

    const char* test = "test";
    int result = sink.write(sink.user, test, strlen(test));
    EXPECT_NE(result, 0); // Should propagate error
}

/**
 * Test DOM write - null value
 */
TEST(DOMWrite, Null) {
    text_json_value* v = text_json_new_null();
    ASSERT_NE(v, nullptr);

    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_write_options opts = text_json_write_options_default();
    text_json_error err;
    status = text_json_write_value(&sink, &opts, v, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "null");

    text_json_sink_buffer_free(&sink);
    text_json_free(v);
}

/**
 * Test DOM write - boolean values
 */
TEST(DOMWrite, Boolean) {
    text_json_value* v_true = text_json_new_bool(1);
    text_json_value* v_false = text_json_new_bool(0);
    ASSERT_NE(v_true, nullptr);
    ASSERT_NE(v_false, nullptr);

    text_json_sink sink;
    text_json_sink_buffer(&sink);

    text_json_write_options opts = text_json_write_options_default();
    text_json_error err;

    text_json_write_value(&sink, &opts, v_true, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "true");

    text_json_sink_buffer_free(&sink);
    text_json_sink_buffer(&sink);

    text_json_write_value(&sink, &opts, v_false, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "false");

    text_json_sink_buffer_free(&sink);
    text_json_free(v_true);
    text_json_free(v_false);
}

/**
 * Test DOM write - string values with escaping
 */
TEST(DOMWrite, StringEscaping) {
    text_json_value* v1 = text_json_new_string("hello", 5);
    text_json_value* v2 = text_json_new_string("he\"llo", 6);
    text_json_value* v3 = text_json_new_string("he\\llo", 6);
    text_json_value* v4 = text_json_new_string("he\nllo", 6);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);
    ASSERT_NE(v4, nullptr);

    text_json_sink sink;
    text_json_write_options opts = text_json_write_options_default();
    text_json_error err;

    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &opts, v1, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "\"hello\"");
    text_json_sink_buffer_free(&sink);

    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &opts, v2, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "\"he\\\"llo\"");
    text_json_sink_buffer_free(&sink);

    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &opts, v3, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "\"he\\\\llo\"");
    text_json_sink_buffer_free(&sink);

    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &opts, v4, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "\"he\\nllo\"");
    text_json_sink_buffer_free(&sink);

    text_json_free(v1);
    text_json_free(v2);
    text_json_free(v3);
    text_json_free(v4);
}

/**
 * Test DOM write - number values
 */
TEST(DOMWrite, Number) {
    text_json_value* v1 = text_json_new_number_i64(123);
    text_json_value* v2 = text_json_new_number_u64(456u);
    text_json_value* v3 = text_json_new_number_double(3.14);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    text_json_sink sink;
    text_json_write_options opts = text_json_write_options_default();
    text_json_error err;

    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &opts, v1, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "123");
    text_json_sink_buffer_free(&sink);

    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &opts, v2, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "456");
    text_json_sink_buffer_free(&sink);

    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &opts, v3, &err);
    // Double output format may vary, just check it's not empty
    EXPECT_GT(text_json_sink_buffer_size(&sink), 0u);
    text_json_sink_buffer_free(&sink);

    text_json_free(v1);
    text_json_free(v2);
    text_json_free(v3);
}

/**
 * Test DOM write - array values
 */
TEST(DOMWrite, Array) {
    text_json_value* arr = text_json_new_array();
    ASSERT_NE(arr, nullptr);

    text_json_value* v1 = text_json_new_number_i64(1);
    text_json_value* v2 = text_json_new_string("two", 3);
    text_json_value* v3 = text_json_new_bool(1);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    text_json_array_push(arr, v1);
    text_json_array_push(arr, v2);
    text_json_array_push(arr, v3);

    text_json_sink sink;
    text_json_sink_buffer(&sink);
    text_json_write_options opts = text_json_write_options_default();
    text_json_error err;

    text_json_write_value(&sink, &opts, arr, &err);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "[1,\"two\",true]");

    text_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    text_json_free(arr);
}

/**
 * Test DOM write - object values
 */
TEST(DOMWrite, Object) {
    text_json_value* obj = text_json_new_object();
    ASSERT_NE(obj, nullptr);

    text_json_value* v1 = text_json_new_number_i64(42);
    text_json_value* v2 = text_json_new_string("value", 5);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);

    text_json_object_put(obj, "key1", 4, v1);
    text_json_object_put(obj, "key2", 4, v2);

    text_json_sink sink;
    text_json_sink_buffer(&sink);
    text_json_write_options opts = text_json_write_options_default();
    text_json_error err;

    text_json_write_value(&sink, &opts, obj, &err);
    // Output order may vary, check it contains both keys
    const char* output = text_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "key1"), nullptr);
    EXPECT_NE(strstr(output, "key2"), nullptr);
    EXPECT_NE(strstr(output, "42"), nullptr);
    EXPECT_NE(strstr(output, "value"), nullptr);

    text_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    text_json_free(obj);
}

/**
 * Test DOM write - pretty printing
 */
TEST(DOMWrite, PrettyPrint) {
    text_json_value* obj = text_json_new_object();
    ASSERT_NE(obj, nullptr);

    text_json_value* arr = text_json_new_array();
    text_json_value* v1 = text_json_new_number_i64(1);
    text_json_value* v2 = text_json_new_string("test", 4);
    text_json_array_push(arr, v1);
    text_json_array_push(arr, v2);

    text_json_object_put(obj, "array", 5, arr);

    text_json_sink sink;
    text_json_sink_buffer(&sink);
    text_json_write_options opts = text_json_write_options_default();
    opts.pretty = 1;
    opts.indent_spaces = 2;
    text_json_error err;

    text_json_write_value(&sink, &opts, obj, &err);
    const char* output = text_json_sink_buffer_data(&sink);
    // Pretty print should contain newlines
    EXPECT_NE(strchr(output, '\n'), nullptr);
    // Should contain indentation
    EXPECT_NE(strstr(output, "  "), nullptr);

    text_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    text_json_free(obj);
}

/**
 * Test DOM write - key sorting for canonical output
 */
TEST(DOMWrite, KeySorting) {
    text_json_value* obj = text_json_new_object();
    ASSERT_NE(obj, nullptr);

    text_json_value* v1 = text_json_new_string("first", 5);
    text_json_value* v2 = text_json_new_string("second", 6);
    text_json_value* v3 = text_json_new_string("third", 5);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    // Add keys in non-alphabetical order
    text_json_object_put(obj, "zebra", 5, v1);
    text_json_object_put(obj, "apple", 5, v2);
    text_json_object_put(obj, "banana", 6, v3);

    text_json_sink sink;
    text_json_sink_buffer(&sink);
    text_json_write_options opts = text_json_write_options_default();
    opts.sort_object_keys = 1;
    text_json_error err;

    text_json_write_value(&sink, &opts, obj, &err);
    const char* output = text_json_sink_buffer_data(&sink);
    // With sorting, "apple" should come before "banana", and "banana" before "zebra"
    const char* apple_pos = strstr(output, "apple");
    const char* banana_pos = strstr(output, "banana");
    const char* zebra_pos = strstr(output, "zebra");
    ASSERT_NE(apple_pos, nullptr);
    ASSERT_NE(banana_pos, nullptr);
    ASSERT_NE(zebra_pos, nullptr);
    EXPECT_LT(apple_pos, banana_pos);
    EXPECT_LT(banana_pos, zebra_pos);

    text_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    text_json_free(obj);
}

/**
 * Test DOM write - error handling
 */
TEST(DOMWrite, ErrorHandling) {
    text_json_sink sink;
    text_json_write_options opts = text_json_write_options_default();
    text_json_error err;

    // NULL sink
    text_json_value* v = text_json_new_null();
    text_json_status status = text_json_write_value(nullptr, &opts, v, &err);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);
    text_json_free(v);

    // NULL value
    text_json_sink_buffer(&sink);
    status = text_json_write_value(&sink, &opts, nullptr, &err);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test DOM write - round trip (parse then write)
 */
TEST(DOMWrite, RoundTrip) {
    const char* input = "{\"key\":[1,2,\"three\",true,null]}";
    text_json_parse_options parse_opts = text_json_parse_options_default();
    text_json_error err;

    text_json_value* parsed = text_json_parse(input, strlen(input), &parse_opts, &err);
    ASSERT_NE(parsed, nullptr);

    text_json_sink sink;
    text_json_sink_buffer(&sink);
    text_json_write_options write_opts = text_json_write_options_default();
    text_json_write_value(&sink, &write_opts, parsed, &err);

    const char* output = text_json_sink_buffer_data(&sink);
    // Output should be valid JSON (we can parse it again)
    text_json_value* reparsed = text_json_parse(output, text_json_sink_buffer_size(&sink), &parse_opts, &err);
    EXPECT_NE(reparsed, nullptr);

    text_json_sink_buffer_free(&sink);
    text_json_free(parsed);
    text_json_free(reparsed);
}

/**
 * Test streaming parser - stream creation and destruction
 */
TEST(StreamingParser, CreationAndDestruction) {
    // Test creation with NULL callback (should fail)
    text_json_parse_options opts = text_json_parse_options_default();
    text_json_stream* st = text_json_stream_new(&opts, nullptr, nullptr);
    EXPECT_EQ(st, nullptr);

    // Test creation with valid callback
    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)user;
        (void)evt;
        (void)err;
        return TEXT_JSON_OK;
    };
    st = text_json_stream_new(&opts, callback, nullptr);
    EXPECT_NE(st, nullptr);

    // Test destruction
    text_json_stream_free(st);
    text_json_stream_free(nullptr);  // Should be safe

    // Test creation with NULL options (should use defaults)
    st = text_json_stream_new(nullptr, callback, nullptr);
    EXPECT_NE(st, nullptr);
    text_json_stream_free(st);
}

/**
 * Test streaming parser - callback invocation (basic)
 *
 * For Task 15, we just test that the stream can be created and that
 * callbacks can be set up. Actual parsing will be tested in Task 16.
 */
TEST(StreamingParser, CallbackSetup) {
    text_json_parse_options opts = text_json_parse_options_default();

    // Track callback invocations
    struct {
        std::vector<text_json_event_type> events;
        void* user_data;
    } context = {{}, (void*)0x12345};

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)err;
        auto* ctx = static_cast<decltype(context)*>(user);
        if (ctx) {
            ctx->events.push_back(evt->type);
        }
        return TEXT_JSON_OK;
    };

    text_json_stream* st = text_json_stream_new(&opts, callback, &context);
    ASSERT_NE(st, nullptr);

    // For Task 15, we just verify the stream is set up correctly
    // Actual event emission will be tested in Task 16
    EXPECT_EQ(context.events.size(), 0u);

    text_json_stream_free(st);
}

/**
 * Test streaming parser - stream state persistence
 *
 * Test that the stream maintains state across feed calls.
 * For Task 15, we just verify the infrastructure is in place.
 */
TEST(StreamingParser, StatePersistence) {
    text_json_parse_options opts = text_json_parse_options_default();

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)user;
        (void)evt;
        (void)err;
        return TEXT_JSON_OK;
    };

    text_json_stream* st = text_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    text_json_error err;

    // Feed empty input (should be OK)
    text_json_status status = text_json_stream_feed(st, "", 0, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Feed some data - should parse successfully
    const char* data = "null";
    status = text_json_stream_feed(st, data, strlen(data), &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Finish should succeed after parsing a complete value
    status = text_json_stream_finish(st, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // After finish, stream is in DONE state - feeding more should fail
    status = text_json_stream_feed(st, " true", 5, &err);
    EXPECT_NE(status, TEXT_JSON_OK);  // Should fail - stream is done

    text_json_stream_free(st);
}

/**
 * Test streaming parser - error handling
 */
TEST(StreamingParser, ErrorHandling) {
    text_json_parse_options opts = text_json_parse_options_default();

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)user;
        (void)evt;
        (void)err;
        return TEXT_JSON_OK;
    };

    text_json_error err;

    // NULL stream
    text_json_status status = text_json_stream_feed(nullptr, "null", 4, &err);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    status = text_json_stream_finish(nullptr, &err);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    // NULL bytes with non-zero length
    text_json_stream* st = text_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    status = text_json_stream_feed(st, nullptr, 10, &err);
    EXPECT_EQ(status, TEXT_JSON_E_INVALID);

    text_json_stream_free(st);
}

/**
 * Test streaming parser - basic value parsing (null, bool, number, string)
 */
TEST(StreamingParser, BasicValues) {
    text_json_parse_options opts = text_json_parse_options_default();

    struct {
        const char* input;
        text_json_event_type expected_type;
    } tests[] = {
        {"null", TEXT_JSON_EVT_NULL},
        {"true", TEXT_JSON_EVT_BOOL},
        {"false", TEXT_JSON_EVT_BOOL},
        {"123", TEXT_JSON_EVT_NUMBER},
        {"\"hello\"", TEXT_JSON_EVT_STRING},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        std::vector<text_json_event_type> events;

        auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
            (void)err;
            auto* evts = static_cast<std::vector<text_json_event_type>*>(user);
            evts->push_back(evt->type);
            return TEXT_JSON_OK;
        };

        text_json_stream* st = text_json_stream_new(&opts, callback, &events);
        ASSERT_NE(st, nullptr);

        text_json_error err;
        text_json_status status = text_json_stream_feed(st, tests[i].input, strlen(tests[i].input), &err);
        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed for input: " << tests[i].input;

        status = text_json_stream_finish(st, &err);
        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed to finish for input: " << tests[i].input;

        EXPECT_EQ(events.size(), 1u) << "Expected 1 event for: " << tests[i].input;
        if (events.size() == 1) {
            EXPECT_EQ(events[0], tests[i].expected_type) << "Event type mismatch for: " << tests[i].input;
        }

        text_json_stream_free(st);
    }
}

/**
 * Test streaming parser - array parsing
 */
TEST(StreamingParser, Arrays) {
    text_json_parse_options opts = text_json_parse_options_default();

    std::vector<text_json_event_type> events;

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)err;
        auto* evts = static_cast<std::vector<text_json_event_type>*>(user);
        evts->push_back(evt->type);
        return TEXT_JSON_OK;
    };

    text_json_stream* st = text_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char* input = "[1, 2, 3]";
    text_json_error err;
    text_json_status status = text_json_stream_feed(st, input, strlen(input), &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_stream_finish(st, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Expected events: ARRAY_BEGIN, NUMBER, NUMBER, NUMBER, ARRAY_END
    EXPECT_EQ(events.size(), 5u);
    if (events.size() >= 5) {
        EXPECT_EQ(events[0], TEXT_JSON_EVT_ARRAY_BEGIN);
        EXPECT_EQ(events[1], TEXT_JSON_EVT_NUMBER);
        EXPECT_EQ(events[2], TEXT_JSON_EVT_NUMBER);
        EXPECT_EQ(events[3], TEXT_JSON_EVT_NUMBER);
        EXPECT_EQ(events[4], TEXT_JSON_EVT_ARRAY_END);
    }

    text_json_stream_free(st);
}

/**
 * Test streaming parser - object parsing
 */
TEST(StreamingParser, Objects) {
    text_json_parse_options opts = text_json_parse_options_default();

    std::vector<text_json_event_type> events;

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)err;
        auto* evts = static_cast<std::vector<text_json_event_type>*>(user);
        evts->push_back(evt->type);
        return TEXT_JSON_OK;
    };

    text_json_stream* st = text_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char* input = "{\"key\": \"value\"}";
    text_json_error err;
    text_json_status status = text_json_stream_feed(st, input, strlen(input), &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_stream_finish(st, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Expected events: OBJECT_BEGIN, KEY, STRING, OBJECT_END
    EXPECT_EQ(events.size(), 4u);
    if (events.size() >= 4) {
        EXPECT_EQ(events[0], TEXT_JSON_EVT_OBJECT_BEGIN);
        EXPECT_EQ(events[1], TEXT_JSON_EVT_KEY);
        EXPECT_EQ(events[2], TEXT_JSON_EVT_STRING);
        EXPECT_EQ(events[3], TEXT_JSON_EVT_OBJECT_END);
    }

    text_json_stream_free(st);
}

/**
 * Test streaming parser - incremental/chunked input
 */
TEST(StreamingParser, IncrementalInput) {
    text_json_parse_options opts = text_json_parse_options_default();

    std::vector<text_json_event_type> events;

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)err;
        auto* evts = static_cast<std::vector<text_json_event_type>*>(user);
        evts->push_back(evt->type);
        return TEXT_JSON_OK;
    };

    text_json_stream* st = text_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char* input = "[1, 2, 3]";
    text_json_error err;

    // Feed byte by byte
    for (size_t i = 0; i < strlen(input); ++i) {
        text_json_status status = text_json_stream_feed(st, input + i, 1, &err);
        EXPECT_EQ(status, TEXT_JSON_OK) << "Failed at byte " << i;
    }

    text_json_status status = text_json_stream_finish(st, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should have received all events
    EXPECT_EQ(events.size(), 5u);

    text_json_stream_free(st);
}

/**
 * Test streaming parser - nested structures
 */
TEST(StreamingParser, NestedStructures) {
    text_json_parse_options opts = text_json_parse_options_default();

    std::vector<text_json_event_type> events;

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)err;
        auto* evts = static_cast<std::vector<text_json_event_type>*>(user);
        evts->push_back(evt->type);
        return TEXT_JSON_OK;
    };

    text_json_stream* st = text_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char* input = "{\"arr\": [1, 2], \"obj\": {\"key\": \"value\"}}";
    text_json_error err;
    text_json_status status = text_json_stream_feed(st, input, strlen(input), &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_stream_finish(st, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Should have received events for nested structure
    EXPECT_GT(events.size(), 5u);

    text_json_stream_free(st);
}

/**
 * Test streaming parser - error handling (invalid JSON)
 */
TEST(StreamingParser, InvalidJSON) {
    text_json_parse_options opts = text_json_parse_options_default();

    auto callback = [](void* user, const text_json_event* evt, text_json_error* err) -> text_json_status {
        (void)user;
        (void)evt;
        (void)err;
        return TEXT_JSON_OK;
    };

    text_json_stream* st = text_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    text_json_error err;

    // Invalid: missing comma
    const char* invalid1 = "[1 2]";
    text_json_status status = text_json_stream_feed(st, invalid1, strlen(invalid1), &err);
    // Should either succeed (buffering) or fail
    status = text_json_stream_finish(st, &err);
    EXPECT_NE(status, TEXT_JSON_OK);  // Should fail on invalid JSON

    text_json_stream_free(st);

    // Invalid: incomplete structure
    st = text_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    const char* invalid2 = "[1, 2";
    status = text_json_stream_feed(st, invalid2, strlen(invalid2), &err);
    status = text_json_stream_finish(st, &err);
    EXPECT_NE(status, TEXT_JSON_OK);  // Should fail on incomplete structure

    text_json_stream_free(st);
}

// ============================================================================
// Streaming Writer Tests
// ============================================================================

/**
 * Test streaming writer - creation and destruction
 */
TEST(StreamingWriter, Creation) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - NULL sink
 */
TEST(StreamingWriter, NullSink) {
    text_json_sink sink = {nullptr, nullptr};
    text_json_writer* w = text_json_writer_new(sink, nullptr);
    EXPECT_EQ(w, nullptr);
}

/**
 * Test streaming writer - basic value writing (null, bool, number, string)
 */
TEST(StreamingWriter, BasicValues) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write null
    status = text_json_writer_null(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Finish
    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Verify output
    const char* output = text_json_sink_buffer_data(&sink);
    size_t output_len = text_json_sink_buffer_size(&sink);
    EXPECT_STREQ(output, "null");
    EXPECT_EQ(output_len, 4u);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test bool
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = text_json_writer_bool(w, 1);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    output = text_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "true");

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test number (i64)
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = text_json_writer_number_i64(w, 12345);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    output = text_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "12345");

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test string
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = text_json_writer_string(w, "hello", 5);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    output = text_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "\"hello\"");

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - array writing
 */
TEST(StreamingWriter, Arrays) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write array: [1, 2, 3]
    status = text_json_writer_array_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_number_i64(w, 1);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_number_i64(w, 2);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_number_i64(w, 3);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_array_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Verify output
    const char* output = text_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "[1, 2, 3]");

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - object writing
 */
TEST(StreamingWriter, Objects) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write object: {"key": "value"}
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_string(w, "value", 5);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_object_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Verify output
    const char* output = text_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "{\"key\":\"value\"}");

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - structural enforcement (invalid sequences)
 */
TEST(StreamingWriter, StructuralEnforcement) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Try to write value without key in object - should fail
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_null(w);  // Should fail - need key first
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_STATE);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Try to write key when not in object - should fail
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = text_json_writer_key(w, "key", 3);  // Should fail - not in object
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_STATE);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Try to end object when expecting key - should fail
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Now try to end object without writing value - should fail
    status = text_json_writer_object_end(w);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_STATE);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - finish validation (incomplete structure)
 */
TEST(StreamingWriter, FinishValidation) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Start object but don't close it
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INCOMPLETE);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Start array but don't close it
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = text_json_writer_array_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_finish(w, &err);
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_INCOMPLETE);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - pretty-print mode
 */
TEST(StreamingWriter, PrettyPrint) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_write_options opts = text_json_write_options_default();
    opts.pretty = 1;
    opts.indent_spaces = 2;

    text_json_writer* w = text_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);

    // Write object with pretty printing
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_string(w, "value", 5);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_object_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Verify output has newlines and indentation
    const char* output = text_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "\n"), nullptr);  // Should have newline

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - nested structures
 */
TEST(StreamingWriter, NestedStructures) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write: {"arr": [1, 2], "obj": {"key": "value"}}
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_key(w, "arr", 3);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_array_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_number_i64(w, 1);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_number_i64(w, 2);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_array_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_key(w, "obj", 3);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_string(w, "value", 5);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_object_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_object_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Verify output
    const char* output = text_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "\"arr\""), nullptr);
    EXPECT_NE(strstr(output, "\"obj\""), nullptr);
    EXPECT_NE(strstr(output, "\"key\""), nullptr);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - number formats (i64, u64, double, lexeme)
 */
TEST(StreamingWriter, NumberFormats) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Test i64
    status = text_json_writer_number_i64(w, -12345);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "-12345");
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test u64
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);
    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);
    status = text_json_writer_number_u64(w, 12345ULL);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "12345");
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test double
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);
    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);
    status = text_json_writer_number_double(w, 3.14159);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, TEXT_JSON_OK);
    const char* output = text_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "3.14"), nullptr);  // Should contain the number
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test lexeme
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);
    w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);
    status = text_json_writer_number_lexeme(w, "123.456", 7);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "123.456");
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - nonfinite numbers
 */
TEST(StreamingWriter, NonfiniteNumbers) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_write_options opts = text_json_write_options_default();
    opts.allow_nonfinite_numbers = 1;

    text_json_writer* w = text_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);

    // Test NaN
    status = text_json_writer_number_double(w, std::nan(""));
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "NaN");
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test Infinity
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);
    w = text_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);
    status = text_json_writer_number_double(w, std::numeric_limits<double>::infinity());
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "Infinity");
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test -Infinity
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);
    w = text_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);
    status = text_json_writer_number_double(w, -std::numeric_limits<double>::infinity());
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, TEXT_JSON_OK);
    EXPECT_STREQ(text_json_sink_buffer_data(&sink), "-Infinity");
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);

    // Test that nonfinite numbers are rejected when option is off
    opts.allow_nonfinite_numbers = 0;
    status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);
    w = text_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);
    status = text_json_writer_number_double(w, std::nan(""));
    EXPECT_NE(status, TEXT_JSON_OK);
    EXPECT_EQ(status, TEXT_JSON_E_NONFINITE);
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - round-trip (write then parse)
 */
TEST(StreamingWriter, RoundTrip) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write: {"key": [1, 2, 3]}
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_array_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_number_i64(w, 1);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_number_i64(w, 2);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_number_i64(w, 3);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_array_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);
    status = text_json_writer_object_end(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_EQ(status, TEXT_JSON_OK);

    // Parse the output
    const char* output = text_json_sink_buffer_data(&sink);
    size_t output_len = text_json_sink_buffer_size(&sink);

    text_json_parse_options parse_opts = text_json_parse_options_default();
    text_json_value* v = text_json_parse(output, output_len, &parse_opts, &err);
    ASSERT_NE(v, nullptr);

    // Verify structure
    EXPECT_EQ(text_json_typeof(v), TEXT_JSON_OBJECT);
    const text_json_value* arr = text_json_object_get(v, "key", 3);
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(text_json_typeof(arr), TEXT_JSON_ARRAY);
    EXPECT_EQ(text_json_array_size(arr), 3u);

    text_json_free(v);
    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - error state handling
 */
TEST(StreamingWriter, ErrorState) {
    text_json_sink sink;
    text_json_status status = text_json_sink_buffer(&sink);
    ASSERT_EQ(status, TEXT_JSON_OK);

    text_json_writer* w = text_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Cause an error (try to write value without key in object)
    status = text_json_writer_object_begin(w);
    EXPECT_EQ(status, TEXT_JSON_OK);

    status = text_json_writer_null(w);  // Should fail
    EXPECT_NE(status, TEXT_JSON_OK);

    // After error, subsequent operations should fail
    status = text_json_writer_key(w, "key", 3);
    EXPECT_NE(status, TEXT_JSON_OK);  // Actually, this might succeed, but finish should fail

    text_json_error err;
    status = text_json_writer_finish(w, &err);
    EXPECT_NE(status, TEXT_JSON_OK);

    text_json_writer_free(w);
    text_json_sink_buffer_free(&sink);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
