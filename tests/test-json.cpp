#include <gtest/gtest.h>
#include <text/text.h>
#include <text/json.h>
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
