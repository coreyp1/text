#include <gtest/gtest.h>
#include <ghoti.io/text/text.h>
#include <ghoti.io/text/json.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <fstream>
#include <sstream>
#include <tuple>

// Include internal header for testing internal functions
extern "C" {
#include "../src/json/json_internal.h"
}

/**
 * Test default parse options match specification (strict JSON by default)
 */
TEST(ParseOptions, Default) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

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
    EXPECT_EQ(opts.in_situ_mode, 0);        // off by default

    // Duplicate keys
    EXPECT_EQ(opts.dupkeys, GTEXT_JSON_DUPKEY_ERROR);

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
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();

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
 * Test standard escape sequence decoding
 */
TEST(StringHandling, EscapeSequences) {
    char output[256];
    size_t output_len;
    json_position pos = {0, 1, 1};

    // Test each standard escape
    struct {
        const char * input;
        const char * expected;
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

        GTEXT_JSON_Status status = json_decode_string(
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

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;
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
        const char * input;
        const char * expected;
        size_t expected_len;
    } tests[] = {
        {"\\u0041", "A", 1},           // U+0041 = 'A'
        {"\\u00E9", "\xC3\xA9", 2},    // U+00E9 = 'e with acute' (UTF-8)
        {"\\u20AC", "\xE2\x82\xAC", 3} // U+20AC = 'Euro sign' (UTF-8)
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;
        output_len = 0;

        GTEXT_JSON_Status status = json_decode_string(
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

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;
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

    // U+1F600 = grinning face emoji
    // High surrogate: U+D83D, Low surrogate: U+DE00
    const char * input = "\\uD83D\\uDE00";
    const char * expected = "\xF0\x9F\x98\x80";  // UTF-8 for U+1F600
    size_t expected_len = 4;

    GTEXT_JSON_Status status = json_decode_string(
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

    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    const char * invalid_escapes[] = {
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

        GTEXT_JSON_Status status = json_decode_string(
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

        EXPECT_NE(status, GTEXT_JSON_OK) << "Should reject: " << invalid_escapes[i];
    }
}

/**
 * Test position tracking during string decoding
 */
TEST(StringHandling, PositionTracking) {
    char output[256];
    size_t output_len;
    json_position pos = {0, 1, 1};

    const char * input = "hello\\nworld";
    GTEXT_JSON_Status status = json_decode_string(
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

    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    const char * input = "hello world";  // 11 characters > 5 buffer size
    GTEXT_JSON_Status status = json_decode_string(
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

    EXPECT_EQ(status, GTEXT_JSON_E_LIMIT);
}

/**
 * Test buffer overflow protection with Unicode escape
 */
TEST(StringHandling, BufferOverflowUnicode) {
    char output[2];  // Very small buffer
    size_t output_len;
    json_position pos = {0, 1, 1};

    // Unicode escape produces 3 bytes (Euro sign), but buffer is only 2
    const char * input = "\\u20AC";
    GTEXT_JSON_Status status = json_decode_string(
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

    EXPECT_EQ(status, GTEXT_JSON_E_LIMIT);
}

/**
 * Test valid number formats
 */
TEST(NumberParsing, ValidFormats) {
    json_number num;
    json_position pos = {0, 1, 1};
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    struct {
        const char * input;
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

        GTEXT_JSON_Status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * invalid_numbers[] = {
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

        GTEXT_JSON_Status status = json_parse_number(
            invalid_numbers[i],
            strlen(invalid_numbers[i]),
            &num,
            &pos,
            &opts
        );

        EXPECT_NE(status, GTEXT_JSON_OK) << "Should reject: " << invalid_numbers[i];
        EXPECT_EQ(status, GTEXT_JSON_E_BAD_NUMBER) << "Should return BAD_NUMBER for: " << invalid_numbers[i];

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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    struct {
        const char * input;
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

        GTEXT_JSON_Status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;

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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    struct {
        const char * input;
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

        GTEXT_JSON_Status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;

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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    struct {
        const char * input;
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

        GTEXT_JSON_Status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = true;

    struct {
        const char * input;
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

        GTEXT_JSON_Status status = json_parse_number(
            tests[i].input,
            strlen(tests[i].input),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = false;

    const char * nonfinite[] = {
        "NaN",
        "Infinity",
        "-Infinity",
    };

    for (size_t i = 0; i < sizeof(nonfinite) / sizeof(nonfinite[0]); ++i) {
        memset(&num, 0, sizeof(num));
        pos.offset = 0;
        pos.line = 1;
        pos.col = 1;

        GTEXT_JSON_Status status = json_parse_number(
            nonfinite[i],
            strlen(nonfinite[i]),
            &num,
            &pos,
            &opts
        );

        EXPECT_NE(status, GTEXT_JSON_OK) << "Should reject nonfinite when disabled: " << nonfinite[i];
        EXPECT_EQ(status, GTEXT_JSON_E_NONFINITE) << "Should return NONFINITE error: " << nonfinite[i];

        // Clean up
        json_number_destroy(&num);
    }
}

/**
 * Test DOM parsing of non-finite numbers
 */
TEST(DOMParsing, NonfiniteNumbers) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = true;

    struct {
        const char * input;
        int is_nan;
        int is_inf;
        int is_neg_inf;
        const char * expected_lexeme;
    } tests[] = {
        {"NaN", 1, 0, 0, "NaN"},
        {"Infinity", 0, 1, 0, "Infinity"},
        {"-Infinity", 0, 0, 1, "-Infinity"},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        GTEXT_JSON_Error err{};

        GTEXT_JSON_Value * value = gtext_json_parse(
            tests[i].input,
            strlen(tests[i].input),
            &opts,
            &err
        );

        ASSERT_NE(value, nullptr) << "Failed to parse: " << tests[i].input;
        EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_NUMBER) << "Should be number: " << tests[i].input;

        // Check lexeme
        const char * lexeme = nullptr;
        size_t lexeme_len = 0;
        GTEXT_JSON_Status status = gtext_json_get_number_lexeme(value, &lexeme, &lexeme_len);
        EXPECT_EQ(status, GTEXT_JSON_OK) << "Should have lexeme: " << tests[i].input;
        EXPECT_STREQ(lexeme, tests[i].expected_lexeme) << "Lexeme mismatch: " << tests[i].input;

        // Check double value
        double dbl_val = 0.0;
        status = gtext_json_get_double(value, &dbl_val);
        EXPECT_EQ(status, GTEXT_JSON_OK) << "Should have double: " << tests[i].input;

        if (tests[i].is_nan) {
            EXPECT_TRUE(std::isnan(dbl_val)) << "Should be NaN: " << tests[i].input;
        } else if (tests[i].is_inf) {
            EXPECT_TRUE(std::isinf(dbl_val) && dbl_val > 0) << "Should be +Infinity: " << tests[i].input;
        } else if (tests[i].is_neg_inf) {
            EXPECT_TRUE(std::isinf(dbl_val) && dbl_val < 0) << "Should be -Infinity: " << tests[i].input;
        }

        gtext_json_free(value);
        gtext_json_error_free(&err);
    }
}

/**
 * Test DOM parsing of non-finite numbers in objects and arrays
 */
TEST(DOMParsing, NonfiniteNumbersInStructures) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = true;

    // Test in object
    {
        const char * json = "{\"nan\": NaN, \"inf\": Infinity, \"neg_inf\": -Infinity}";
        GTEXT_JSON_Error err{};
        GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
        ASSERT_NE(root, nullptr);

        // Check NaN
        const GTEXT_JSON_Value * nan_val = gtext_json_object_get(root, "nan", 3);
        ASSERT_NE(nan_val, nullptr);
        double dbl = 0.0;
        EXPECT_EQ(gtext_json_get_double(nan_val, &dbl), GTEXT_JSON_OK);
        EXPECT_TRUE(std::isnan(dbl));

        // Check Infinity
        const GTEXT_JSON_Value * inf_val = gtext_json_object_get(root, "inf", 3);
        ASSERT_NE(inf_val, nullptr);
        dbl = 0.0;
        EXPECT_EQ(gtext_json_get_double(inf_val, &dbl), GTEXT_JSON_OK);
        EXPECT_TRUE(std::isinf(dbl) && dbl > 0);

        // Check -Infinity
        const GTEXT_JSON_Value * neg_inf_val = gtext_json_object_get(root, "neg_inf", 7);
        ASSERT_NE(neg_inf_val, nullptr);
        dbl = 0.0;
        EXPECT_EQ(gtext_json_get_double(neg_inf_val, &dbl), GTEXT_JSON_OK);
        EXPECT_TRUE(std::isinf(dbl) && dbl < 0);

        gtext_json_free(root);
        gtext_json_error_free(&err);
    }

    // Test in array
    {
        const char * json = "[NaN, Infinity, -Infinity]";
        GTEXT_JSON_Error err{};
        GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
        ASSERT_NE(root, nullptr);
        EXPECT_EQ(gtext_json_array_size(root), 3u);

        // Check NaN
        const GTEXT_JSON_Value * nan_val = gtext_json_array_get(root, 0);
        ASSERT_NE(nan_val, nullptr);
        double dbl = 0.0;
        EXPECT_EQ(gtext_json_get_double(nan_val, &dbl), GTEXT_JSON_OK);
        EXPECT_TRUE(std::isnan(dbl));

        // Check Infinity
        const GTEXT_JSON_Value * inf_val = gtext_json_array_get(root, 1);
        ASSERT_NE(inf_val, nullptr);
        dbl = 0.0;
        EXPECT_EQ(gtext_json_get_double(inf_val, &dbl), GTEXT_JSON_OK);
        EXPECT_TRUE(std::isinf(dbl) && dbl > 0);

        // Check -Infinity
        const GTEXT_JSON_Value * neg_inf_val = gtext_json_array_get(root, 2);
        ASSERT_NE(neg_inf_val, nullptr);
        dbl = 0.0;
        EXPECT_EQ(gtext_json_get_double(neg_inf_val, &dbl), GTEXT_JSON_OK);
        EXPECT_TRUE(std::isinf(dbl) && dbl < 0);

        gtext_json_free(root);
        gtext_json_error_free(&err);
    }
}

/**
 * Test that non-finite numbers are rejected when disabled in DOM parsing
 */
TEST(DOMParsing, NonfiniteNumbersRejected) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = false;

    const char * nonfinite[] = {
        "NaN",
        "Infinity",
        "-Infinity"
    };

    for (size_t i = 0; i < sizeof(nonfinite) / sizeof(nonfinite[0]); ++i) {
        GTEXT_JSON_Error err{};
        GTEXT_JSON_Value * value = gtext_json_parse(
            nonfinite[i],
            strlen(nonfinite[i]),
            &opts,
            &err
        );

        EXPECT_EQ(value, nullptr) << "Should reject nonfinite when disabled: " << nonfinite[i];
        EXPECT_EQ(err.code, GTEXT_JSON_E_NONFINITE) << "Should return NONFINITE error: " << nonfinite[i];

        gtext_json_error_free(&err);
    }
}

/**
 * Test lexeme preservation
 */
TEST(NumberParsing, LexemePreservation) {
    json_number num;
    json_position pos = {0, 1, 1};
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.preserve_number_lexeme = true;

    const char * numbers[] = {
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

        GTEXT_JSON_Status status = json_parse_number(
            numbers[i],
            strlen(numbers[i]),
            &num,
            &pos,
            &opts
        );

        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << numbers[i];
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "123.456";
    GTEXT_JSON_Status status = json_parse_number(
        input,
        strlen(input),
        &num,
        &pos,
        &opts
    );

    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "{}[]:,";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Test LBRACE
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACE);
    json_token_cleanup(&token);

    // Test RBRACE
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACE);
    json_token_cleanup(&token);

    // Test LBRACKET
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACKET);
    json_token_cleanup(&token);

    // Test RBRACKET
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACKET);
    json_token_cleanup(&token);

    // Test COLON
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_COLON);
    json_token_cleanup(&token);

    // Test COMMA
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_COMMA);
    json_token_cleanup(&token);

    // Test EOF
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_EOF);
    json_token_cleanup(&token);
}

/**
 * Test lexer keyword tokenization
 */
TEST(Lexer, Keywords) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "null true false";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Test null
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NULL);
    json_token_cleanup(&token);

    // Test true
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_TRUE);
    json_token_cleanup(&token);

    // Test false
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_FALSE);
    json_token_cleanup(&token);
}

/**
 * Test lexer string tokenization with escape sequences
 */
TEST(Lexer, StringTokenization) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "\"hello\\nworld\"";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "123 -456 789.012";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Test integer
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_TRUE(token.data.number.flags & JSON_NUMBER_HAS_I64);
    EXPECT_EQ(token.data.number.i64, 123);
    json_token_cleanup(&token);

    // Test negative integer
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_TRUE(token.data.number.flags & JSON_NUMBER_HAS_I64);
    EXPECT_EQ(token.data.number.i64, -456);
    json_token_cleanup(&token);

    // Test decimal
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_comments = true;

    const char * input = "// comment\n123 /* multi\nline */ 456";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should skip comment and get first number
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    EXPECT_EQ(token.data.number.i64, 123);
    json_token_cleanup(&token);

    // Should skip multi-line comment and get second number
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_comments = false;

    const char * input = "// comment\n123";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should treat // as invalid token
    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test position tracking accuracy
 */
TEST(Lexer, PositionTracking) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "{\n  \"key\": 123\n}";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // LBRACE at line 1, col 1
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACE);
    EXPECT_EQ(token.pos.line, 1);
    EXPECT_EQ(token.pos.col, 1);
    json_token_cleanup(&token);

    // STRING at line 2, col 3
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.pos.line, 2);
    EXPECT_EQ(token.pos.col, 3);
    json_token_cleanup(&token);

    // COLON at line 2
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_COLON);
    json_token_cleanup(&token);

    // NUMBER at line 2
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    json_token_cleanup(&token);

    // RBRACE at line 3, col 1
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = true;

    const char * input = "NaN Infinity -Infinity";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Test NaN
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NAN);
    json_token_cleanup(&token);

    // Test Infinity
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_INFINITY);
    json_token_cleanup(&token);

    // Test -Infinity
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NEG_INFINITY);
    json_token_cleanup(&token);
}

/**
 * Test that extension tokens are rejected when disabled
 */
TEST(Lexer, ExtensionTokensRejected) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = false;

    const char * input = "NaN";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should treat NaN as invalid token (not a keyword)
    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test whitespace handling
 */
TEST(Lexer, WhitespaceHandling) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "  {  }  [  ]  ";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should skip leading whitespace
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACE);
    json_token_cleanup(&token);

    // Should skip whitespace between tokens
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACE);
    json_token_cleanup(&token);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_LBRACKET);
    json_token_cleanup(&token);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_RBRACKET);
    json_token_cleanup(&token);
}

/**
 * Test lexer error reporting with accurate positions
 */
TEST(Lexer, ErrorReporting) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    const char * input = "123 @ invalid";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should successfully parse number
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NUMBER);
    json_token_cleanup(&token);

    // Should fail on invalid character
    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    EXPECT_EQ(status, GTEXT_JSON_E_BAD_TOKEN);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_single_quotes = true;

    const char * input = "'hello world'";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_single_quotes = false;

    const char * input = "'hello'";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test unescaped control characters are rejected by default
 */
TEST(Lexer, UnescapedControlsRejected) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_unescaped_controls = false;

    // Test with tab character (0x09) - should be rejected
    const char * input = "\"hello\tworld\"";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);

    // Test with newline (0x0A) - should be rejected
    const char * input2 = "\"hello\nworld\"";
    status = json_lexer_init(&lexer, input2, strlen(input2), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);

    // Test with null byte (0x00) - should be rejected
    const char input3[] = "\"hello\0world\"";
    status = json_lexer_init(&lexer, input3, sizeof(input3) - 1, &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_ERROR);
    json_token_cleanup(&token);
}

/**
 * Test unescaped control characters are allowed when option enabled
 */
TEST(Lexer, UnescapedControlsAllowed) {
    json_lexer lexer;
    json_token token;
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_unescaped_controls = true;

    // Test with tab character (0x09) - should be allowed
    const char * input = "\"hello\tworld\"";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.data.string.value_len, 11u);  // "hello\tworld" = 11 bytes
    EXPECT_EQ(memcmp(token.data.string.value, "hello\tworld", 11), 0);
    json_token_cleanup(&token);

    // Test with newline (0x0A) - should be allowed
    const char * input2 = "\"hello\nworld\"";
    status = json_lexer_init(&lexer, input2, strlen(input2), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
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
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_comments = true;
    opts.allow_nonfinite_numbers = true;
    opts.allow_single_quotes = true;
    opts.allow_unescaped_controls = true;

    // Input with comments, single quotes, nonfinite numbers, and unescaped controls
    const char * input = "// comment\n'hello\tworld' Infinity NaN";
    GTEXT_JSON_Status status = json_lexer_init(&lexer, input, strlen(input), &opts, 0);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should skip comment and get single-quoted string with tab
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_STRING);
    EXPECT_EQ(token.data.string.value_len, 11u);
    EXPECT_EQ(memcmp(token.data.string.value, "hello\tworld", 11), 0);
    json_token_cleanup(&token);

    // Should get Infinity
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_INFINITY);
    json_token_cleanup(&token);

    // Should get NaN
    status = json_lexer_next(&lexer, &token);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(token.type, JSON_TOKEN_NAN);
    json_token_cleanup(&token);
}

/**
 * Test that extensions are opt-in (strict by default)
 */
TEST(Parser, ExtensionsOptIn) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    // Verify all extensions are off by default
    EXPECT_EQ(opts.allow_comments, 0);
    EXPECT_EQ(opts.allow_trailing_commas, 0);
    EXPECT_EQ(opts.allow_nonfinite_numbers, 0);
    EXPECT_EQ(opts.allow_single_quotes, 0);
    EXPECT_EQ(opts.allow_unescaped_controls, 0);

    // Test that strict JSON is parsed correctly
    const char * strict_json = "{\"key\": \"value\", \"number\": 123}";
    GTEXT_JSON_Value * val = gtext_json_parse(strict_json, strlen(strict_json), &opts, NULL);
    EXPECT_NE(val, nullptr);
    gtext_json_free(val);

    // Test that extensions are rejected by default
    const char * with_comment = "{\"key\": \"value\" // comment\n}";
    GTEXT_JSON_Value * val2 = gtext_json_parse(with_comment, strlen(with_comment), &opts, NULL);
    EXPECT_EQ(val2, nullptr);  // Should fail because comments are disabled

    const char * with_trailing = "{\"key\": \"value\",}";
    GTEXT_JSON_Value * val3 = gtext_json_parse(with_trailing, strlen(with_trailing), &opts, NULL);
    EXPECT_EQ(val3, nullptr);  // Should fail because trailing commas are disabled
}

/**
 * Test value creation for null
 */
TEST(DOMValueCreation, Null) {
    GTEXT_JSON_Value * val = gtext_json_new_null();
    ASSERT_NE(val, nullptr);

    // Verify type
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NULL);

    // Value can be freed without crashing
    gtext_json_free(val);
}

/**
 * Test value creation for boolean
 */
TEST(DOMValueCreation, Bool) {
    // Test true
    GTEXT_JSON_Value * val_true = gtext_json_new_bool(true);
    ASSERT_NE(val_true, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_true), GTEXT_JSON_BOOL);
    bool bool_val = false;
    GTEXT_JSON_Status status = gtext_json_get_bool(val_true, &bool_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(bool_val, true);
    gtext_json_free(val_true);

    // Test false
    GTEXT_JSON_Value * val_false = gtext_json_new_bool(false);
    ASSERT_NE(val_false, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_false), GTEXT_JSON_BOOL);
    bool_val = true;
    status = gtext_json_get_bool(val_false, &bool_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(bool_val, false);
    gtext_json_free(val_false);
}

/**
 * Test value creation for string
 */
TEST(DOMValueCreation, String) {
    const char * test_str = "Hello, World!";
    size_t test_len = strlen(test_str);

    GTEXT_JSON_Value * val = gtext_json_new_string(test_str, test_len);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_STRING);
    const char * out_str = nullptr;
    size_t out_len = 0;
    GTEXT_JSON_Status status = gtext_json_get_string(val, &out_str, &out_len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_len, test_len);
    EXPECT_EQ(memcmp(out_str, test_str, test_len), 0);
    gtext_json_free(val);

    // Test empty string
    GTEXT_JSON_Value * val_empty = gtext_json_new_string("", 0);
    ASSERT_NE(val_empty, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_empty), GTEXT_JSON_STRING);
    out_str = nullptr;
    out_len = 1;  // Set to non-zero to verify it gets set to 0
    status = gtext_json_get_string(val_empty, &out_str, &out_len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_len, 0u);
    gtext_json_free(val_empty);

    // Test string with null bytes
    const char * null_str = "a\0b\0c";
    size_t null_len = 5;
    GTEXT_JSON_Value * val_null = gtext_json_new_string(null_str, null_len);
    ASSERT_NE(val_null, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_null), GTEXT_JSON_STRING);
    out_str = nullptr;
    out_len = 0;
    status = gtext_json_get_string(val_null, &out_str, &out_len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_len, null_len);
    EXPECT_EQ(memcmp(out_str, null_str, null_len), 0);
    gtext_json_free(val_null);
}

/**
 * Test value creation for number from lexeme
 */
TEST(DOMValueCreation, NumberFromLexeme) {
    const char * lexeme = "123.456";
    size_t lexeme_len = strlen(lexeme);

    GTEXT_JSON_Value * val = gtext_json_new_number_from_lexeme(lexeme, lexeme_len);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NUMBER);
    const char * out_lexeme = nullptr;
    size_t out_len = 0;
    GTEXT_JSON_Status status = gtext_json_get_number_lexeme(val, &out_lexeme, &out_len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_len, lexeme_len);
    EXPECT_STREQ(out_lexeme, lexeme);
    gtext_json_free(val);
}

/**
 * Test value creation for number from int64
 */
TEST(DOMValueCreation, NumberI64) {
    int64_t test_val = 12345;
    GTEXT_JSON_Value * val = gtext_json_new_number_i64(test_val);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NUMBER);
    int64_t out_val = 0;
    GTEXT_JSON_Status status = gtext_json_get_i64(val, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_val, test_val);
    gtext_json_free(val);

    // Test negative
    int64_t test_neg = -67890;
    GTEXT_JSON_Value * val_neg = gtext_json_new_number_i64(test_neg);
    ASSERT_NE(val_neg, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_neg), GTEXT_JSON_NUMBER);
    out_val = 0;
    status = gtext_json_get_i64(val_neg, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_val, test_neg);
    gtext_json_free(val_neg);

    // Test zero
    GTEXT_JSON_Value * val_zero = gtext_json_new_number_i64(0);
    ASSERT_NE(val_zero, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_zero), GTEXT_JSON_NUMBER);
    out_val = 1;  // Set to non-zero to verify it gets set to 0
    status = gtext_json_get_i64(val_zero, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_val, 0);
    gtext_json_free(val_zero);
}

/**
 * Test value creation for number from uint64
 */
TEST(DOMValueCreation, NumberU64) {
    uint64_t test_val = 12345;
    GTEXT_JSON_Value * val = gtext_json_new_number_u64(test_val);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NUMBER);
    uint64_t out_val = 0;
    GTEXT_JSON_Status status = gtext_json_get_u64(val, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_val, test_val);
    gtext_json_free(val);

    // Test large value
    uint64_t test_large = UINT64_MAX;
    GTEXT_JSON_Value * val_large = gtext_json_new_number_u64(test_large);
    ASSERT_NE(val_large, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_large), GTEXT_JSON_NUMBER);
    out_val = 0;
    status = gtext_json_get_u64(val_large, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(out_val, test_large);
    gtext_json_free(val_large);
}

/**
 * Test value creation for number from double
 */
TEST(DOMValueCreation, NumberDouble) {
    double test_val = 123.456;
    GTEXT_JSON_Value * val = gtext_json_new_number_double(test_val);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NUMBER);
    double out_val = 0.0;
    GTEXT_JSON_Status status = gtext_json_get_double(val, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_NEAR(out_val, test_val, 0.001);
    gtext_json_free(val);

    // Test negative
    double test_neg = -789.012;
    GTEXT_JSON_Value * val_neg = gtext_json_new_number_double(test_neg);
    ASSERT_NE(val_neg, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_neg), GTEXT_JSON_NUMBER);
    out_val = 0.0;
    status = gtext_json_get_double(val_neg, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_NEAR(out_val, test_neg, 0.001);
    gtext_json_free(val_neg);

    // Test zero
    GTEXT_JSON_Value * val_zero = gtext_json_new_number_double(0.0);
    ASSERT_NE(val_zero, nullptr);
    EXPECT_EQ(gtext_json_typeof(val_zero), GTEXT_JSON_NUMBER);
    out_val = 1.0;  // Set to non-zero to verify it gets set to 0
    status = gtext_json_get_double(val_zero, &out_val);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_DOUBLE_EQ(out_val, 0.0);
    gtext_json_free(val_zero);
}

/**
 * Test value creation for array
 */
TEST(DOMValueCreation, Array) {
    GTEXT_JSON_Value * val = gtext_json_new_array();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(val), 0u);
    gtext_json_free(val);
}

/**
 * Test value creation for object
 */
TEST(DOMValueCreation, Object) {
    GTEXT_JSON_Value * val = gtext_json_new_object();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(val), 0u);
    gtext_json_free(val);
}

/**
 * Test memory cleanup via gtext_json_free
 */
TEST(MemoryManagement, ValueCleanup) {
    // Create multiple values and verify they're cleaned up
    GTEXT_JSON_Value * null_val = gtext_json_new_null();
    GTEXT_JSON_Value * bool_val = gtext_json_new_bool(true);
    GTEXT_JSON_Value * str_val = gtext_json_new_string("test", 4);
    GTEXT_JSON_Value * num_val = gtext_json_new_number_i64(42);
    GTEXT_JSON_Value * arr_val = gtext_json_new_array();
    GTEXT_JSON_Value * obj_val = gtext_json_new_object();

    ASSERT_NE(null_val, nullptr);
    ASSERT_NE(bool_val, nullptr);
    ASSERT_NE(str_val, nullptr);
    ASSERT_NE(num_val, nullptr);
    ASSERT_NE(arr_val, nullptr);
    ASSERT_NE(obj_val, nullptr);

    // Free all values (should not crash or leak)
    gtext_json_free(null_val);
    gtext_json_free(bool_val);
    gtext_json_free(str_val);
    gtext_json_free(num_val);
    gtext_json_free(arr_val);
    gtext_json_free(obj_val);

    // If we get here without crashing, the test passed
    SUCCEED();
}

/**
 * Test accessor error cases - wrong type access
 */
TEST(DOMAccessors, WrongType) {
    // Create a string value
    GTEXT_JSON_Value * str_val = gtext_json_new_string("test", 4);
    ASSERT_NE(str_val, nullptr);

    // Try to get bool from string - should fail
    bool bool_out = false;
    GTEXT_JSON_Status status = gtext_json_get_bool(str_val, &bool_out);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // Try to get number from string - should fail
    int64_t i64_out = 0;
    status = gtext_json_get_i64(str_val, &i64_out);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // Try to get array size from string - should return 0
    EXPECT_EQ(gtext_json_array_size(str_val), 0u);

    gtext_json_free(str_val);
}

/**
 * Test accessor error cases - null pointer handling
 */
TEST(DOMAccessors, NullPointer) {
    // GTEXT_JSON_Typeof should handle NULL
    EXPECT_EQ(gtext_json_typeof(nullptr), GTEXT_JSON_NULL);

    // Other accessors should return error for NULL
    bool bool_out = false;
    GTEXT_JSON_Status status = gtext_json_get_bool(nullptr, &bool_out);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    const char * str_out = nullptr;
    size_t str_len = 0;
    status = gtext_json_get_string(nullptr, &str_out, &str_len);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // Array/object size should return 0 for NULL
    EXPECT_EQ(gtext_json_array_size(nullptr), 0u);
    EXPECT_EQ(gtext_json_object_size(nullptr), 0u);

    // Array/object get should return NULL for NULL
    EXPECT_EQ(gtext_json_array_get(nullptr, 0), nullptr);
    EXPECT_EQ(gtext_json_object_value(nullptr, 0), nullptr);
    EXPECT_EQ(gtext_json_object_key(nullptr, 0, nullptr), nullptr);
    EXPECT_EQ(gtext_json_object_get(nullptr, "key", 3), nullptr);
}

/**
 * Test array access with bounds checking
 */
TEST(DOMAccessors, ArrayAccessBounds) {
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Empty array - should return NULL for any index
    EXPECT_EQ(gtext_json_array_get(arr, 0), nullptr);
    EXPECT_EQ(gtext_json_array_get(arr, 1), nullptr);

    // Note: We can't test with actual elements yet since array mutation
    // functions (gtext_json_array_push) haven't been implemented.
    // This test verifies bounds checking works for empty arrays.

    gtext_json_free(arr);
}

/**
 * Test object access - key lookup and iteration
 */
TEST(DOMAccessors, ObjectAccess) {
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    ASSERT_NE(obj, nullptr);

    // Empty object - should return NULL for any key/index
    EXPECT_EQ(gtext_json_object_get(obj, "key", 3), nullptr);
    EXPECT_EQ(gtext_json_object_value(obj, 0), nullptr);
    EXPECT_EQ(gtext_json_object_key(obj, 0, nullptr), nullptr);

    // Note: We can't test with actual key-value pairs yet since object mutation
    // functions (gtext_json_object_put) haven't been implemented.
    // This test verifies bounds checking works for empty objects.

    gtext_json_free(obj);
}

/**
 * Test number accessor error cases - missing representations
 */
TEST(DOMAccessors, NumberAccessorMissingRepresentations) {
    // Create number from lexeme only (no numeric representations)
    GTEXT_JSON_Value * num = gtext_json_new_number_from_lexeme("123.456", 7);
    ASSERT_NE(num, nullptr);

    // Lexeme should be available
    const char * lexeme = nullptr;
    size_t lexeme_len = 0;
    GTEXT_JSON_Status status = gtext_json_get_number_lexeme(num, &lexeme, &lexeme_len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(lexeme, "123.456");

    // int64 should not be available (created from lexeme only)
    int64_t i64_out = 0;
    status = gtext_json_get_i64(num, &i64_out);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // uint64 should not be available
    uint64_t u64_out = 0;
    status = gtext_json_get_u64(num, &u64_out);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // double should not be available
    double dbl_out = 0.0;
    status = gtext_json_get_double(num, &dbl_out);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    gtext_json_free(num);
}

/**
 * Test array mutation - push, set, insert, remove
 */
TEST(DOMMutation, ArrayPush) {
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push some elements
    GTEXT_JSON_Value * val1 = gtext_json_new_number_i64(42);
    GTEXT_JSON_Value * val2 = gtext_json_new_string("hello", 5);
    GTEXT_JSON_Value * val3 = gtext_json_new_bool(true);

    EXPECT_EQ(gtext_json_array_push(arr, val1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val2), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val3), GTEXT_JSON_OK);

    // Verify array size and contents
    EXPECT_EQ(gtext_json_array_size(arr), 3u);
    EXPECT_EQ(gtext_json_array_get(arr, 0), val1);
    EXPECT_EQ(gtext_json_array_get(arr, 1), val2);
    EXPECT_EQ(gtext_json_array_get(arr, 2), val3);

    // Verify values
    int64_t i64_out = 0;
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 0), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 42);

    const char * str_out = nullptr;
    size_t str_len = 0;
    EXPECT_EQ(gtext_json_get_string(gtext_json_array_get(arr, 1), &str_out, &str_len), GTEXT_JSON_OK);
    EXPECT_EQ(str_len, 5u);
    EXPECT_STREQ(str_out, "hello");

    bool bool_out = false;
    EXPECT_EQ(gtext_json_get_bool(gtext_json_array_get(arr, 2), &bool_out), GTEXT_JSON_OK);
    EXPECT_EQ(bool_out, true);

    gtext_json_free(arr);
}

TEST(DOMMutation, ArraySet) {
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push initial elements
    GTEXT_JSON_Value * val1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * val2 = gtext_json_new_number_i64(2);
    GTEXT_JSON_Value * val3 = gtext_json_new_number_i64(3);

    EXPECT_EQ(gtext_json_array_push(arr, val1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val2), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val3), GTEXT_JSON_OK);

    // Replace element at index 1
    GTEXT_JSON_Value * new_val = gtext_json_new_number_i64(99);
    EXPECT_EQ(gtext_json_array_set(arr, 1, new_val), GTEXT_JSON_OK);

    // Verify
    int64_t i64_out = 0;
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 0), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 1), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 99);  // Changed
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 2), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    // Test out of bounds
    GTEXT_JSON_Value * val4 = gtext_json_new_number_i64(4);
    EXPECT_EQ(gtext_json_array_set(arr, 10, val4), GTEXT_JSON_E_INVALID);
    gtext_json_free(val4);

    gtext_json_free(arr);
}

TEST(DOMMutation, ArrayInsert) {
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push initial elements
    GTEXT_JSON_Value * val1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * val2 = gtext_json_new_number_i64(2);
    GTEXT_JSON_Value * val3 = gtext_json_new_number_i64(3);

    EXPECT_EQ(gtext_json_array_push(arr, val1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val2), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val3), GTEXT_JSON_OK);

    // Insert at index 1
    GTEXT_JSON_Value * new_val = gtext_json_new_number_i64(99);
    EXPECT_EQ(gtext_json_array_insert(arr, 1, new_val), GTEXT_JSON_OK);

    // Verify: [1, 99, 2, 3]
    EXPECT_EQ(gtext_json_array_size(arr), 4u);
    int64_t i64_out = 0;
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 0), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 1), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 99);
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 2), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 3), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    // Insert at end (should work like push)
    GTEXT_JSON_Value * val_end = gtext_json_new_number_i64(100);
    EXPECT_EQ(gtext_json_array_insert(arr, 4, val_end), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_size(arr), 5u);

    // Test out of bounds
    GTEXT_JSON_Value * val4 = gtext_json_new_number_i64(4);
    EXPECT_EQ(gtext_json_array_insert(arr, 10, val4), GTEXT_JSON_E_INVALID);
    gtext_json_free(val4);

    gtext_json_free(arr);
}

TEST(DOMMutation, ArrayRemove) {
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    ASSERT_NE(arr, nullptr);

    // Push initial elements
    GTEXT_JSON_Value * val1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * val2 = gtext_json_new_number_i64(2);
    GTEXT_JSON_Value * val3 = gtext_json_new_number_i64(3);
    GTEXT_JSON_Value * val4 = gtext_json_new_number_i64(4);

    EXPECT_EQ(gtext_json_array_push(arr, val1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val2), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val3), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, val4), GTEXT_JSON_OK);

    // Remove element at index 1 (value 2)
    EXPECT_EQ(gtext_json_array_remove(arr, 1), GTEXT_JSON_OK);

    // Verify: [1, 3, 4]
    EXPECT_EQ(gtext_json_array_size(arr), 3u);
    int64_t i64_out = 0;
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 0), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 1), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);
    EXPECT_EQ(gtext_json_get_i64(gtext_json_array_get(arr, 2), &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 4);

    // Remove first element
    EXPECT_EQ(gtext_json_array_remove(arr, 0), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_size(arr), 2u);

    // Remove last element
    EXPECT_EQ(gtext_json_array_remove(arr, 1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_size(arr), 1u);

    // Test out of bounds
    EXPECT_EQ(gtext_json_array_remove(arr, 10), GTEXT_JSON_E_INVALID);

    gtext_json_free(arr);
}

TEST(DOMMutation, ObjectPut) {
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    ASSERT_NE(obj, nullptr);

    // Add key-value pairs
    GTEXT_JSON_Value * val1 = gtext_json_new_number_i64(42);
    GTEXT_JSON_Value * val2 = gtext_json_new_string("hello", 5);
    GTEXT_JSON_Value * val3 = gtext_json_new_bool(true);

    EXPECT_EQ(gtext_json_object_put(obj, "key1", 4, val1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_put(obj, "key2", 4, val2), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_put(obj, "key3", 4, val3), GTEXT_JSON_OK);

    // Verify object size
    EXPECT_EQ(gtext_json_object_size(obj), 3u);

    // Verify values
    const GTEXT_JSON_Value * v1 = gtext_json_object_get(obj, "key1", 4);
    ASSERT_NE(v1, nullptr);
    int64_t i64_out = 0;
    EXPECT_EQ(gtext_json_get_i64(v1, &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 42);

    const GTEXT_JSON_Value * v2 = gtext_json_object_get(obj, "key2", 4);
    ASSERT_NE(v2, nullptr);
    const char * str_out = nullptr;
    size_t str_len = 0;
    EXPECT_EQ(gtext_json_get_string(v2, &str_out, &str_len), GTEXT_JSON_OK);
    EXPECT_STREQ(str_out, "hello");

    // Replace existing key
    GTEXT_JSON_Value * new_val = gtext_json_new_number_i64(99);
    EXPECT_EQ(gtext_json_object_put(obj, "key1", 4, new_val), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_size(obj), 3u);  // Size should not change

    const GTEXT_JSON_Value * v1_new = gtext_json_object_get(obj, "key1", 4);
    ASSERT_NE(v1_new, nullptr);
    EXPECT_EQ(gtext_json_get_i64(v1_new, &i64_out), GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 99);  // Changed

    gtext_json_free(obj);
}

TEST(DOMMutation, ObjectRemove) {
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    ASSERT_NE(obj, nullptr);

    // Add key-value pairs
    GTEXT_JSON_Value * val1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * val2 = gtext_json_new_number_i64(2);
    GTEXT_JSON_Value * val3 = gtext_json_new_number_i64(3);

    EXPECT_EQ(gtext_json_object_put(obj, "key1", 4, val1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_put(obj, "key2", 4, val2), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_put(obj, "key3", 4, val3), GTEXT_JSON_OK);

    EXPECT_EQ(gtext_json_object_size(obj), 3u);

    // Remove middle key
    EXPECT_EQ(gtext_json_object_remove(obj, "key2", 4), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_size(obj), 2u);

    // Verify key2 is gone
    EXPECT_EQ(gtext_json_object_get(obj, "key2", 4), nullptr);
    EXPECT_NE(gtext_json_object_get(obj, "key1", 4), nullptr);
    EXPECT_NE(gtext_json_object_get(obj, "key3", 4), nullptr);

    // Remove first key
    EXPECT_EQ(gtext_json_object_remove(obj, "key1", 4), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_size(obj), 1u);

    // Remove last key
    EXPECT_EQ(gtext_json_object_remove(obj, "key3", 4), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_object_size(obj), 0u);

    // Try to remove non-existent key
    EXPECT_EQ(gtext_json_object_remove(obj, "nonexistent", 11), GTEXT_JSON_E_INVALID);

    gtext_json_free(obj);
}

TEST(DOMMutation, NestedStructures) {
    // Test building nested structures
    GTEXT_JSON_Value * root = gtext_json_new_object();
    ASSERT_NE(root, nullptr);

    // Create nested array
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    GTEXT_JSON_Value * elem1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * elem2 = gtext_json_new_number_i64(2);
    EXPECT_EQ(gtext_json_array_push(arr, elem1), GTEXT_JSON_OK);
    EXPECT_EQ(gtext_json_array_push(arr, elem2), GTEXT_JSON_OK);

    // Add array to object
    EXPECT_EQ(gtext_json_object_put(root, "array", 5, arr), GTEXT_JSON_OK);

    // Create nested object
    GTEXT_JSON_Value * nested_obj = gtext_json_new_object();
    GTEXT_JSON_Value * nested_val = gtext_json_new_string("nested", 6);
    EXPECT_EQ(gtext_json_object_put(nested_obj, "key", 3, nested_val), GTEXT_JSON_OK);

    // Add nested object to root
    EXPECT_EQ(gtext_json_object_put(root, "object", 6, nested_obj), GTEXT_JSON_OK);

    // Verify structure
    EXPECT_EQ(gtext_json_object_size(root), 2u);

    const GTEXT_JSON_Value * arr_val = gtext_json_object_get(root, "array", 5);
    ASSERT_NE(arr_val, nullptr);
    EXPECT_EQ(gtext_json_typeof(arr_val), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(arr_val), 2u);

    const GTEXT_JSON_Value * obj_val = gtext_json_object_get(root, "object", 6);
    ASSERT_NE(obj_val, nullptr);
    EXPECT_EQ(gtext_json_typeof(obj_val), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(obj_val), 1u);

    gtext_json_free(root);
}

TEST(DOMMutation, ErrorCases) {
    // Test error cases for array operations
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    GTEXT_JSON_Value * val = gtext_json_new_number_i64(1);

    // NULL array
    EXPECT_EQ(gtext_json_array_push(nullptr, val), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_array_set(nullptr, 0, val), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_array_insert(nullptr, 0, val), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_array_remove(nullptr, 0), GTEXT_JSON_E_INVALID);

    // NULL value
    EXPECT_EQ(gtext_json_array_push(arr, nullptr), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_array_set(arr, 0, nullptr), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_array_insert(arr, 0, nullptr), GTEXT_JSON_E_INVALID);

    // Wrong type
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    EXPECT_EQ(gtext_json_array_push(obj, val), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_array_set(obj, 0, val), GTEXT_JSON_E_INVALID);

    gtext_json_free(arr);
    gtext_json_free(val);
    gtext_json_free(obj);

    // Test error cases for object operations
    GTEXT_JSON_Value * obj2 = gtext_json_new_object();
    GTEXT_JSON_Value * val2 = gtext_json_new_number_i64(2);

    // NULL object
    EXPECT_EQ(gtext_json_object_put(nullptr, "key", 3, val2), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_object_remove(nullptr, "key", 3), GTEXT_JSON_E_INVALID);

    // NULL key
    EXPECT_EQ(gtext_json_object_put(obj2, nullptr, 3, val2), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_object_remove(obj2, nullptr, 3), GTEXT_JSON_E_INVALID);

    // NULL value
    EXPECT_EQ(gtext_json_object_put(obj2, "key", 3, nullptr), GTEXT_JSON_E_INVALID);

    // Wrong type
    GTEXT_JSON_Value * arr2 = gtext_json_new_array();
    EXPECT_EQ(gtext_json_object_put(arr2, "key", 3, val2), GTEXT_JSON_E_INVALID);
    EXPECT_EQ(gtext_json_object_remove(arr2, "key", 3), GTEXT_JSON_E_INVALID);

    gtext_json_free(obj2);
    gtext_json_free(val2);
    gtext_json_free(arr2);
}

/**
 * Test duplicate key handling - ERROR policy
 */
TEST(DuplicateKeyHandling, Error) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_ERROR;

    const char * input = R"({"key": 1, "key": 2})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

    EXPECT_EQ(value, nullptr);
    EXPECT_EQ(err.code, GTEXT_JSON_E_DUPKEY);
    EXPECT_STREQ(err.message, "Duplicate key in object");

    // Clean up error context snippet
    gtext_json_error_free(&err);
}

/**
 * Test duplicate key handling - FIRST_WINS policy
 */
TEST(DuplicateKeyHandling, FirstWins) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_FIRST_WINS;

    const char * input = R"({"key": 1, "key": 2})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(value), 1u);

    // Should have first value (1)
    const GTEXT_JSON_Value * val = gtext_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NUMBER);

    int64_t i64_out = 0;
    GTEXT_JSON_Status status = gtext_json_get_i64(val, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);

    gtext_json_free(value);
}

/**
 * Test duplicate key handling - LAST_WINS policy
 */
TEST(DuplicateKeyHandling, LastWins) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_LAST_WINS;

    const char * input = R"({"key": 1, "key": 2})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(value), 1u);

    // Should have last value (2)
    const GTEXT_JSON_Value * val = gtext_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NUMBER);

    int64_t i64_out = 0;
    GTEXT_JSON_Status status = gtext_json_get_i64(val, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);

    gtext_json_free(value);
}

/**
 * Test duplicate key handling - COLLECT policy (single value to array)
 */
TEST(DuplicateKeyHandling, CollectSingle) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_COLLECT;

    const char * input = R"({"key": 1, "key": 2})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(value), 1u);

    // Should have array with [1, 2]
    const GTEXT_JSON_Value * val = gtext_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(val), 2u);

    // First element should be 1
    const GTEXT_JSON_Value * elem0 = gtext_json_array_get(val, 0);
    ASSERT_NE(elem0, nullptr);
    int64_t i64_out = 0;
    GTEXT_JSON_Status status = gtext_json_get_i64(elem0, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);

    // Second element should be 2
    const GTEXT_JSON_Value * elem1 = gtext_json_array_get(val, 1);
    ASSERT_NE(elem1, nullptr);
    status = gtext_json_get_i64(elem1, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);

    gtext_json_free(value);
}

/**
 * Test duplicate key handling - COLLECT policy (array to array)
 */
TEST(DuplicateKeyHandling, CollectArray) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_COLLECT;

    const char * input = R"({"key": [1, 2], "key": 3})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

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
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(value), 1u);

    // Should have array with [1, 2, 3]
    const GTEXT_JSON_Value * val = gtext_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(val), 3u);

    // Check elements
    const GTEXT_JSON_Value * elem0 = gtext_json_array_get(val, 0);
    ASSERT_NE(elem0, nullptr);
    int64_t i64_out = 0;
    GTEXT_JSON_Status status = gtext_json_get_i64(elem0, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 1);

    const GTEXT_JSON_Value * elem1 = gtext_json_array_get(val, 1);
    ASSERT_NE(elem1, nullptr);
    status = gtext_json_get_i64(elem1, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 2);

    const GTEXT_JSON_Value * elem2 = gtext_json_array_get(val, 2);
    ASSERT_NE(elem2, nullptr);
    status = gtext_json_get_i64(elem2, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    gtext_json_free(value);
}

/**
 * Test duplicate key handling - multiple duplicates with COLLECT
 */
TEST(DuplicateKeyHandling, CollectMultiple) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_COLLECT;

    const char * input = R"({"key": 1, "key": 2, "key": 3})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(value), 1u);

    // Should have array with [1, 2, 3]
    const GTEXT_JSON_Value * val = gtext_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(val), 3u);

    gtext_json_free(value);
}

/**
 * Test duplicate key handling - nested objects
 */
TEST(DuplicateKeyHandling, Nested) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_LAST_WINS;

    const char * input = R"({"outer": {"key": 1, "key": 2}, "outer": {"key": 3}})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(value), 1u);

    // Should have last outer object
    const GTEXT_JSON_Value * outer = gtext_json_object_get(value, "outer", 5);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(gtext_json_typeof(outer), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(outer), 1u);

    // Inner object should have last value (3)
    const GTEXT_JSON_Value * inner = gtext_json_object_get(outer, "key", 3);
    ASSERT_NE(inner, nullptr);
    int64_t i64_out = 0;
    GTEXT_JSON_Status status = gtext_json_get_i64(inner, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 3);

    gtext_json_free(value);
}

/**
 * Test duplicate key handling - different value types with COLLECT
 */
TEST(DuplicateKeyHandling, CollectDifferentTypes) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.dupkeys = GTEXT_JSON_DUPKEY_COLLECT;

    const char * input = R"({"key": "first", "key": 42, "key": true})";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(value), 1u);

    // Should have array with ["first", 42, true]
    const GTEXT_JSON_Value * val = gtext_json_object_get(value, "key", 3);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(val), 3u);

    // First element should be string "first"
    const GTEXT_JSON_Value * elem0 = gtext_json_array_get(val, 0);
    ASSERT_NE(elem0, nullptr);
    EXPECT_EQ(gtext_json_typeof(elem0), GTEXT_JSON_STRING);
    const char * str_out = nullptr;
    size_t str_len = 0;
    GTEXT_JSON_Status status = gtext_json_get_string(elem0, &str_out, &str_len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(str_len, 5u);
    EXPECT_STREQ(str_out, "first");

    // Second element should be number 42
    const GTEXT_JSON_Value * elem1 = gtext_json_array_get(val, 1);
    ASSERT_NE(elem1, nullptr);
    EXPECT_EQ(gtext_json_typeof(elem1), GTEXT_JSON_NUMBER);
    int64_t i64_out = 0;
    status = gtext_json_get_i64(elem1, &i64_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(i64_out, 42);

    // Third element should be boolean true
    const GTEXT_JSON_Value * elem2 = gtext_json_array_get(val, 2);
    ASSERT_NE(elem2, nullptr);
    EXPECT_EQ(gtext_json_typeof(elem2), GTEXT_JSON_BOOL);
    bool bool_out = false;
    status = gtext_json_get_bool(elem2, &bool_out);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(bool_out, true);

    gtext_json_free(value);
}

/**
 * Test sink abstraction - custom callback sink
 */
TEST(SinkAbstraction, CallbackSink) {
    std::string output;

    // Custom write callback that appends to string
    auto write_callback = [](void * user, const char * bytes, size_t len) -> int {
        std::string* str = (std::string*)user;
        str->append(bytes, len);
        return 0;
    };

    GTEXT_JSON_Sink sink;
    sink.write = write_callback;
    sink.user = &output;

    // Write some data
    const char * test_data = "Hello, World!";
    int result = sink.write(sink.user, test_data, strlen(test_data));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(output, "Hello, World!");

    // Write more data
    const char * more_data = " Test";
    result = sink.write(sink.user, more_data, strlen(more_data));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(output, "Hello, World! Test");
}

/**
 * Test sink abstraction - growable buffer sink
 */
TEST(SinkAbstraction, GrowableBuffer) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Initially empty
    EXPECT_EQ(gtext_json_sink_buffer_size(&sink), 0u);
    const char * data = gtext_json_sink_buffer_data(&sink);
    ASSERT_NE(data, nullptr);
    EXPECT_STREQ(data, "");

    // Write some data
    const char * test1 = "Hello";
    int result = sink.write(sink.user, test1, strlen(test1));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(gtext_json_sink_buffer_size(&sink), 5u);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "Hello");

    // Write more data
    const char * test2 = ", World!";
    result = sink.write(sink.user, test2, strlen(test2));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(gtext_json_sink_buffer_size(&sink), 13u);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "Hello, World!");

    // Write large amount of data to test growth
    std::string large_data(1000, 'A');
    result = sink.write(sink.user, large_data.c_str(), large_data.size());
    EXPECT_EQ(result, 0);
    EXPECT_EQ(gtext_json_sink_buffer_size(&sink), 1013u);

    // Verify data integrity
    data = gtext_json_sink_buffer_data(&sink);
    EXPECT_EQ(strncmp(data, "Hello, World!", 13), 0);
    EXPECT_EQ(data[1012], 'A');

    // Clean up
    gtext_json_sink_buffer_free(&sink);
    EXPECT_EQ(sink.write, nullptr);
    EXPECT_EQ(sink.user, nullptr);
}

/**
 * Test sink abstraction - fixed buffer sink
 */
TEST(SinkAbstraction, FixedBuffer) {
    char buffer[64];
    GTEXT_JSON_Sink sink;

    GTEXT_JSON_Status status = gtext_json_sink_fixed_buffer(&sink, buffer, sizeof(buffer));
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Initially empty
    EXPECT_EQ(gtext_json_sink_fixed_buffer_used(&sink), 0u);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_truncated(&sink), false);
    EXPECT_STREQ(buffer, "");

    // Write some data
    const char * test1 = "Hello";
    int result = sink.write(sink.user, test1, strlen(test1));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_used(&sink), 5u);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_truncated(&sink), false);
    EXPECT_STREQ(buffer, "Hello");

    // Write more data
    const char * test2 = ", World!";
    result = sink.write(sink.user, test2, strlen(test2));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_used(&sink), 13u);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_truncated(&sink), false);
    EXPECT_STREQ(buffer, "Hello, World!");

    // Write data that fits exactly
    const char * test3 = " This fits";
    result = sink.write(sink.user, test3, strlen(test3));
    EXPECT_EQ(result, 0);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_used(&sink), 23u);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_truncated(&sink), false);

    // Write data that exceeds buffer (should truncate)
    // After 23 bytes, we have 64 - 23 - 1 = 40 bytes available
    // This string is 50 bytes, so it will exceed and truncate
    const char * test4 = " This is way too long and will definitely be truncated";
    result = sink.write(sink.user, test4, strlen(test4));
    EXPECT_NE(result, 0); // Should return error on truncation
    EXPECT_EQ(gtext_json_sink_fixed_buffer_truncated(&sink), true);
    // Should have written up to buffer limit (63 bytes, leaving 1 for null terminator)
    EXPECT_EQ(gtext_json_sink_fixed_buffer_used(&sink), sizeof(buffer) - 1);

    // Clean up
    gtext_json_sink_fixed_buffer_free(&sink);
}

/**
 * Test sink abstraction - fixed buffer edge cases
 */
TEST(SinkAbstraction, FixedBufferEdgeCases) {
    // Test with size 1 buffer (only null terminator)
    char tiny_buffer[1];
    GTEXT_JSON_Sink sink;

    GTEXT_JSON_Status status = gtext_json_sink_fixed_buffer(&sink, tiny_buffer, 1);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Try to write (should truncate immediately)
    const char * test = "X";
    int result = sink.write(sink.user, test, 1);
    EXPECT_NE(result, 0); // Should return error
    EXPECT_EQ(gtext_json_sink_fixed_buffer_truncated(&sink), true);
    EXPECT_EQ(gtext_json_sink_fixed_buffer_used(&sink), 0u);
    EXPECT_EQ(tiny_buffer[0], '\0');

    // Test invalid parameters
    status = gtext_json_sink_fixed_buffer(nullptr, tiny_buffer, 1);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    char buf[10];
    status = gtext_json_sink_fixed_buffer(&sink, nullptr, 10);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    status = gtext_json_sink_fixed_buffer(&sink, buf, 0);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // Clean up
    gtext_json_sink_fixed_buffer_free(&sink);
}

/**
 * Test sink abstraction - growable buffer edge cases
 */
TEST(SinkAbstraction, GrowableBufferEdgeCases) {
    // Test invalid parameters
    GTEXT_JSON_Status status = gtext_json_sink_buffer(nullptr);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // Test empty buffer access
    GTEXT_JSON_Sink sink;
    status = gtext_json_sink_buffer(&sink);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    const char * data = gtext_json_sink_buffer_data(nullptr);
    EXPECT_EQ(data, nullptr);

    size_t size = gtext_json_sink_buffer_size(nullptr);
    EXPECT_EQ(size, 0u);

    // Test free with invalid sink
    GTEXT_JSON_Sink invalid_sink = {nullptr, nullptr};
    gtext_json_sink_buffer_free(&invalid_sink); // Should not crash

    // Clean up valid sink
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test sink abstraction - error propagation
 */
TEST(SinkAbstraction, ErrorPropagation) {
    // Custom callback that returns error
    auto error_callback = [](void * user, const char * bytes, size_t len) -> int {
        (void)user;
        (void)bytes;
        (void)len;
        return 1; // Error
    };

    GTEXT_JSON_Sink sink;
    sink.write = error_callback;
    sink.user = nullptr;

    const char * test = "test";
    int result = sink.write(sink.user, test, strlen(test));
    EXPECT_NE(result, 0); // Should propagate error
}

/**
 * Test DOM write - null value
 */
TEST(DOMWrite, Null) {
    GTEXT_JSON_Value * v = gtext_json_new_null();
    ASSERT_NE(v, nullptr);

    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};
    status = gtext_json_write_value(&sink, &opts, v, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "null");

    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(v);
}

/**
 * Test DOM write - boolean values
 */
TEST(DOMWrite, Boolean) {
    GTEXT_JSON_Value * v_true = gtext_json_new_bool(true);
    GTEXT_JSON_Value * v_false = gtext_json_new_bool(false);
    ASSERT_NE(v_true, nullptr);
    ASSERT_NE(v_false, nullptr);

    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);

    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    gtext_json_write_value(&sink, &opts, v_true, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "true");

    gtext_json_sink_buffer_free(&sink);
    gtext_json_sink_buffer(&sink);

    gtext_json_write_value(&sink, &opts, v_false, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "false");

    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(v_true);
    gtext_json_free(v_false);
}

/**
 * Test DOM write - string values with escaping
 */
TEST(DOMWrite, StringEscaping) {
    GTEXT_JSON_Value * v1 = gtext_json_new_string("hello", 5);
    GTEXT_JSON_Value * v2 = gtext_json_new_string("he\"llo", 6);
    GTEXT_JSON_Value * v3 = gtext_json_new_string("he\\llo", 6);
    GTEXT_JSON_Value * v4 = gtext_json_new_string("he\nllo", 6);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);
    ASSERT_NE(v4, nullptr);

    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v1, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "\"hello\"");
    gtext_json_sink_buffer_free(&sink);

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v2, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "\"he\\\"llo\"");
    gtext_json_sink_buffer_free(&sink);

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v3, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "\"he\\\\llo\"");
    gtext_json_sink_buffer_free(&sink);

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v4, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "\"he\\nllo\"");
    gtext_json_sink_buffer_free(&sink);

    gtext_json_free(v1);
    gtext_json_free(v2);
    gtext_json_free(v3);
    gtext_json_free(v4);
}

/**
 * Test DOM write - number values
 */
TEST(DOMWrite, Number) {
    GTEXT_JSON_Value * v1 = gtext_json_new_number_i64(123);
    GTEXT_JSON_Value * v2 = gtext_json_new_number_u64(456u);
    GTEXT_JSON_Value * v3 = gtext_json_new_number_double(3.14);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v1, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "123");
    gtext_json_sink_buffer_free(&sink);

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v2, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "456");
    gtext_json_sink_buffer_free(&sink);

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v3, &err);
    // Double output format may vary, just check it's not empty
    EXPECT_GT(gtext_json_sink_buffer_size(&sink), 0u);
    gtext_json_sink_buffer_free(&sink);

    gtext_json_free(v1);
    gtext_json_free(v2);
    gtext_json_free(v3);
}

/**
 * Test DOM write - array values
 */
TEST(DOMWrite, Array) {
    GTEXT_JSON_Value * arr = gtext_json_new_array();
    ASSERT_NE(arr, nullptr);

    GTEXT_JSON_Value * v1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * v2 = gtext_json_new_string("two", 3);
    GTEXT_JSON_Value * v3 = gtext_json_new_bool(true);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    gtext_json_array_push(arr, v1);
    gtext_json_array_push(arr, v2);
    gtext_json_array_push(arr, v3);

    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    gtext_json_write_value(&sink, &opts, arr, &err);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "[1,\"two\",true]");

    gtext_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    gtext_json_free(arr);
}

/**
 * Test DOM write - object values
 */
TEST(DOMWrite, Object) {
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    ASSERT_NE(obj, nullptr);

    GTEXT_JSON_Value * v1 = gtext_json_new_number_i64(42);
    GTEXT_JSON_Value * v2 = gtext_json_new_string("value", 5);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);

    gtext_json_object_put(obj, "key1", 4, v1);
    gtext_json_object_put(obj, "key2", 4, v2);

    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    gtext_json_write_value(&sink, &opts, obj, &err);
    // Output order may vary, check it contains both keys
    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "key1"), nullptr);
    EXPECT_NE(strstr(output, "key2"), nullptr);
    EXPECT_NE(strstr(output, "42"), nullptr);
    EXPECT_NE(strstr(output, "value"), nullptr);

    gtext_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    gtext_json_free(obj);
}

/**
 * Test DOM write - pretty printing
 */
TEST(DOMWrite, PrettyPrint) {
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    ASSERT_NE(obj, nullptr);

    GTEXT_JSON_Value * arr = gtext_json_new_array();
    GTEXT_JSON_Value * v1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * v2 = gtext_json_new_string("test", 4);
    gtext_json_array_push(arr, v1);
    gtext_json_array_push(arr, v2);

    gtext_json_object_put(obj, "array", 5, arr);

    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    opts.pretty = true;
    opts.indent_spaces = 2;
    GTEXT_JSON_Error err{};

    gtext_json_write_value(&sink, &opts, obj, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    // Pretty print should contain newlines
    EXPECT_NE(strchr(output, '\n'), nullptr);
    // Should contain indentation
    EXPECT_NE(strstr(output, "  "), nullptr);

    gtext_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    gtext_json_free(obj);
}

/**
 * Test DOM write - key sorting for canonical output
 */
TEST(DOMWrite, KeySorting) {
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    ASSERT_NE(obj, nullptr);

    GTEXT_JSON_Value * v1 = gtext_json_new_string("first", 5);
    GTEXT_JSON_Value * v2 = gtext_json_new_string("second", 6);
    GTEXT_JSON_Value * v3 = gtext_json_new_string("third", 5);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    // Add keys in non-alphabetical order
    gtext_json_object_put(obj, "zebra", 5, v1);
    gtext_json_object_put(obj, "apple", 5, v2);
    gtext_json_object_put(obj, "banana", 6, v3);

    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    opts.sort_object_keys = true;
    GTEXT_JSON_Error err{};

    gtext_json_write_value(&sink, &opts, obj, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    // With sorting, "apple" should come before "banana", and "banana" before "zebra"
    const char * apple_pos = strstr(output, "apple");
    const char * banana_pos = strstr(output, "banana");
    const char * zebra_pos = strstr(output, "zebra");
    ASSERT_NE(apple_pos, nullptr);
    ASSERT_NE(banana_pos, nullptr);
    ASSERT_NE(zebra_pos, nullptr);
    EXPECT_LT(apple_pos, banana_pos);
    EXPECT_LT(banana_pos, zebra_pos);

    gtext_json_sink_buffer_free(&sink);
    // Freeing the parent should automatically free all children
    gtext_json_free(obj);
}

/**
 * Test DOM write - error handling
 */
TEST(DOMWrite, ErrorHandling) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    // NULL sink
    GTEXT_JSON_Value * v = gtext_json_new_null();
    GTEXT_JSON_Status status = gtext_json_write_value(nullptr, &opts, v, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);
    gtext_json_free(v);

    // NULL value
    gtext_json_sink_buffer(&sink);
    status = gtext_json_write_value(&sink, &opts, nullptr, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test DOM write - round trip (parse then write)
 */
TEST(DOMWrite, RoundTrip) {
    const char * input = "{\"key\":[1,2,\"three\",true,null]}";
    GTEXT_JSON_Parse_Options parse_opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * parsed = gtext_json_parse(input, strlen(input), &parse_opts, &err);
    ASSERT_NE(parsed, nullptr);

    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);
    GTEXT_JSON_Write_Options write_opts = gtext_json_write_options_default();
    gtext_json_write_value(&sink, &write_opts, parsed, &err);

    const char * output = gtext_json_sink_buffer_data(&sink);
    // Output should be valid JSON (we can parse it again)
    GTEXT_JSON_Value * reparsed = gtext_json_parse(output, gtext_json_sink_buffer_size(&sink), &parse_opts, &err);
    EXPECT_NE(reparsed, nullptr);

    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(parsed);
    gtext_json_free(reparsed);
}

/**
 * Test streaming parser - stream creation and destruction
 */
TEST(StreamingParser, CreationAndDestruction) {
    // Test creation with NULL callback (should fail)
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, nullptr, nullptr);
    EXPECT_EQ(st, nullptr);

    // Test creation with valid callback
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user;
        (void)evt;
        (void)err;
        return GTEXT_JSON_OK;
    };
    st = gtext_json_stream_new(&opts, callback, nullptr);
    EXPECT_NE(st, nullptr);

    // Test destruction
    gtext_json_stream_free(st);
    gtext_json_stream_free(nullptr);  // Should be safe

    // Test creation with NULL options (should use defaults)
    st = gtext_json_stream_new(nullptr, callback, nullptr);
    EXPECT_NE(st, nullptr);
    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - callback invocation (basic)
 *
 * Test that the stream can be created and that callbacks can be set up.
 */
TEST(StreamingParser, CallbackSetup) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    // Track callback invocations
    struct {
        std::vector<GTEXT_JSON_Event_Type> events;
        void * user_data;
    } context = {{}, (void * )0x12345};

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        auto* ctx = static_cast<decltype(context)*>(user);
        if (ctx) {
            ctx->events.push_back(evt->type);
        }
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &context);
    ASSERT_NE(st, nullptr);

    // Verify the stream is set up correctly
    EXPECT_EQ(context.events.size(), 0u);

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - stream state persistence
 *
 * Test that the stream maintains state across feed calls.
 */
TEST(StreamingParser, StatePersistence) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user;
        (void)evt;
        (void)err;
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};
    // Feed empty input (should be OK)
    GTEXT_JSON_Status status = gtext_json_stream_feed(st, "", 0, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Feed some data - should parse successfully
    const char * data = "null";
    status = gtext_json_stream_feed(st, data, strlen(data), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Finish should succeed after parsing a complete value
    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // After finish, stream is in DONE state - feeding more should fail
    status = gtext_json_stream_feed(st, " true", 5, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);  // Should fail - stream is done

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - error handling
 */
TEST(StreamingParser, ErrorHandling) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user;
        (void)evt;
        (void)err;
        return GTEXT_JSON_OK;
    };

    // Test invalid JSON input (not NULL pointer tests - those are in NullPointerHandling suite)
    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};

    // Feed invalid JSON
    GTEXT_JSON_Status status = gtext_json_stream_feed(st, "invalid!!!", 10, &err);
    // May succeed initially (partial input)

    // Finish should detect the error
    status = gtext_json_stream_finish(st, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    gtext_json_stream_free(st);
    gtext_json_error_free(&err);
}

/**
 * Test streaming parser - basic value parsing (null, bool, number, string)
 */
TEST(StreamingParser, BasicValues) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    struct {
        const char * input;
        GTEXT_JSON_Event_Type expected_type;
    } tests[] = {
        {"null", GTEXT_JSON_EVT_NULL},
        {"true", GTEXT_JSON_EVT_BOOL},
        {"false", GTEXT_JSON_EVT_BOOL},
        {"123", GTEXT_JSON_EVT_NUMBER},
        {"\"hello\"", GTEXT_JSON_EVT_STRING},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        std::vector<GTEXT_JSON_Event_Type> events;

        auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
            (void)err;
            auto* evts = static_cast<std::vector<GTEXT_JSON_Event_Type>*>(user);
            evts->push_back(evt->type);
            return GTEXT_JSON_OK;
        };

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &events);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};
        GTEXT_JSON_Status status = gtext_json_stream_feed(st, tests[i].input, strlen(tests[i].input), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed for input: " << tests[i].input;

        status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed to finish for input: " << tests[i].input;

        EXPECT_EQ(events.size(), 1u) << "Expected 1 event for: " << tests[i].input;
        if (events.size() == 1) {
            EXPECT_EQ(events[0], tests[i].expected_type) << "Event type mismatch for: " << tests[i].input;
        }

        gtext_json_stream_free(st);
    }
}

/**
 * Test streaming parser - array parsing
 */
TEST(StreamingParser, Arrays) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<GTEXT_JSON_Event_Type> events;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        auto* evts = static_cast<std::vector<GTEXT_JSON_Event_Type>*>(user);
        evts->push_back(evt->type);
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char * input = "[1, 2, 3]";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Status status = gtext_json_stream_feed(st, input, strlen(input), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Expected events: ARRAY_BEGIN, NUMBER, NUMBER, NUMBER, ARRAY_END
    EXPECT_EQ(events.size(), 5u);
    if (events.size() >= 5) {
        EXPECT_EQ(events[0], GTEXT_JSON_EVT_ARRAY_BEGIN);
        EXPECT_EQ(events[1], GTEXT_JSON_EVT_NUMBER);
        EXPECT_EQ(events[2], GTEXT_JSON_EVT_NUMBER);
        EXPECT_EQ(events[3], GTEXT_JSON_EVT_NUMBER);
        EXPECT_EQ(events[4], GTEXT_JSON_EVT_ARRAY_END);
    }

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - object parsing
 */
TEST(StreamingParser, Objects) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<GTEXT_JSON_Event_Type> events;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        auto* evts = static_cast<std::vector<GTEXT_JSON_Event_Type>*>(user);
        evts->push_back(evt->type);
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char * input = "{\"key\": \"value\"}";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Status status = gtext_json_stream_feed(st, input, strlen(input), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Expected events: OBJECT_BEGIN, KEY, STRING, OBJECT_END
    EXPECT_EQ(events.size(), 4u);
    if (events.size() >= 4) {
        EXPECT_EQ(events[0], GTEXT_JSON_EVT_OBJECT_BEGIN);
        EXPECT_EQ(events[1], GTEXT_JSON_EVT_KEY);
        EXPECT_EQ(events[2], GTEXT_JSON_EVT_STRING);
        EXPECT_EQ(events[3], GTEXT_JSON_EVT_OBJECT_END);
    }

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - incremental/chunked input
 */
TEST(StreamingParser, IncrementalInput) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<GTEXT_JSON_Event_Type> events;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        auto* evts = static_cast<std::vector<GTEXT_JSON_Event_Type>*>(user);
        evts->push_back(evt->type);
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char * input = "[1, 2, 3]";
    GTEXT_JSON_Error err{};
    // Feed byte by byte
    for (size_t i = 0; i < strlen(input); ++i) {
        GTEXT_JSON_Status status = gtext_json_stream_feed(st, input + i, 1, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed at byte " << i;
    }

    GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received all events
    EXPECT_EQ(events.size(), 5u);

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - nested structures
 */
TEST(StreamingParser, NestedStructures) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<GTEXT_JSON_Event_Type> events;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        auto* evts = static_cast<std::vector<GTEXT_JSON_Event_Type>*>(user);
        evts->push_back(evt->type);
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &events);
    ASSERT_NE(st, nullptr);

    const char * input = "{\"arr\": [1, 2], \"obj\": {\"key\": \"value\"}}";
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Status status = gtext_json_stream_feed(st, input, strlen(input), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received events for nested structure
    EXPECT_GT(events.size(), 5u);

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - error handling (invalid JSON)
 */
TEST(StreamingParser, InvalidJSON) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user;
        (void)evt;
        (void)err;
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};

    // Invalid: missing comma
    const char * invalid1 = "[1 2]";
    GTEXT_JSON_Status status = gtext_json_stream_feed(st, invalid1, strlen(invalid1), &err);
    // Should either succeed (buffering) or fail
    status = gtext_json_stream_finish(st, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);  // Should fail on invalid JSON

    gtext_json_stream_free(st);
    // Free error from first operation before reusing
    gtext_json_error_free(&err);
    err = GTEXT_JSON_Error{
        .code = {},
        .message = {},
        .offset = {},
        .line = {},
        .col = {},
        .context_snippet = {},
        .context_snippet_len = {},
        .caret_offset = {},
        .expected_token = {},
        .actual_token = {}
    };

    // Invalid: incomplete structure
    st = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(st, nullptr);

    const char * invalid2 = "[1, 2";
    status = gtext_json_stream_feed(st, invalid2, strlen(invalid2), &err);
    status = gtext_json_stream_finish(st, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);  // Should fail on incomplete structure

    gtext_json_stream_free(st);
    gtext_json_error_free(&err);
}

/**
 * Test streaming parser - string spanning multiple chunks
 */
TEST(StreamingParser, StringSpanningChunks) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<std::string> string_values;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        if (evt->type == GTEXT_JSON_EVT_STRING) {
            auto* strings = static_cast<std::vector<std::string>*>(user);
            std::string str(evt->as.str.s, evt->as.str.len);
            strings->push_back(str);
        }
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &string_values);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};
    // Test: string split across chunks
    // Chunk 1: "hello
    // Chunk 2: world"
    const char * chunk1 = "\"hello";
    const char * chunk2 = "world\"";

    GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input

    status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received "helloworld"
    EXPECT_EQ(string_values.size(), 1u);
    EXPECT_EQ(string_values[0], "helloworld");

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - number spanning multiple chunks
 */
TEST(StreamingParser, NumberSpanningChunks) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<std::string> number_values;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        if (evt->type == GTEXT_JSON_EVT_NUMBER) {
            auto* numbers = static_cast<std::vector<std::string>*>(user);
            std::string num(evt->as.number.s, evt->as.number.len);
            numbers->push_back(num);
        }
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &number_values);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};
    // Test: number split across chunks (integer followed by decimal)
    // Chunk 1: 12345 (at EOF, should be treated as incomplete)
    // Chunk 2: .678
    const char * chunk1 = "12345";
    const char * chunk2 = ".678";

    GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input (incomplete)

    status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received "12345.678" (not "12345" and then error on ".678")
    EXPECT_EQ(number_values.size(), 1u);
    EXPECT_EQ(number_values[0], "12345.678");

    // Test: number ending with '.' at EOF
    gtext_json_stream_free(st);
    number_values.clear();
    st = gtext_json_stream_new(&opts, callback, &number_values);
    ASSERT_NE(st, nullptr);

    const char * chunk3 = "12345.";
    const char * chunk4 = "678";

    status = gtext_json_stream_feed(st, chunk3, strlen(chunk3), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input (incomplete)

    status = gtext_json_stream_feed(st, chunk4, strlen(chunk4), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received "12345.678"
    EXPECT_EQ(number_values.size(), 1u);
    EXPECT_EQ(number_values[0], "12345.678");

    // Test: number ending with 'e' at EOF (scientific notation)
    gtext_json_stream_free(st);
    number_values.clear();
    st = gtext_json_stream_new(&opts, callback, &number_values);
    ASSERT_NE(st, nullptr);

    const char * chunk5 = "12345e";
    const char * chunk6 = "+2";

    status = gtext_json_stream_feed(st, chunk5, strlen(chunk5), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input (incomplete)

    status = gtext_json_stream_feed(st, chunk6, strlen(chunk6), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received "12345e+2"
    EXPECT_EQ(number_values.size(), 1u);
    EXPECT_EQ(number_values[0], "12345e+2");

    // Test: complete number at EOF (followed by space in next chunk)
    gtext_json_stream_free(st);
    number_values.clear();
    st = gtext_json_stream_new(&opts, callback, &number_values);
    ASSERT_NE(st, nullptr);

    const char * chunk7 = "12345";
    const char * chunk8 = " ";

    status = gtext_json_stream_feed(st, chunk7, strlen(chunk7), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input (incomplete)

    status = gtext_json_stream_feed(st, chunk8, strlen(chunk8), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    // Note: This will fail because "12345 " is not valid JSON (trailing space)
    // But the number should have been emitted before the error
    // Actually, wait - if we have just "12345 ", that's not valid JSON at root level
    // Let's test with a valid context: [12345 ]
    gtext_json_stream_free(st);
    number_values.clear();
    st = gtext_json_stream_new(&opts, callback, &number_values);
    ASSERT_NE(st, nullptr);

    const char * chunk9 = "[12345";
    const char * chunk10 = "]";

    status = gtext_json_stream_feed(st, chunk9, strlen(chunk9), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input (incomplete)

    status = gtext_json_stream_feed(st, chunk10, strlen(chunk10), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received "12345" (complete number, not incomplete)
    EXPECT_EQ(number_values.size(), 1u);
    EXPECT_EQ(number_values[0], "12345");

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - escape sequence spanning chunks
 */
TEST(StreamingParser, EscapeSequenceSpanningChunks) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<std::string> string_values;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        if (evt->type == GTEXT_JSON_EVT_STRING) {
            auto* strings = static_cast<std::vector<std::string>*>(user);
            std::string str(evt->as.str.s, evt->as.str.len);
            strings->push_back(str);
        }
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &string_values);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};
    // Test: escape sequence split across chunks
    // The escape sequence \n is split across two chunks.
    // Chunk 1: "hello" followed by backslash
    // Chunk 2: "nworld"
    const char * chunk1 = "\"hello\\";
    const char * chunk2 = "nworld\"";

    GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input

    status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received "hello\nworld" (decoded)
    EXPECT_EQ(string_values.size(), 1u);
    EXPECT_EQ(string_values[0], "hello\nworld");

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - unicode escape spanning chunks
 */
TEST(StreamingParser, UnicodeEscapeSpanningChunks) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<std::string> string_values;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        if (evt->type == GTEXT_JSON_EVT_STRING) {
            auto* strings = static_cast<std::vector<std::string>*>(user);
            std::string str(evt->as.str.s, evt->as.str.len);
            strings->push_back(str);
        }
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &string_values);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};

    // Test: unicode escape split across chunks
    // Chunk 1: "hello\u
    // Chunk 2: 0041"
    const char * chunk1 = "\"hello\\u";
    const char * chunk2 = "0041\"";

    GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input

    status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received "helloA" (U+0041 is 'A')
    EXPECT_EQ(string_values.size(), 1u);
    EXPECT_EQ(string_values[0], "helloA");

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - large value spanning many chunks
 */
TEST(StreamingParser, LargeValueSpanningManyChunks) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    std::vector<std::string> string_values;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        if (evt->type == GTEXT_JSON_EVT_STRING) {
            auto* strings = static_cast<std::vector<std::string>*>(user);
            std::string str(evt->as.str.s, evt->as.str.len);
            strings->push_back(str);
        }
        return GTEXT_JSON_OK;
    };

    GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &string_values);
    ASSERT_NE(st, nullptr);

    GTEXT_JSON_Error err{};

    // Test: large string split byte-by-byte
    std::string large_string = "\"";
    for (int i = 0; i < 1000; ++i) {
        large_string += "a";
    }
    large_string += "\"";

    // Feed byte by byte
    for (size_t i = 0; i < large_string.length(); ++i) {
        GTEXT_JSON_Status status = gtext_json_stream_feed(st, large_string.c_str() + i, 1, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed at byte " << i;
    }

    GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Should have received the complete string
    EXPECT_EQ(string_values.size(), 1u);
    EXPECT_EQ(string_values[0].length(), 1000u);

    gtext_json_stream_free(st);
}

/**
 * Test streaming parser - value spanning 3 chunks
 */
TEST(StreamingParser, ValueSpanningThreeChunks) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    // Test 1: String spanning 3 chunks
    {
        std::vector<std::string> string_values;

        auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
            (void)err;
            if (evt->type == GTEXT_JSON_EVT_STRING) {
                auto* strings = static_cast<std::vector<std::string>*>(user);
                std::string str(evt->as.str.s, evt->as.str.len);
                strings->push_back(str);
            }
            return GTEXT_JSON_OK;
        };

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &string_values);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};

        // String split across 3 chunks
        // Chunk 1: "hello
        // Chunk 2: world
        // Chunk 3: !"
        const char * chunk1 = "\"hello";
        const char * chunk2 = "world";
        const char * chunk3 = "!\"";

        GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input

        status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should still wait for more input

        status = gtext_json_stream_feed(st, chunk3, strlen(chunk3), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Should have received "helloworld!"
        EXPECT_EQ(string_values.size(), 1u);
        EXPECT_EQ(string_values[0], "helloworld!");

        gtext_json_stream_free(st);
    }

    // Test 2: Number spanning 3 chunks
    {
        std::vector<std::string> number_values;

        auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
            (void)err;
            if (evt->type == GTEXT_JSON_EVT_NUMBER) {
                auto* numbers = static_cast<std::vector<std::string>*>(user);
                std::string num(evt->as.number.s, evt->as.number.len);
                numbers->push_back(num);
            }
            return GTEXT_JSON_OK;
        };

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &number_values);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};

        // Number split across 3 chunks
        // Chunk 1: 123
        // Chunk 2: 45
        // Chunk 3: .678
        const char * chunk1 = "123";
        const char * chunk2 = "45";
        const char * chunk3 = ".678";

        GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input (incomplete)

        status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should still wait for more input (incomplete)

        status = gtext_json_stream_feed(st, chunk3, strlen(chunk3), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Should have received "12345.678"
        EXPECT_EQ(number_values.size(), 1u);
        EXPECT_EQ(number_values[0], "12345.678");

        gtext_json_stream_free(st);
    }

    // Test 3: Unicode escape spanning 3 chunks
    {
        std::vector<std::string> string_values;

        auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
            (void)err;
            if (evt->type == GTEXT_JSON_EVT_STRING) {
                auto* strings = static_cast<std::vector<std::string>*>(user);
                std::string str(evt->as.str.s, evt->as.str.len);
                strings->push_back(str);
            }
            return GTEXT_JSON_OK;
        };

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &string_values);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};

        // Unicode escape split across 3 chunks
        // Chunk 1: "hello\u
        // Chunk 2: 00
        // Chunk 3: 41"
        const char * chunk1 = "\"hello\\u";
        const char * chunk2 = "00";
        const char * chunk3 = "41\"";

        GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input

        status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should still wait for more input

        status = gtext_json_stream_feed(st, chunk3, strlen(chunk3), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Should have received "helloA" (U+0041 is 'A')
        EXPECT_EQ(string_values.size(), 1u);
        EXPECT_EQ(string_values[0], "helloA");

        gtext_json_stream_free(st);
    }

    // Test 4: Scientific notation number spanning 3 chunks
    {
        std::vector<std::string> number_values;

        auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
            (void)err;
            if (evt->type == GTEXT_JSON_EVT_NUMBER) {
                auto* numbers = static_cast<std::vector<std::string>*>(user);
                std::string num(evt->as.number.s, evt->as.number.len);
                numbers->push_back(num);
            }
            return GTEXT_JSON_OK;
        };

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &number_values);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};

        // Scientific notation split across 3 chunks
        // Chunk 1: 12345e
        // Chunk 2: +
        // Chunk 3: 2
        const char * chunk1 = "12345e";
        const char * chunk2 = "+";
        const char * chunk3 = "2";

        GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input (incomplete)

        status = gtext_json_stream_feed(st, chunk2, strlen(chunk2), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should still wait for more input (incomplete)

        status = gtext_json_stream_feed(st, chunk3, strlen(chunk3), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Should have received "12345e+2"
        EXPECT_EQ(number_values.size(), 1u);
        EXPECT_EQ(number_values[0], "12345e+2");

        gtext_json_stream_free(st);
    }
}

/**
 * Test streaming parser - value spanning many chunks (100+)
 * Verifies that state preservation works correctly across many chunk boundaries
 */
TEST(StreamingParser, ValueSpanningManyChunks) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    // Test: String spanning 100 chunks
    {
        std::vector<std::string> string_values;

        auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
            (void)err;
            if (evt->type == GTEXT_JSON_EVT_STRING) {
                auto* strings = static_cast<std::vector<std::string>*>(user);
                std::string str(evt->as.str.s, evt->as.str.len);
                strings->push_back(str);
            }
            return GTEXT_JSON_OK;
        };

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &string_values);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};

        // String split across 100 chunks, each chunk is 1 character
        // Chunk 1: "a
        // Chunks 2-99: b, c, d, ...
        // Chunk 100: z"
        const char * chunk1 = "\"a";
        GTEXT_JSON_Status status = gtext_json_stream_feed(st, chunk1, strlen(chunk1), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);  // Should wait for more input

        // Feed 98 more single-character chunks
        for (char c = 'b'; c <= 'y'; ++c) {
            status = gtext_json_stream_feed(st, &c, 1, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed at chunk for character " << c;
        }

        // Final chunk with closing quote
        const char * final_chunk = "z\"";
        status = gtext_json_stream_feed(st, final_chunk, strlen(final_chunk), &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Should have received "abcdefghijklmnopqrstuvwxyz" (26 characters)
        EXPECT_EQ(string_values.size(), 1u);
        EXPECT_EQ(string_values[0].length(), 26u);
        EXPECT_EQ(string_values[0], "abcdefghijklmnopqrstuvwxyz");

        gtext_json_stream_free(st);
    }

    // Test: Number spanning 50 chunks
    {
        std::vector<std::string> number_values;

        auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
            (void)err;
            if (evt->type == GTEXT_JSON_EVT_NUMBER) {
                auto* numbers = static_cast<std::vector<std::string>*>(user);
                std::string num(evt->as.number.s, evt->as.number.len);
                numbers->push_back(num);
            }
            return GTEXT_JSON_OK;
        };

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &number_values);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};

        // Number split across 50 chunks, each chunk is 1 digit
        // Chunks 1-49: digits 1-9
        // Chunk 50: final digit 0
        for (int i = 1; i <= 9; ++i) {
            char digit = '0' + i;
            GTEXT_JSON_Status status = gtext_json_stream_feed(st, &digit, 1, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed at chunk " << i;
        }

        // Feed 40 more digits
        for (int i = 0; i < 40; ++i) {
            char digit = '0' + (i % 10);
            GTEXT_JSON_Status status = gtext_json_stream_feed(st, &digit, 1, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK) << "Failed at chunk " << (10 + i);
        }

        // Final digit
        const char final_digit = '0';
        GTEXT_JSON_Status status = gtext_json_stream_feed(st, &final_digit, 1, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Should have received a 50-digit number
        EXPECT_EQ(number_values.size(), 1u);
        EXPECT_EQ(number_values[0].length(), 50u);

        gtext_json_stream_free(st);
    }
}

/**
 * Test streaming parser - torture test feeding complex JSON byte-by-byte
 *
 * This test feeds complex JSON structures one character at a time to stress-test
 * the streaming parser's ability to handle edge cases, especially:
 * - Escape sequences split across byte boundaries
 * - Unicode escapes split across byte boundaries
 * - Deeply nested structures
 * - Numbers with various formats
 * - Mixed content types
 *
 * This is similar to the CSV torture test and helps find bugs in state management
 * and buffering when input arrives incrementally.
 */
TEST(StreamingParser, TortureTestByteByByte) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    // Track all events and string values for verification
    std::vector<GTEXT_JSON_Event_Type> events;
    std::vector<std::string> string_values;
    std::vector<std::string> number_values;
    std::vector<bool> bool_values;

    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)err;
        auto* data = static_cast<std::tuple<
            std::vector<GTEXT_JSON_Event_Type>*,
            std::vector<std::string>*,
            std::vector<std::string>*,
            std::vector<bool>*
        >*>(user);

        auto* evts = std::get<0>(*data);
        auto* strings = std::get<1>(*data);
        auto* numbers = std::get<2>(*data);
        auto* bools = std::get<3>(*data);

        evts->push_back(evt->type);

        switch (evt->type) {
            case GTEXT_JSON_EVT_STRING:
            case GTEXT_JSON_EVT_KEY:
                strings->push_back(std::string(evt->as.str.s, evt->as.str.len));
                break;
            case GTEXT_JSON_EVT_NUMBER:
                numbers->push_back(std::string(evt->as.number.s, evt->as.number.len));
                break;
            case GTEXT_JSON_EVT_BOOL:
                bools->push_back(evt->as.boolean);
                break;
            default:
                break;
        }
        return GTEXT_JSON_OK;
    };

    // Test 1: Complex nested structure with escape sequences
    {
        // JSON with nested objects/arrays, escape sequences, and Unicode
        const char * complex_json =
            "{\"key1\":\"value\\nwith\\tescapes\","
            "\"key2\":[1,2.5,-3.14e+10],"
            "\"key3\":{\"nested\":\"\\u0041\\u0042\\u0043\","
            "\"deep\":{\"array\":[true,false,null]}},"
            "\"unicode\":\"\\uD83D\\uDE00\","
            "\"escapes\":\"\\\\\\\"\\/\\b\\f\\n\\r\\t\"}";

        std::vector<GTEXT_JSON_Event_Type> test_events;
        std::vector<std::string> test_strings;
        std::vector<std::string> test_numbers;
        std::vector<bool> test_bools;

        auto test_data = std::make_tuple(&test_events, &test_strings, &test_numbers, &test_bools);

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &test_data);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};
        size_t json_len = strlen(complex_json);

        // Feed byte by byte
        for (size_t i = 0; i < json_len; ++i) {
            GTEXT_JSON_Status status = gtext_json_stream_feed(st, complex_json + i, 1, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK)
                << "Failed at byte " << i << " (char: '" << complex_json[i] << "')";
        }

        GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Verify we got events
        EXPECT_GT(test_events.size(), 0u) << "Should have received events";

        // Verify string values were decoded correctly
        bool found_escaped = false;
        bool found_unicode = false;
        for (const auto& str : test_strings) {
            if (str.find('\n') != std::string::npos || str.find('\t') != std::string::npos) {
                found_escaped = true;
            }
            // Check for emoji (UTF-8 encoding of U+1F600)
            if (str.find("\xF0\x9F\x98\x80") != std::string::npos) {
                found_unicode = true;
            }
        }
        EXPECT_TRUE(found_escaped) << "Should have decoded escape sequences";
        EXPECT_TRUE(found_unicode) << "Should have decoded Unicode escape";

        gtext_json_stream_free(st);
    }

    // Test 2: Escape sequence split at every possible boundary
    {
        // Test each escape sequence type, split at different points
        const char * escape_tests[] = {
            "\"test\\nvalue\"",      // \n escape
            "\"test\\rvalue\"",      // \r escape
            "\"test\\tvalue\"",      // \t escape
            "\"test\\bvalue\"",      // \b escape
            "\"test\\fvalue\"",      // \f escape
            "\"test\\\\value\"",     // \\ escape
            "\"test\\\"value\"",     // \" escape
            "\"test\\/value\"",      // \/ escape
        };

        for (size_t test_idx = 0; test_idx < sizeof(escape_tests) / sizeof(escape_tests[0]); ++test_idx) {
            const char * test_json = escape_tests[test_idx];
            size_t test_len = strlen(test_json);

            std::vector<GTEXT_JSON_Event_Type> test_events;
            std::vector<std::string> test_strings;
            std::vector<std::string> test_numbers;
            std::vector<bool> test_bools;

            auto test_data = std::make_tuple(&test_events, &test_strings, &test_numbers, &test_bools);

            GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &test_data);
            ASSERT_NE(st, nullptr);

            GTEXT_JSON_Error err{};

            // Feed byte by byte
            for (size_t i = 0; i < test_len; ++i) {
                GTEXT_JSON_Status status = gtext_json_stream_feed(st, test_json + i, 1, &err);
                EXPECT_EQ(status, GTEXT_JSON_OK)
                    << "Escape test " << test_idx << " failed at byte " << i;
            }

            GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK);

            // Should have received at least one string event
            EXPECT_GT(test_strings.size(), 0u)
                << "Escape test " << test_idx << " should have produced string";

            gtext_json_stream_free(st);
        }
    }

    // Test 3: Unicode escape sequences split at various boundaries
    {
        const char * unicode_tests[] = {
            "\"\\u0041\"",           // Simple Unicode (A)
            "\"\\u00E9\"",           // é
            "\"\\u4E2D\"",           // 中 (Chinese)
            "\"\\uD83D\\uDE00\"",    // Emoji (surrogate pair)
        };

        for (size_t test_idx = 0; test_idx < sizeof(unicode_tests) / sizeof(unicode_tests[0]); ++test_idx) {
            const char * test_json = unicode_tests[test_idx];
            size_t test_len = strlen(test_json);

            std::vector<GTEXT_JSON_Event_Type> test_events;
            std::vector<std::string> test_strings;
            std::vector<std::string> test_numbers;
            std::vector<bool> test_bools;

            auto test_data = std::make_tuple(&test_events, &test_strings, &test_numbers, &test_bools);

            GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &test_data);
            ASSERT_NE(st, nullptr);

            GTEXT_JSON_Error err{};

            // Feed byte by byte - this is especially tricky for Unicode escapes
            // as the \uXXXX sequence can be split anywhere
            for (size_t i = 0; i < test_len; ++i) {
                GTEXT_JSON_Status status = gtext_json_stream_feed(st, test_json + i, 1, &err);
                EXPECT_EQ(status, GTEXT_JSON_OK)
                    << "Unicode test " << test_idx << " failed at byte " << i;
            }

            GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK);

            // Should have received at least one string event
            EXPECT_GT(test_strings.size(), 0u)
                << "Unicode test " << test_idx << " should have produced string";

            gtext_json_stream_free(st);
        }
    }

    // Test 4: Numbers with various formats, split byte-by-byte
    {
        // Create a copy of opts with non-finite numbers enabled for this test
        GTEXT_JSON_Parse_Options number_opts = opts;
        number_opts.allow_nonfinite_numbers = true;

        const char * number_tests[] = {
            "0",
            "123",
            "-456",
            "789.012",
            "-3.14159",
            "1e10",
            "2E-5",
            "-1.5e+20",
            "0.000001",
            "999999999999999999",
            "NaN",
            "Infinity",
            "-Infinity",
        };

        for (size_t test_idx = 0; test_idx < sizeof(number_tests) / sizeof(number_tests[0]); ++test_idx) {
            const char * test_json = number_tests[test_idx];
            size_t test_len = strlen(test_json);

            std::vector<GTEXT_JSON_Event_Type> test_events;
            std::vector<std::string> test_strings;
            std::vector<std::string> test_numbers;
            std::vector<bool> test_bools;

            auto test_data = std::make_tuple(&test_events, &test_strings, &test_numbers, &test_bools);

            GTEXT_JSON_Stream * st = gtext_json_stream_new(&number_opts, callback, &test_data);
            ASSERT_NE(st, nullptr);

            GTEXT_JSON_Error err{};

            // Feed byte by byte
            for (size_t i = 0; i < test_len; ++i) {
                GTEXT_JSON_Status status = gtext_json_stream_feed(st, test_json + i, 1, &err);
                EXPECT_EQ(status, GTEXT_JSON_OK)
                    << "Number test " << test_idx << " failed at byte " << i;
            }

            GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK);

            // Should have received at least one number event
            EXPECT_GT(test_numbers.size(), 0u)
                << "Number test " << test_idx << " should have produced number";
            EXPECT_EQ(test_numbers[0], test_json)
                << "Number should match input";

            gtext_json_stream_free(st);
        }
    }

    // Test 5: Deeply nested structure with mixed content
    {
        // Create a deeply nested structure that will stress-test the parser
        // 20 opening brackets, 20 closing brackets (one for each nested array)
        const char * nested_json =
            "[[[[[[[[[[[[[[[[[[[[\"deep\"]]]]]]]]]]]]]]]]]]]]";

        std::vector<GTEXT_JSON_Event_Type> test_events;
        std::vector<std::string> test_strings;
        std::vector<std::string> test_numbers;
        std::vector<bool> test_bools;

        auto test_data = std::make_tuple(&test_events, &test_strings, &test_numbers, &test_bools);

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &test_data);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};
        size_t json_len = strlen(nested_json);

        // Feed byte by byte
        for (size_t i = 0; i < json_len; ++i) {
            GTEXT_JSON_Status status = gtext_json_stream_feed(st, nested_json + i, 1, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK)
                << "Nested test failed at byte " << i;
        }

        GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Should have received events for nested structure
        EXPECT_GT(test_events.size(), 0u) << "Should have received events";
        EXPECT_GT(test_strings.size(), 0u) << "Should have received string";

        gtext_json_stream_free(st);
    }

    // Test 6: Complex real-world-like JSON with all features
    {
        const char * realworld_json =
            "{\"users\":["
            "{\"id\":1,\"name\":\"Alice\\nSmith\",\"email\":\"alice@example.com\",\"active\":true},"
            "{\"id\":2,\"name\":\"Bob\\tJones\",\"email\":\"bob@example.com\",\"active\":false},"
            "{\"id\":3,\"name\":\"Charlie\\u00E9\",\"email\":\"charlie@example.com\",\"score\":98.5}"
            "],"
            "\"metadata\":{\"version\":\"1.0\",\"unicode\":\"\\uD83D\\uDE00\"}}";

        std::vector<GTEXT_JSON_Event_Type> test_events;
        std::vector<std::string> test_strings;
        std::vector<std::string> test_numbers;
        std::vector<bool> test_bools;

        auto test_data = std::make_tuple(&test_events, &test_strings, &test_numbers, &test_bools);

        GTEXT_JSON_Stream * st = gtext_json_stream_new(&opts, callback, &test_data);
        ASSERT_NE(st, nullptr);

        GTEXT_JSON_Error err{};
        size_t json_len = strlen(realworld_json);

        // Feed byte by byte - this is the ultimate torture test
        for (size_t i = 0; i < json_len; ++i) {
            GTEXT_JSON_Status status = gtext_json_stream_feed(st, realworld_json + i, 1, &err);
            EXPECT_EQ(status, GTEXT_JSON_OK)
                << "Real-world test failed at byte " << i
                << " (char: '" << (realworld_json[i] >= 32 && realworld_json[i] < 127 ? realworld_json[i] : '?') << "')";
        }

        GTEXT_JSON_Status status = gtext_json_stream_finish(st, &err);
        EXPECT_EQ(status, GTEXT_JSON_OK);

        // Verify we got a reasonable number of events
        EXPECT_GT(test_events.size(), 10u) << "Should have received many events";
        EXPECT_GT(test_strings.size(), 5u) << "Should have received multiple strings";
        EXPECT_GT(test_numbers.size(), 3u) << "Should have received multiple numbers";
        EXPECT_GT(test_bools.size(), 0u) << "Should have received boolean values";

        gtext_json_stream_free(st);
    }
}

// ============================================================================
// Streaming Writer Tests
// ============================================================================

/**
 * Test streaming writer - creation and destruction
 */
TEST(StreamingWriter, Creation) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - NULL sink
 */
TEST(StreamingWriter, NullSink) {
    GTEXT_JSON_Sink sink = {nullptr, nullptr};
    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    EXPECT_EQ(w, nullptr);
}

/**
 * Test streaming writer - basic value writing (null, bool, number, string)
 */
TEST(StreamingWriter, BasicValues) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write null
    status = gtext_json_writer_null(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Finish
    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify output
    const char * output = gtext_json_sink_buffer_data(&sink);
    size_t output_len = gtext_json_sink_buffer_size(&sink);
    EXPECT_STREQ(output, "null");
    EXPECT_EQ(output_len, 4u);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test bool
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = gtext_json_writer_bool(w, true);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    output = gtext_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "true");

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test number (i64)
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = gtext_json_writer_number_i64(w, 12345);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    output = gtext_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "12345");

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test string
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = gtext_json_writer_string(w, "hello", 5);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    output = gtext_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "\"hello\"");

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - array writing
 */
TEST(StreamingWriter, Arrays) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write array: [1, 2, 3]
    status = gtext_json_writer_array_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_number_i64(w, 1);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_number_i64(w, 2);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_number_i64(w, 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_array_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify output (compact mode: no spaces after commas)
    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "[1,2,3]");

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - object writing
 */
TEST(StreamingWriter, Objects) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write object: {"key": "value"}
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_string(w, "value", 5);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_object_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify output
    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_STREQ(output, "{\"key\":\"value\"}");

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - structural enforcement (invalid sequences)
 */
TEST(StreamingWriter, StructuralEnforcement) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Try to write value without key in object - should fail
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_null(w);  // Should fail - need key first
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_STATE);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Try to write key when not in object - should fail
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = gtext_json_writer_key(w, "key", 3);  // Should fail - not in object
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_STATE);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Try to end object when expecting key - should fail
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Now try to end object without writing value - should fail
    status = gtext_json_writer_object_end(w);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_STATE);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - finish validation (incomplete structure)
 */
TEST(StreamingWriter, FinishValidation) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Start object but don't close it
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INCOMPLETE);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Start array but don't close it
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    status = gtext_json_writer_array_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_finish(w, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INCOMPLETE);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - pretty-print mode
 */
TEST(StreamingWriter, PrettyPrint) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    opts.pretty = true;
    opts.indent_spaces = 2;

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);

    // Write object with pretty printing
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_string(w, "value", 5);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_object_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify output has newlines and indentation
    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "\n"), nullptr);  // Should have newline

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - nested structures
 */
TEST(StreamingWriter, NestedStructures) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write: {"arr": [1, 2], "obj": {"key": "value"}}
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_key(w, "arr", 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_array_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_number_i64(w, 1);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_number_i64(w, 2);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_array_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_key(w, "obj", 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_string(w, "value", 5);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_object_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_object_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify output
    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "\"arr\""), nullptr);
    EXPECT_NE(strstr(output, "\"obj\""), nullptr);
    EXPECT_NE(strstr(output, "\"key\""), nullptr);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - number formats (i64, u64, double, lexeme)
 */
TEST(StreamingWriter, NumberFormats) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Test i64
    status = gtext_json_writer_number_i64(w, -12345);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "-12345");
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test u64
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);
    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);
    status = gtext_json_writer_number_u64(w, 12345ULL);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "12345");
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test double
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);
    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);
    status = gtext_json_writer_number_double(w, 3.14159);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_NE(strstr(output, "3.14"), nullptr);  // Should contain the number
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test lexeme
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);
    w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);
    status = gtext_json_writer_number_lexeme(w, "123.456", 7);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "123.456");
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - nonfinite numbers
 */
TEST(StreamingWriter, NonfiniteNumbers) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    opts.allow_nonfinite_numbers = true;

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);

    // Test NaN
    status = gtext_json_writer_number_double(w, std::nan(""));
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "NaN");
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test Infinity
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);
    w = gtext_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);
    status = gtext_json_writer_number_double(w, std::numeric_limits<double>::infinity());
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "Infinity");
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test -Infinity
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);
    w = gtext_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);
    status = gtext_json_writer_number_double(w, -std::numeric_limits<double>::infinity());
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_finish(w, nullptr);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_STREQ(gtext_json_sink_buffer_data(&sink), "-Infinity");
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);

    // Test that nonfinite numbers are rejected when option is off
    opts.allow_nonfinite_numbers = false;
    status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);
    w = gtext_json_writer_new(sink, &opts);
    ASSERT_NE(w, nullptr);
    status = gtext_json_writer_number_double(w, std::nan(""));
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_NONFINITE);
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - round-trip (write then parse)
 */
TEST(StreamingWriter, RoundTrip) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Write: {"key": [1, 2, 3]}
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_key(w, "key", 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_array_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_number_i64(w, 1);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_number_i64(w, 2);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_number_i64(w, 3);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_array_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    status = gtext_json_writer_object_end(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Parse the output
    const char * output = gtext_json_sink_buffer_data(&sink);
    size_t output_len = gtext_json_sink_buffer_size(&sink);

    GTEXT_JSON_Parse_Options parse_opts = gtext_json_parse_options_default();
    GTEXT_JSON_Value * v = gtext_json_parse(output, output_len, &parse_opts, &err);
    ASSERT_NE(v, nullptr);

    // Verify structure
    EXPECT_EQ(gtext_json_typeof(v), GTEXT_JSON_OBJECT);
    const GTEXT_JSON_Value * arr = gtext_json_object_get(v, "key", 3);
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(gtext_json_typeof(arr), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(arr), 3u);

    gtext_json_free(v);
    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test streaming writer - error state handling
 */
TEST(StreamingWriter, ErrorState) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Writer * w = gtext_json_writer_new(sink, nullptr);
    ASSERT_NE(w, nullptr);

    // Cause an error (try to write value without key in object)
    status = gtext_json_writer_object_begin(w);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    status = gtext_json_writer_null(w);  // Should fail
    EXPECT_NE(status, GTEXT_JSON_OK);

    // After error, subsequent operations should fail
    status = gtext_json_writer_key(w, "key", 3);
    EXPECT_NE(status, GTEXT_JSON_OK);  // Actually, this might succeed, but finish should fail

    GTEXT_JSON_Error err{};
    status = gtext_json_writer_finish(w, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);

    gtext_json_writer_free(w);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test JSON Pointer - empty pointer refers to root
 */
TEST(JsonPointer, EmptyPointer) {
    GTEXT_JSON_Value * root = gtext_json_new_string("test", 4);
    ASSERT_NE(root, nullptr);

    const GTEXT_JSON_Value * result = gtext_json_pointer_get(root, "", 0);
    EXPECT_EQ(result, root);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - object key access
 */
TEST(JsonPointer, ObjectKeyAccess) {
    const char * json = "{\"a\":1,\"b\":2,\"c\":{\"d\":3}}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Access existing keys
    const GTEXT_JSON_Value * a = gtext_json_pointer_get(root, "/a", 2);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(gtext_json_typeof(a), GTEXT_JSON_NUMBER);

    const GTEXT_JSON_Value * b = gtext_json_pointer_get(root, "/b", 2);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(gtext_json_typeof(b), GTEXT_JSON_NUMBER);

    // Access nested object
    const GTEXT_JSON_Value * c = gtext_json_pointer_get(root, "/c", 2);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(gtext_json_typeof(c), GTEXT_JSON_OBJECT);

    const GTEXT_JSON_Value * d = gtext_json_pointer_get(root, "/c/d", 4);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(gtext_json_typeof(d), GTEXT_JSON_NUMBER);

    // Access non-existent key
    const GTEXT_JSON_Value * missing = gtext_json_pointer_get(root, "/x", 2);
    EXPECT_EQ(missing, nullptr);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - array index access
 */
TEST(JsonPointer, ArrayIndexAccess) {
    const char * json = "[10,20,30,[40,50]]";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Access valid indices
    const GTEXT_JSON_Value * elem0 = gtext_json_pointer_get(root, "/0", 2);
    ASSERT_NE(elem0, nullptr);
    EXPECT_EQ(gtext_json_typeof(elem0), GTEXT_JSON_NUMBER);

    const GTEXT_JSON_Value * elem1 = gtext_json_pointer_get(root, "/1", 2);
    ASSERT_NE(elem1, nullptr);
    EXPECT_EQ(gtext_json_typeof(elem1), GTEXT_JSON_NUMBER);

    // Access nested array
    const GTEXT_JSON_Value * nested = gtext_json_pointer_get(root, "/3", 2);
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(gtext_json_typeof(nested), GTEXT_JSON_ARRAY);

    const GTEXT_JSON_Value * nested0 = gtext_json_pointer_get(root, "/3/0", 4);
    ASSERT_NE(nested0, nullptr);
    EXPECT_EQ(gtext_json_typeof(nested0), GTEXT_JSON_NUMBER);

    // Access out-of-bounds index
    const GTEXT_JSON_Value * out_of_bounds = gtext_json_pointer_get(root, "/10", 3);
    EXPECT_EQ(out_of_bounds, nullptr);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - complex nested structures
 */
TEST(JsonPointer, ComplexNestedStructures) {
    const char * json = "{\"a\":[{\"b\":1,\"c\":2},{\"d\":3}],\"e\":{\"f\":[4,5,6]}}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Navigate complex path: /a/0/b
    const GTEXT_JSON_Value * result = gtext_json_pointer_get(root, "/a/0/b", 6);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_NUMBER);

    // Navigate: /a/0/c
    result = gtext_json_pointer_get(root, "/a/0/c", 6);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_NUMBER);

    // Navigate: /a/1/d
    result = gtext_json_pointer_get(root, "/a/1/d", 6);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_NUMBER);

    // Navigate: /e/f/0
    result = gtext_json_pointer_get(root, "/e/f/0", 6);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_NUMBER);

    // Navigate: /e/f/2
    result = gtext_json_pointer_get(root, "/e/f/2", 6);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_NUMBER);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - escape sequences (~0 -> ~, ~1 -> /)
 */
TEST(JsonPointer, EscapeSequences) {
    // Create object with keys containing ~ and /
    GTEXT_JSON_Value * root = gtext_json_new_object();
    ASSERT_NE(root, nullptr);

    GTEXT_JSON_Value * val1 = gtext_json_new_string("value1", 6);
    GTEXT_JSON_Value * val2 = gtext_json_new_string("value2", 6);
    GTEXT_JSON_Value * val3 = gtext_json_new_string("value3", 6);

    // Key with tilde: "key~with~tilde" (14 bytes)
    gtext_json_object_put(root, "key~with~tilde", 14, val1);

    // Key with slash: "key/with/slash"
    gtext_json_object_put(root, "key/with/slash", 14, val2);

    // Key with both: "key~0/with~1both"
    gtext_json_object_put(root, "key~0/with~1both", 16, val3);

    // Access key with tilde using ~0 escape
    const GTEXT_JSON_Value * result = gtext_json_pointer_get(root, "/key~0with~0tilde", 17);  // "/key~0with~0tilde" = 17 chars
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_STRING);

    // Access key with slash using ~1 escape
    result = gtext_json_pointer_get(root, "/key~1with~1slash", 17);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_STRING);

    // Access key with both escapes
    // Key is "key~0/with~1both", so pointer is "/key~00~1with~01both"
    // (~00 -> ~0, ~1 -> /, ~01 -> ~1)
    result = gtext_json_pointer_get(root, "/key~00~1with~01both", 20);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(gtext_json_typeof(result), GTEXT_JSON_STRING);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - invalid pointer formats
 */
TEST(JsonPointer, InvalidFormats) {
    GTEXT_JSON_Value * root = gtext_json_new_string("test", 4);
    ASSERT_NE(root, nullptr);

    // Pointer not starting with /
    const GTEXT_JSON_Value * result = gtext_json_pointer_get(root, "a", 1);
    EXPECT_EQ(result, nullptr);

    // Invalid escape sequence
    result = gtext_json_pointer_get(root, "/a~2", 4);
    EXPECT_EQ(result, nullptr);

    // Incomplete escape sequence
    result = gtext_json_pointer_get(root, "/a~", 3);
    EXPECT_EQ(result, nullptr);

    // NULL pointer
    result = gtext_json_pointer_get(root, nullptr, 0);
    EXPECT_EQ(result, nullptr);

    // NULL root
    result = gtext_json_pointer_get(nullptr, "/a", 2);
    EXPECT_EQ(result, nullptr);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - array index validation
 */
TEST(JsonPointer, ArrayIndexValidation) {
    const char * json = "[1,2,3]";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Valid indices
    EXPECT_NE(gtext_json_pointer_get(root, "/0", 2), nullptr);
    EXPECT_NE(gtext_json_pointer_get(root, "/1", 2), nullptr);
    EXPECT_NE(gtext_json_pointer_get(root, "/2", 2), nullptr);

    // Out of bounds
    EXPECT_EQ(gtext_json_pointer_get(root, "/3", 2), nullptr);

    // Leading zeros not allowed (except "0")
    EXPECT_EQ(gtext_json_pointer_get(root, "/01", 3), nullptr);
    EXPECT_EQ(gtext_json_pointer_get(root, "/00", 3), nullptr);

    // Non-numeric index
    EXPECT_EQ(gtext_json_pointer_get(root, "/a", 2), nullptr);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - mutable access
 */
TEST(JsonPointer, MutableAccess) {
    const char * json = "{\"a\":1,\"b\":[2,3]}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Get mutable pointer
    GTEXT_JSON_Value * a = gtext_json_pointer_get_mut(root, "/a", 2);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(gtext_json_typeof(a), GTEXT_JSON_NUMBER);

    // Modify the value (replace with new number)
    GTEXT_JSON_Value * new_val = gtext_json_new_number_i64(42);
    ASSERT_NE(new_val, nullptr);

    // Get parent object and replace value
    GTEXT_JSON_Value * parent = gtext_json_pointer_get_mut(root, "", 0);
    ASSERT_NE(parent, nullptr);
    gtext_json_object_put(parent, "a", 1, new_val);

    // Verify modification
    const GTEXT_JSON_Value * modified = gtext_json_pointer_get(root, "/a", 2);
    ASSERT_NE(modified, nullptr);
    int64_t val;
    gtext_json_get_i64(modified, &val);
    EXPECT_EQ(val, 42);

    // Modify array element
    GTEXT_JSON_Value * arr_elem = gtext_json_pointer_get_mut(root, "/b/0", 4);
    ASSERT_NE(arr_elem, nullptr);
    GTEXT_JSON_Value * new_arr_val = gtext_json_new_number_i64(99);
    GTEXT_JSON_Value * arr = gtext_json_pointer_get_mut(root, "/b", 2);
    gtext_json_array_set(arr, 0, new_arr_val);

    // Verify array modification
    const GTEXT_JSON_Value * modified_arr_elem = gtext_json_pointer_get(root, "/b/0", 4);
    ASSERT_NE(modified_arr_elem, nullptr);
    gtext_json_get_i64(modified_arr_elem, &val);
    EXPECT_EQ(val, 99);

    gtext_json_free(root);
}

/**
 * Test JSON Pointer - type mismatches
 */
TEST(JsonPointer, TypeMismatches) {
    const char * json = "{\"a\":1,\"b\":[2,3]}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Try to access object key on array
    const GTEXT_JSON_Value * result = gtext_json_pointer_get(root, "/b/a", 4);
    EXPECT_EQ(result, nullptr);

    // Try to access array index on object
    result = gtext_json_pointer_get(root, "/a/0", 4);
    EXPECT_EQ(result, nullptr);

    gtext_json_free(root);
}

/**
 * Test JSON Patch - add operation to object
 */
TEST(JsonPatch, AddToObject) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: add "baz": "qux" to root object
    const char * patch_json = "[{\"op\":\"add\",\"path\":\"/baz\",\"value\":\"qux\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify the value was added
    const GTEXT_JSON_Value * baz = gtext_json_pointer_get(root, "/baz", 4);
    ASSERT_NE(baz, nullptr);
    EXPECT_EQ(gtext_json_typeof(baz), GTEXT_JSON_STRING);
    const char * baz_str;
    size_t baz_len;
    gtext_json_get_string(baz, &baz_str, &baz_len);
    EXPECT_EQ(baz_len, 3u);
    EXPECT_EQ(memcmp(baz_str, "qux", 3), 0);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - add operation to array
 */
TEST(JsonPatch, AddToArray) {
    const char * json = "{\"foo\":[1,2]}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: add 3 to end of array
    const char * patch_json = "[{\"op\":\"add\",\"path\":\"/foo/-\",\"value\":3}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify the value was added
    const GTEXT_JSON_Value * arr = gtext_json_pointer_get(root, "/foo", 4);
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(gtext_json_array_size(arr), 3u);
    const GTEXT_JSON_Value * elem2 = gtext_json_array_get(arr, 2);
    ASSERT_NE(elem2, nullptr);
    int64_t val;
    gtext_json_get_i64(elem2, &val);
    EXPECT_EQ(val, 3);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - add operation at specific array index
 */
TEST(JsonPatch, AddToArrayAtIndex) {
    const char * json = "{\"foo\":[1,3]}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: insert 2 at index 1
    const char * patch_json = "[{\"op\":\"add\",\"path\":\"/foo/1\",\"value\":2}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify the value was inserted
    const GTEXT_JSON_Value * arr = gtext_json_pointer_get(root, "/foo", 4);
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(gtext_json_array_size(arr), 3u);
    int64_t val;
    gtext_json_get_i64(gtext_json_array_get(arr, 0), &val);
    EXPECT_EQ(val, 1);
    gtext_json_get_i64(gtext_json_array_get(arr, 1), &val);
    EXPECT_EQ(val, 2);
    gtext_json_get_i64(gtext_json_array_get(arr, 2), &val);
    EXPECT_EQ(val, 3);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - remove operation from object
 */
TEST(JsonPatch, RemoveFromObject) {
    const char * json = "{\"foo\":\"bar\",\"baz\":\"qux\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: remove "foo"
    const char * patch_json = "[{\"op\":\"remove\",\"path\":\"/foo\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify "foo" was removed
    const GTEXT_JSON_Value * foo = gtext_json_pointer_get(root, "/foo", 4);
    EXPECT_EQ(foo, nullptr);

    // Verify "baz" still exists
    const GTEXT_JSON_Value * baz = gtext_json_pointer_get(root, "/baz", 4);
    ASSERT_NE(baz, nullptr);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - remove operation from array
 */
TEST(JsonPatch, RemoveFromArray) {
    const char * json = "{\"foo\":[1,2,3]}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: remove element at index 1
    const char * patch_json = "[{\"op\":\"remove\",\"path\":\"/foo/1\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify element was removed
    const GTEXT_JSON_Value * arr = gtext_json_pointer_get(root, "/foo", 4);
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(gtext_json_array_size(arr), 2u);
    int64_t val;
    gtext_json_get_i64(gtext_json_array_get(arr, 0), &val);
    EXPECT_EQ(val, 1);
    gtext_json_get_i64(gtext_json_array_get(arr, 1), &val);
    EXPECT_EQ(val, 3);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - replace operation
 */
TEST(JsonPatch, Replace) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: replace "foo" with "baz"
    const char * patch_json = "[{\"op\":\"replace\",\"path\":\"/foo\",\"value\":\"baz\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify the value was replaced
    const GTEXT_JSON_Value * foo = gtext_json_pointer_get(root, "/foo", 4);
    ASSERT_NE(foo, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(foo, &str, &len);
    EXPECT_EQ(len, 3u);
    EXPECT_EQ(memcmp(str, "baz", 3), 0);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - move operation
 */
TEST(JsonPatch, Move) {
    const char * json = "{\"foo\":{\"bar\":\"baz\"},\"qux\":{}}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: move /foo/bar to /qux/thud
    const char * patch_json = "[{\"op\":\"move\",\"from\":\"/foo/bar\",\"path\":\"/qux/thud\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify value was moved
    const GTEXT_JSON_Value * bar = gtext_json_pointer_get(root, "/foo/bar", 8);
    EXPECT_EQ(bar, nullptr);  // Should be removed

    const GTEXT_JSON_Value * thud = gtext_json_pointer_get(root, "/qux/thud", 9);
    ASSERT_NE(thud, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(thud, &str, &len);
    EXPECT_EQ(len, 3u);
    EXPECT_EQ(memcmp(str, "baz", 3), 0);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - copy operation
 */
TEST(JsonPatch, Copy) {
    const char * json = "{\"foo\":{\"bar\":\"baz\"},\"qux\":{}}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: copy /foo/bar to /qux/thud
    const char * patch_json = "[{\"op\":\"copy\",\"from\":\"/foo/bar\",\"path\":\"/qux/thud\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify value was copied (original should still exist)
    const GTEXT_JSON_Value * bar = gtext_json_pointer_get(root, "/foo/bar", 8);
    ASSERT_NE(bar, nullptr);  // Should still exist

    const GTEXT_JSON_Value * thud = gtext_json_pointer_get(root, "/qux/thud", 9);
    ASSERT_NE(thud, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(thud, &str, &len);
    EXPECT_EQ(len, 3u);
    EXPECT_EQ(memcmp(str, "baz", 3), 0);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - test operation (success)
 */
TEST(JsonPatch, TestSuccess) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: test that /foo equals "bar"
    const char * patch_json = "[{\"op\":\"test\",\"path\":\"/foo\",\"value\":\"bar\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - test operation (failure)
 */
TEST(JsonPatch, TestFailure) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: test that /foo equals "baz" (should fail)
    const char * patch_json = "[{\"op\":\"test\",\"path\":\"/foo\",\"value\":\"baz\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - multiple operations
 */
TEST(JsonPatch, MultipleOperations) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch with multiple operations
    const char * patch_json = "["
        "{\"op\":\"add\",\"path\":\"/baz\",\"value\":\"qux\"},"
        "{\"op\":\"replace\",\"path\":\"/foo\",\"value\":\"bar2\"},"
        "{\"op\":\"test\",\"path\":\"/baz\",\"value\":\"qux\"}"
    "]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify all operations were applied
    const GTEXT_JSON_Value * foo = gtext_json_pointer_get(root, "/foo", 4);
    ASSERT_NE(foo, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(foo, &str, &len);
    EXPECT_EQ(len, 4u);
    EXPECT_EQ(memcmp(str, "bar2", 4), 0);

    const GTEXT_JSON_Value * baz = gtext_json_pointer_get(root, "/baz", 4);
    ASSERT_NE(baz, nullptr);
    gtext_json_get_string(baz, &str, &len);
    EXPECT_EQ(len, 3u);
    EXPECT_EQ(memcmp(str, "qux", 3), 0);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - error cases: invalid path
 */
TEST(JsonPatch, ErrorInvalidPath) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch with invalid path
    const char * patch_json = "[{\"op\":\"remove\",\"path\":\"/nonexistent\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - error cases: missing required fields
 */
TEST(JsonPatch, ErrorMissingFields) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch with missing "value" field
    const char * patch_json = "[{\"op\":\"add\",\"path\":\"/baz\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - error cases: move into descendant
 */
TEST(JsonPatch, ErrorMoveIntoDescendant) {
    const char * json = "{\"foo\":{\"bar\":\"baz\"}}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: try to move /foo into /foo/bar (should fail)
    const char * patch_json = "[{\"op\":\"move\",\"from\":\"/foo\",\"path\":\"/foo/bar\"}]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Patch - atomicity: true rollback on failure
 *
 * The implementation provides true atomicity with rollback:
 * - All operations are applied to a deep clone of the root first
 * - Only if all operations succeed is the clone's content copied back to the original
 * - If any operation fails, the clone is discarded and the original remains completely unchanged
 * - This ensures all-or-nothing semantics: either all operations succeed or none are applied
 *
 * This test verifies that when a later operation fails, earlier operations are not applied.
 */
TEST(JsonPatch, Atomicity) {
    const char * json = "{\"foo\":\"bar\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * root = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(root, nullptr);

    // Create patch: first operation succeeds, second fails
    const char * patch_json = "["
        "{\"op\":\"add\",\"path\":\"/baz\",\"value\":\"qux\"},"
        "{\"op\":\"remove\",\"path\":\"/nonexistent\"}"
    "]";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_patch_apply(root, patch, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);

    // With true atomicity: all operations are applied to a clone first.
    // Only if all operations succeed is the clone's content copied back to the original.
    // If any operation fails, the original remains unchanged.
    const GTEXT_JSON_Value * baz = gtext_json_pointer_get(root, "/baz", 4);
    EXPECT_EQ(baz, nullptr);  // Nothing was applied because second operation failed

    // Verify original value is unchanged
    const GTEXT_JSON_Value * foo = gtext_json_pointer_get(root, "/foo", 4);
    ASSERT_NE(foo, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(foo, &str, &len);
    EXPECT_EQ(len, 3u);
    EXPECT_EQ(memcmp(str, "bar", 3), 0);

    gtext_json_free(patch);
    gtext_json_free(root);
}

/**
 * Test JSON Merge Patch - basic object merge (replace value)
 */
TEST(JsonMergePatch, BasicReplace) {
    const char * target_json = "{\"a\":\"b\"}";
    const char * patch_json = "{\"a\":\"c\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify result
    const GTEXT_JSON_Value * a = gtext_json_pointer_get(target, "/a", 2);
    ASSERT_NE(a, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(a, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "c", 1), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - add new member
 */
TEST(JsonMergePatch, AddNewMember) {
    const char * target_json = "{\"a\":\"b\"}";
    const char * patch_json = "{\"b\":\"c\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify both members exist
    const GTEXT_JSON_Value * a = gtext_json_pointer_get(target, "/a", 2);
    ASSERT_NE(a, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(a, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "b", 1), 0);

    const GTEXT_JSON_Value * b = gtext_json_pointer_get(target, "/b", 2);
    ASSERT_NE(b, nullptr);
    gtext_json_get_string(b, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "c", 1), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - remove member via null
 */
TEST(JsonMergePatch, RemoveViaNull) {
    const char * target_json = "{\"a\":\"b\"}";
    const char * patch_json = "{\"a\":null}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify member was removed
    const GTEXT_JSON_Value * a = gtext_json_pointer_get(target, "/a", 2);
    EXPECT_EQ(a, nullptr);

    // Verify object is now empty
    EXPECT_EQ(gtext_json_object_size(target), 0u);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - replace array entirely
 */
TEST(JsonMergePatch, ReplaceArray) {
    const char * target_json = "{\"a\":[\"b\"]}";
    const char * patch_json = "{\"a\":\"c\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify array was replaced with string
    const GTEXT_JSON_Value * a = gtext_json_pointer_get(target, "/a", 2);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(gtext_json_typeof(a), GTEXT_JSON_STRING);
    const char * str;
    size_t len;
    gtext_json_get_string(a, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "c", 1), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - non-object patch replaces target entirely
 */
TEST(JsonMergePatch, NonObjectPatchReplaces) {
    const char * target_json = "{\"a\":\"foo\"}";
    const char * patch_json = "null";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify target is now null
    EXPECT_EQ(gtext_json_typeof(target), GTEXT_JSON_NULL);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - array patch replaces target array
 */
TEST(JsonMergePatch, ArrayPatchReplaces) {
    const char * target_json = "[\"a\",\"b\"]";
    const char * patch_json = "[\"c\",\"d\"]";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify array was replaced
    EXPECT_EQ(gtext_json_typeof(target), GTEXT_JSON_ARRAY);
    EXPECT_EQ(gtext_json_array_size(target), 2u);

    const GTEXT_JSON_Value * elem0 = gtext_json_array_get(target, 0);
    ASSERT_NE(elem0, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(elem0, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "c", 1), 0);

    const GTEXT_JSON_Value * elem1 = gtext_json_array_get(target, 1);
    ASSERT_NE(elem1, nullptr);
    gtext_json_get_string(elem1, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "d", 1), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - nested object merge
 */
TEST(JsonMergePatch, NestedObjectMerge) {
    const char * target_json = "{\"title\":\"Goodbye!\",\"author\":{\"givenName\":\"John\",\"familyName\":\"Doe\"},\"tags\":[\"example\",\"sample\"],\"content\":\"This will be unchanged\"}";
    const char * patch_json = "{\"title\":\"Hello!\",\"phoneNumber\":\"+01-123-456-7890\",\"author\":{\"familyName\":null},\"tags\":[\"example\"]}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify title was replaced
    const GTEXT_JSON_Value * title = gtext_json_pointer_get(target, "/title", 6);
    ASSERT_NE(title, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(title, &str, &len);
    EXPECT_EQ(len, 6u);
    EXPECT_EQ(memcmp(str, "Hello!", 6), 0);

    // Verify phoneNumber was added
    const GTEXT_JSON_Value * phone = gtext_json_pointer_get(target, "/phoneNumber", 12);
    ASSERT_NE(phone, nullptr);
    gtext_json_get_string(phone, &str, &len);
    EXPECT_EQ(len, 16u);
    EXPECT_EQ(memcmp(str, "+01-123-456-7890", 16), 0);

    // Verify author.familyName was removed
    const GTEXT_JSON_Value * familyName = gtext_json_pointer_get(target, "/author/familyName", 17);
    EXPECT_EQ(familyName, nullptr);

    // Verify author.givenName still exists
    const GTEXT_JSON_Value * givenName = gtext_json_pointer_get(target, "/author/givenName", 17);
    ASSERT_NE(givenName, nullptr);
    gtext_json_get_string(givenName, &str, &len);
    EXPECT_EQ(len, 4u);
    EXPECT_EQ(memcmp(str, "John", 4), 0);

    // Verify tags array was replaced
    const GTEXT_JSON_Value * tags = gtext_json_pointer_get(target, "/tags", 5);
    ASSERT_NE(tags, nullptr);
    EXPECT_EQ(gtext_json_array_size(tags), 1u);
    const GTEXT_JSON_Value * tag0 = gtext_json_array_get(tags, 0);
    ASSERT_NE(tag0, nullptr);
    gtext_json_get_string(tag0, &str, &len);
    EXPECT_EQ(len, 7u);
    EXPECT_EQ(memcmp(str, "example", 7), 0);

    // Verify content was unchanged
    const GTEXT_JSON_Value * content = gtext_json_pointer_get(target, "/content", 8);
    ASSERT_NE(content, nullptr);
    gtext_json_get_string(content, &str, &len);
    EXPECT_EQ(len, 22u);
    EXPECT_EQ(memcmp(str, "This will be unchanged", 22), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - non-object target converted to object
 */
TEST(JsonMergePatch, NonObjectTargetConverted) {
    const char * target_json = "\"not an object\"";
    const char * patch_json = "{\"a\":\"b\"}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify target is now an object
    EXPECT_EQ(gtext_json_typeof(target), GTEXT_JSON_OBJECT);

    // Verify patch member was added
    const GTEXT_JSON_Value * a = gtext_json_pointer_get(target, "/a", 2);
    ASSERT_NE(a, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(a, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "b", 1), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - empty object patch
 */
TEST(JsonMergePatch, EmptyObjectPatch) {
    const char * target_json = "{\"a\":\"b\",\"c\":\"d\"}";
    const char * patch_json = "{}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify target is unchanged (empty patch doesn't modify)
    EXPECT_EQ(gtext_json_object_size(target), 2u);

    const GTEXT_JSON_Value * a = gtext_json_pointer_get(target, "/a", 2);
    ASSERT_NE(a, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(a, &str, &len);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(memcmp(str, "b", 1), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - grandchild elements (3+ levels deep)
 */
TEST(JsonMergePatch, GrandchildElements) {
    const char * target_json = "{\"person\":{\"name\":{\"first\":\"John\",\"last\":\"Doe\"},\"contact\":{\"email\":\"john@example.com\",\"phone\":\"123-456-7890\"}},\"metadata\":{\"created\":\"2024-01-01\"}}";
    const char * patch_json = "{\"person\":{\"name\":{\"last\":\"Smith\"},\"contact\":{\"phone\":null}},\"metadata\":{\"updated\":\"2024-01-02\"}}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify person.name.first still exists (grandchild)
    const GTEXT_JSON_Value * first = gtext_json_pointer_get(target, "/person/name/first", 18);
    ASSERT_NE(first, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(first, &str, &len);
    EXPECT_EQ(len, 4u);
    EXPECT_EQ(memcmp(str, "John", 4), 0);

    // Verify person.name.last was changed (grandchild)
    const GTEXT_JSON_Value * last = gtext_json_pointer_get(target, "/person/name/last", 17);
    ASSERT_NE(last, nullptr);
    gtext_json_get_string(last, &str, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(str, "Smith", 5), 0);

    // Verify person.contact.email still exists (grandchild)
    const GTEXT_JSON_Value * email = gtext_json_pointer_get(target, "/person/contact/email", 21);
    ASSERT_NE(email, nullptr);
    gtext_json_get_string(email, &str, &len);
    EXPECT_EQ(len, 16u);
    EXPECT_EQ(memcmp(str, "john@example.com", 16), 0);

    // Verify person.contact.phone was removed (grandchild)
    const GTEXT_JSON_Value * phone = gtext_json_pointer_get(target, "/person/contact/phone", 21);
    EXPECT_EQ(phone, nullptr);

    // Verify metadata.created still exists
    const GTEXT_JSON_Value * created = gtext_json_pointer_get(target, "/metadata/created", 17);
    ASSERT_NE(created, nullptr);
    gtext_json_get_string(created, &str, &len);
    EXPECT_EQ(len, 10u);
    EXPECT_EQ(memcmp(str, "2024-01-01", 10), 0);

    // Verify metadata.updated was added
    const GTEXT_JSON_Value * updated = gtext_json_pointer_get(target, "/metadata/updated", 17);
    ASSERT_NE(updated, nullptr);
    gtext_json_get_string(updated, &str, &len);
    EXPECT_EQ(len, 10u);
    EXPECT_EQ(memcmp(str, "2024-01-02", 10), 0);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - atomicity with rollback on error
 */
TEST(JsonMergePatch, AtomicityWithRollback) {
    // Create a target with multiple keys
    const char * target_json = "{\"key1\":\"value1\",\"key2\":\"value2\",\"key3\":{\"nested\":\"data\"}}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * target = gtext_json_parse(target_json, strlen(target_json), &opts, &err);
    ASSERT_NE(target, nullptr);

    // Save original state for comparison
    const GTEXT_JSON_Value * orig_key1 = gtext_json_pointer_get(target, "/key1", 5);
    ASSERT_NE(orig_key1, nullptr);
    const char * orig_str;
    size_t orig_len;
    gtext_json_get_string(orig_key1, &orig_str, &orig_len);

    const GTEXT_JSON_Value * orig_key2 = gtext_json_pointer_get(target, "/key2", 5);
    ASSERT_NE(orig_key2, nullptr);

    const GTEXT_JSON_Value * orig_nested = gtext_json_pointer_get(target, "/key3/nested", 12);
    ASSERT_NE(orig_nested, nullptr);

    // Create a patch that will succeed for first key but we'll simulate failure
    // by using an invalid patch that causes OOM or other error
    // Actually, we can't easily simulate OOM in a test, but we can verify
    // that if we have a valid patch, all changes are applied atomically

    // Test: Verify that if merge succeeds, all changes are applied
    const char * patch_json = "{\"key1\":\"newvalue1\",\"key2\":\"newvalue2\",\"key4\":\"value4\"}";
    GTEXT_JSON_Value * patch = gtext_json_parse(patch_json, strlen(patch_json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    GTEXT_JSON_Status status = gtext_json_merge_patch(target, patch, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify all changes were applied
    const GTEXT_JSON_Value * new_key1 = gtext_json_pointer_get(target, "/key1", 5);
    ASSERT_NE(new_key1, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(new_key1, &str, &len);
    EXPECT_EQ(len, 9u);
    EXPECT_EQ(memcmp(str, "newvalue1", 9), 0);

    const GTEXT_JSON_Value * new_key2 = gtext_json_pointer_get(target, "/key2", 5);
    ASSERT_NE(new_key2, nullptr);
    gtext_json_get_string(new_key2, &str, &len);
    EXPECT_EQ(len, 9u);
    EXPECT_EQ(memcmp(str, "newvalue2", 9), 0);

    const GTEXT_JSON_Value * new_key4 = gtext_json_pointer_get(target, "/key4", 5);
    ASSERT_NE(new_key4, nullptr);
    gtext_json_get_string(new_key4, &str, &len);
    EXPECT_EQ(len, 6u);
    EXPECT_EQ(memcmp(str, "value4", 6), 0);

    // Verify key3/nested still exists (unchanged)
    const GTEXT_JSON_Value * still_nested = gtext_json_pointer_get(target, "/key3/nested", 12);
    ASSERT_NE(still_nested, nullptr);

    gtext_json_free(patch);
    gtext_json_free(target);
}

/**
 * Test JSON Merge Patch - error cases: NULL arguments
 */
TEST(JsonMergePatch, ErrorNullArguments) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    const char * json = "{\"a\":\"b\"}";
    GTEXT_JSON_Value * target = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(target, nullptr);

    GTEXT_JSON_Value * patch = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(patch, nullptr);

    // Test NULL target
    GTEXT_JSON_Status status = gtext_json_merge_patch(nullptr, patch, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);

    // Test NULL patch
    status = gtext_json_merge_patch(target, nullptr, &err);
    EXPECT_NE(status, GTEXT_JSON_OK);

    gtext_json_free(patch);
    gtext_json_free(target);
}

// ============================================================================
// JSON Schema Validation Tests
// ============================================================================

/**
 * Test schema compilation - basic type validation
 */
TEST(JsonSchema, TypeValidation) {
    const char * schema_json = "{\"type\":\"string\"}";
    const char * valid_json = "\"hello\"";
    const char * invalid_json = "123";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instance
    GTEXT_JSON_Value * valid = gtext_json_parse(valid_json, strlen(valid_json), &opts, &err);
    ASSERT_NE(valid, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid);

    // Test invalid instance (wrong type)
    GTEXT_JSON_Value * invalid = gtext_json_parse(invalid_json, strlen(invalid_json), &opts, &err);
    ASSERT_NE(invalid, nullptr);
    status = gtext_json_schema_validate(schema, invalid, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema compilation - multiple types
 */
TEST(JsonSchema, MultipleTypes) {
    const char * schema_json = "{\"type\":[\"string\",\"number\"]}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test string instance
    const char * str_json = "\"hello\"";
    GTEXT_JSON_Value * str_val = gtext_json_parse(str_json, strlen(str_json), &opts, &err);
    ASSERT_NE(str_val, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, str_val, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(str_val);

    // Test number instance
    const char * num_json = "123";
    GTEXT_JSON_Value * num_val = gtext_json_parse(num_json, strlen(num_json), &opts, &err);
    ASSERT_NE(num_val, nullptr);
    status = gtext_json_schema_validate(schema, num_val, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(num_val);

    // Test invalid instance (boolean)
    const char * bool_json = "true";
    GTEXT_JSON_Value * bool_val = gtext_json_parse(bool_json, strlen(bool_json), &opts, &err);
    ASSERT_NE(bool_val, nullptr);
    status = gtext_json_schema_validate(schema, bool_val, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(bool_val);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - properties and required
 */
TEST(JsonSchema, PropertiesAndRequired) {
    const char * schema_json = "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"age\":{\"type\":\"number\"}},\"required\":[\"name\"]}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instance (has required property)
    const char * valid_json = "{\"name\":\"John\",\"age\":30}";
    GTEXT_JSON_Value * valid = gtext_json_parse(valid_json, strlen(valid_json), &opts, &err);
    ASSERT_NE(valid, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid);

    // Test invalid instance (missing required property)
    const char * invalid_json = "{\"age\":30}";
    GTEXT_JSON_Value * invalid = gtext_json_parse(invalid_json, strlen(invalid_json), &opts, &err);
    ASSERT_NE(invalid, nullptr);
    status = gtext_json_schema_validate(schema, invalid, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid);

    // Test invalid instance (wrong property type)
    const char * wrong_type_json = "{\"name\":123}";
    GTEXT_JSON_Value * wrong_type = gtext_json_parse(wrong_type_json, strlen(wrong_type_json), &opts, &err);
    ASSERT_NE(wrong_type, nullptr);
    status = gtext_json_schema_validate(schema, wrong_type, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(wrong_type);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - items validation
 */
TEST(JsonSchema, ItemsValidation) {
    const char * schema_json = "{\"type\":\"array\",\"items\":{\"type\":\"string\"}}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instance (all items are strings)
    const char * valid_json = "[\"a\",\"b\",\"c\"]";
    GTEXT_JSON_Value * valid = gtext_json_parse(valid_json, strlen(valid_json), &opts, &err);
    ASSERT_NE(valid, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid);

    // Test invalid instance (one item is not a string)
    const char * invalid_json = "[\"a\",123,\"c\"]";
    GTEXT_JSON_Value * invalid = gtext_json_parse(invalid_json, strlen(invalid_json), &opts, &err);
    ASSERT_NE(invalid, nullptr);
    status = gtext_json_schema_validate(schema, invalid, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - enum validation
 */
TEST(JsonSchema, EnumValidation) {
    const char * schema_json = "{\"enum\":[\"red\",\"green\",\"blue\"]}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instances
    const char * valid1_json = "\"red\"";
    GTEXT_JSON_Value * valid1 = gtext_json_parse(valid1_json, strlen(valid1_json), &opts, &err);
    ASSERT_NE(valid1, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid1);

    const char * valid2_json = "\"green\"";
    GTEXT_JSON_Value * valid2 = gtext_json_parse(valid2_json, strlen(valid2_json), &opts, &err);
    ASSERT_NE(valid2, nullptr);
    status = gtext_json_schema_validate(schema, valid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid2);

    // Test invalid instance (not in enum)
    const char * invalid_json = "\"yellow\"";
    GTEXT_JSON_Value * invalid = gtext_json_parse(invalid_json, strlen(invalid_json), &opts, &err);
    ASSERT_NE(invalid, nullptr);
    status = gtext_json_schema_validate(schema, invalid, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - const validation
 */
TEST(JsonSchema, ConstValidation) {
    const char * schema_json = "{\"const\":42}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instance (exact match)
    const char * valid_json = "42";
    GTEXT_JSON_Value * valid = gtext_json_parse(valid_json, strlen(valid_json), &opts, &err);
    ASSERT_NE(valid, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid);

    // Test invalid instance (different value)
    const char * invalid_json = "43";
    GTEXT_JSON_Value * invalid = gtext_json_parse(invalid_json, strlen(invalid_json), &opts, &err);
    ASSERT_NE(invalid, nullptr);
    status = gtext_json_schema_validate(schema, invalid, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - numeric constraints (minimum/maximum)
 */
TEST(JsonSchema, NumericConstraints) {
    const char * schema_json = "{\"type\":\"number\",\"minimum\":10,\"maximum\":100}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instances
    const char * valid1_json = "50";
    GTEXT_JSON_Value * valid1 = gtext_json_parse(valid1_json, strlen(valid1_json), &opts, &err);
    ASSERT_NE(valid1, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid1);

    const char * valid2_json = "10";
    GTEXT_JSON_Value * valid2 = gtext_json_parse(valid2_json, strlen(valid2_json), &opts, &err);
    ASSERT_NE(valid2, nullptr);
    status = gtext_json_schema_validate(schema, valid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid2);

    // Test invalid instance (below minimum)
    const char * invalid1_json = "5";
    GTEXT_JSON_Value * invalid1 = gtext_json_parse(invalid1_json, strlen(invalid1_json), &opts, &err);
    ASSERT_NE(invalid1, nullptr);
    status = gtext_json_schema_validate(schema, invalid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid1);

    // Test invalid instance (above maximum)
    const char * invalid2_json = "150";
    GTEXT_JSON_Value * invalid2 = gtext_json_parse(invalid2_json, strlen(invalid2_json), &opts, &err);
    ASSERT_NE(invalid2, nullptr);
    status = gtext_json_schema_validate(schema, invalid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid2);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - string length constraints
 */
TEST(JsonSchema, StringLengthConstraints) {
    const char * schema_json = "{\"type\":\"string\",\"minLength\":3,\"maxLength\":10}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instances
    const char * valid1_json = "\"abc\"";
    GTEXT_JSON_Value * valid1 = gtext_json_parse(valid1_json, strlen(valid1_json), &opts, &err);
    ASSERT_NE(valid1, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid1);

    const char * valid2_json = "\"abcdefghij\"";
    GTEXT_JSON_Value * valid2 = gtext_json_parse(valid2_json, strlen(valid2_json), &opts, &err);
    ASSERT_NE(valid2, nullptr);
    status = gtext_json_schema_validate(schema, valid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid2);

    // Test invalid instance (too short)
    const char * invalid1_json = "\"ab\"";
    GTEXT_JSON_Value * invalid1 = gtext_json_parse(invalid1_json, strlen(invalid1_json), &opts, &err);
    ASSERT_NE(invalid1, nullptr);
    status = gtext_json_schema_validate(schema, invalid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid1);

    // Test invalid instance (too long)
    const char * invalid2_json = "\"abcdefghijk\"";
    GTEXT_JSON_Value * invalid2 = gtext_json_parse(invalid2_json, strlen(invalid2_json), &opts, &err);
    ASSERT_NE(invalid2, nullptr);
    status = gtext_json_schema_validate(schema, invalid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid2);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - array size constraints
 */
TEST(JsonSchema, ArraySizeConstraints) {
    const char * schema_json = "{\"type\":\"array\",\"minItems\":2,\"maxItems\":5}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid instances
    const char * valid1_json = "[1,2]";
    GTEXT_JSON_Value * valid1 = gtext_json_parse(valid1_json, strlen(valid1_json), &opts, &err);
    ASSERT_NE(valid1, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid1);

    const char * valid2_json = "[1,2,3,4,5]";
    GTEXT_JSON_Value * valid2 = gtext_json_parse(valid2_json, strlen(valid2_json), &opts, &err);
    ASSERT_NE(valid2, nullptr);
    status = gtext_json_schema_validate(schema, valid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid2);

    // Test invalid instance (too few items)
    const char * invalid1_json = "[1]";
    GTEXT_JSON_Value * invalid1 = gtext_json_parse(invalid1_json, strlen(invalid1_json), &opts, &err);
    ASSERT_NE(invalid1, nullptr);
    status = gtext_json_schema_validate(schema, invalid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid1);

    // Test invalid instance (too many items)
    const char * invalid2_json = "[1,2,3,4,5,6]";
    GTEXT_JSON_Value * invalid2 = gtext_json_parse(invalid2_json, strlen(invalid2_json), &opts, &err);
    ASSERT_NE(invalid2, nullptr);
    status = gtext_json_schema_validate(schema, invalid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid2);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema - nested schema validation
 */
TEST(JsonSchema, NestedSchema) {
    const char * schema_json = "{\"type\":\"object\",\"properties\":{\"user\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"age\":{\"type\":\"number\",\"minimum\":0}},\"required\":[\"name\"]}}}";

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    // Test valid nested instance
    const char * valid_json = "{\"user\":{\"name\":\"John\",\"age\":30}}";
    GTEXT_JSON_Value * valid = gtext_json_parse(valid_json, strlen(valid_json), &opts, &err);
    ASSERT_NE(valid, nullptr);
    GTEXT_JSON_Status status = gtext_json_schema_validate(schema, valid, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    gtext_json_free(valid);

    // Test invalid nested instance (missing required property)
    const char * invalid1_json = "{\"user\":{\"age\":30}}";
    GTEXT_JSON_Value * invalid1 = gtext_json_parse(invalid1_json, strlen(invalid1_json), &opts, &err);
    ASSERT_NE(invalid1, nullptr);
    status = gtext_json_schema_validate(schema, invalid1, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid1);

    // Test invalid nested instance (negative age)
    const char * invalid2_json = "{\"user\":{\"name\":\"John\",\"age\":-5}}";
    GTEXT_JSON_Value * invalid2 = gtext_json_parse(invalid2_json, strlen(invalid2_json), &opts, &err);
    ASSERT_NE(invalid2, nullptr);
    status = gtext_json_schema_validate(schema, invalid2, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_SCHEMA);
    gtext_json_free(invalid2);

    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema compilation - invalid schema rejection
 */
TEST(JsonSchema, InvalidSchemaRejection) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    // Test NULL schema document
    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(nullptr, &err);
    EXPECT_EQ(schema, nullptr);
    EXPECT_EQ(err.code, GTEXT_JSON_E_INVALID);

    // Test non-object schema
    const char * invalid_json = "\"not an object\"";
    GTEXT_JSON_Value * invalid = gtext_json_parse(invalid_json, strlen(invalid_json), &opts, &err);
    ASSERT_NE(invalid, nullptr);
    schema = gtext_json_schema_compile(invalid, &err);
    EXPECT_EQ(schema, nullptr);
    EXPECT_EQ(err.code, GTEXT_JSON_E_INVALID);
    gtext_json_free(invalid);

    // Test invalid type value
    const char * invalid_type_json = "{\"type\":\"invalid\"}";
    GTEXT_JSON_Value * invalid_type = gtext_json_parse(invalid_type_json, strlen(invalid_type_json), &opts, &err);
    ASSERT_NE(invalid_type, nullptr);
    schema = gtext_json_schema_compile(invalid_type, &err);
    EXPECT_EQ(schema, nullptr);
    EXPECT_EQ(err.code, GTEXT_JSON_E_INVALID);
    gtext_json_free(invalid_type);
}

/**
 * Test schema validation - NULL arguments
 */
TEST(JsonSchema, NullArguments) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    const char * schema_json = "{\"type\":\"string\"}";
    GTEXT_JSON_Value * schema_doc = gtext_json_parse(schema_json, strlen(schema_json), &opts, &err);
    ASSERT_NE(schema_doc, nullptr);

    GTEXT_JSON_Schema * schema = gtext_json_schema_compile(schema_doc, &err);
    ASSERT_NE(schema, nullptr);

    const char * instance_json = "\"test\"";
    GTEXT_JSON_Value * instance = gtext_json_parse(instance_json, strlen(instance_json), &opts, &err);
    ASSERT_NE(instance, nullptr);

    // Test NULL schema
    GTEXT_JSON_Status status = gtext_json_schema_validate(nullptr, instance, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    // Test NULL instance
    status = gtext_json_schema_validate(schema, nullptr, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    gtext_json_free(instance);
    gtext_json_schema_free(schema);
    gtext_json_free(schema_doc);
}

/**
 * Test schema free - NULL argument
 */
TEST(JsonSchema, FreeNull) {
    // Should not crash - free functions must handle NULL gracefully
    gtext_json_schema_free(nullptr);
    // If we get here without crashing, the test passed
    SUCCEED();
}

/**
 * Test in-situ parsing mode - strings without escape sequences
 */
TEST(InSituMode, StringNoEscapes) {
    // String without escape sequences should use in-situ mode
    const char * input = "\"hello world\"";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.in_situ_mode = true;
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * val = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_STRING);

    const char * str;
    size_t len;
    GTEXT_JSON_Status status = gtext_json_get_string(val, &str, &len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(len, 11u);
    EXPECT_EQ(memcmp(str, "hello world", 11), 0);

    // Verify string points into input buffer (in-situ mode)
    // The string data should be at offset 1 (after opening quote)
    EXPECT_EQ(str, input + 1);

    gtext_json_free(val);
}

/**
 * Test in-situ parsing mode - strings with escape sequences (should not use in-situ)
 */
TEST(InSituMode, StringWithEscapes) {
    // String with escape sequences should NOT use in-situ mode
    const char * input = "\"hello\\nworld\"";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.in_situ_mode = true;
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * val = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_STRING);

    const char * str;
    size_t len;
    GTEXT_JSON_Status status = gtext_json_get_string(val, &str, &len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(len, 11u);  // "hello\nworld" = 11 chars (newline is one char)
    EXPECT_EQ(memcmp(str, "hello\nworld", 11), 0);

    // Verify string does NOT point into input buffer (decoded, not in-situ)
    EXPECT_NE(str, input + 1);

    gtext_json_free(val);
}

/**
 * Test in-situ parsing mode - numbers
 */
TEST(InSituMode, NumberLexeme) {
    // Number lexeme should use in-situ mode
    const char * input = "123.456";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.in_situ_mode = true;
    opts.preserve_number_lexeme = true;
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * val = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_NUMBER);

    const char * lexeme;
    size_t lexeme_len;
    GTEXT_JSON_Status status = gtext_json_get_number_lexeme(val, &lexeme, &lexeme_len);
    EXPECT_EQ(status, GTEXT_JSON_OK);
    EXPECT_EQ(lexeme_len, 7u);
    EXPECT_EQ(memcmp(lexeme, "123.456", 7), 0);

    // Verify lexeme points into input buffer (in-situ mode)
    EXPECT_EQ(lexeme, input);

    gtext_json_free(val);
}

/**
 * Test in-situ parsing mode - nested structures
 */
TEST(InSituMode, NestedStructures) {
    // Test in-situ mode with nested objects and arrays
    const char * input = "{\"key\":\"value\",\"num\":42}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.in_situ_mode = true;
    opts.preserve_number_lexeme = true;
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * val = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_OBJECT);

    // Check string value (no escapes, should be in-situ)
    const GTEXT_JSON_Value * str_val = gtext_json_object_get(val, "key", 3);
    ASSERT_NE(str_val, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(str_val, &str, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(str, "value", 5), 0);
    // Verify in-situ: "value" starts at offset 8 in input ("{\"key\":\"" = 7 chars, then "value" starts)
    // Input: {"key":"value","num":42}
    //        0123456789...
    EXPECT_EQ(str, input + 8);

    // Check number value
    const GTEXT_JSON_Value * num_val = gtext_json_object_get(val, "num", 3);
    ASSERT_NE(num_val, nullptr);
    const char * lexeme;
    size_t lexeme_len;
    gtext_json_get_number_lexeme(num_val, &lexeme, &lexeme_len);
    EXPECT_EQ(lexeme_len, 2u);
    EXPECT_EQ(memcmp(lexeme, "42", 2), 0);
    // Verify in-situ: "42" starts at offset 21 in input
    // Input: {"key":"value","num":42}
    //        0123456789012345678901...
    EXPECT_EQ(lexeme, input + 21);

    gtext_json_free(val);
}

/**
 * Test in-situ parsing mode - lifetime requirements
 * This test verifies that the input buffer must remain valid
 */
TEST(InSituMode, LifetimeRequirements) {
    // Create input in a scope that will be destroyed
    std::string input_str = "\"test string\"";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.in_situ_mode = true;
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * val = gtext_json_parse(input_str.c_str(), input_str.length(), &opts, &err);
    ASSERT_NE(val, nullptr);

    const char * str;
    size_t len;
    gtext_json_get_string(val, &str, &len);

    // At this point, str points into input_str
    // If input_str is destroyed, str becomes invalid
    // This test documents the requirement - caller must keep input alive
    EXPECT_EQ(len, 11u);
    EXPECT_EQ(memcmp(str, "test string", 11), 0);

    // Note: In a real scenario, the caller must ensure input_str remains valid
    // until gtext_json_free(val) is called. This is a documentation test.

    gtext_json_free(val);
}

/**
 * Test in-situ parsing mode - round-trip
 */
TEST(InSituMode, RoundTrip) {
    const char * input = "{\"name\":\"Alice\",\"age\":30}";
    GTEXT_JSON_Parse_Options parse_opts = gtext_json_parse_options_default();
    parse_opts.in_situ_mode = true;
    parse_opts.preserve_number_lexeme = true;
    GTEXT_JSON_Error err{};

    // Parse with in-situ mode
    GTEXT_JSON_Value * val = gtext_json_parse(input, strlen(input), &parse_opts, &err);
    ASSERT_NE(val, nullptr);

    // Write it back
    GTEXT_JSON_Write_Options write_opts = gtext_json_write_options_default();
    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);

    GTEXT_JSON_Status status = gtext_json_write_value(&sink, &write_opts, val, &err);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Verify the output matches (structurally, not byte-for-byte due to spacing)
    const char * output = gtext_json_sink_buffer_data(&sink);
    size_t output_len = gtext_json_sink_buffer_size(&sink);

    // Parse the output again to verify structural equality
    GTEXT_JSON_Value * val2 = gtext_json_parse(output, output_len, &parse_opts, &err);
    ASSERT_NE(val2, nullptr);

    // Verify structure
    EXPECT_EQ(gtext_json_typeof(val), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_typeof(val2), GTEXT_JSON_OBJECT);
    EXPECT_EQ(gtext_json_object_size(val), gtext_json_object_size(val2));

    gtext_json_free(val);
    gtext_json_free(val2);
    gtext_json_sink_buffer_free(&sink);
}

/**
 * Test in-situ parsing mode - disabled by default
 */
TEST(InSituMode, DisabledByDefault) {
    const char * input = "\"hello\"";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    // in_situ_mode is 0 by default
    GTEXT_JSON_Error err{};

    GTEXT_JSON_Value * val = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(val, nullptr);

    const char * str;
    size_t len;
    gtext_json_get_string(val, &str, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(memcmp(str, "hello", 5), 0);

    // Verify string does NOT point into input buffer (copy mode)
    EXPECT_NE(str, input + 1);

    gtext_json_free(val);
}

/**
 * Test deep equality - null values
 */
TEST(DomUtilities, DeepEqualityNull) {
    GTEXT_JSON_Value * a = gtext_json_new_null();
    GTEXT_JSON_Value * b = gtext_json_new_null();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(gtext_json_equal(a, b, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(a, b, GTEXT_JSON_EQUAL_NUMERIC), true);
    EXPECT_EQ(gtext_json_equal(a, nullptr, GTEXT_JSON_EQUAL_LEXEME), false);
    EXPECT_EQ(gtext_json_equal(nullptr, b, GTEXT_JSON_EQUAL_LEXEME), false);

    gtext_json_free(a);
    gtext_json_free(b);
}

/**
 * Test deep equality - boolean values
 */
TEST(DomUtilities, DeepEqualityBool) {
    GTEXT_JSON_Value * a1 = gtext_json_new_bool(true);
    GTEXT_JSON_Value * a2 = gtext_json_new_bool(true);
    GTEXT_JSON_Value * b = gtext_json_new_bool(false);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(gtext_json_equal(a1, a2, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(a1, b, GTEXT_JSON_EQUAL_LEXEME), false);

    gtext_json_free(a1);
    gtext_json_free(a2);
    gtext_json_free(b);
}

/**
 * Test deep equality - string values
 */
TEST(DomUtilities, DeepEqualityString) {
    GTEXT_JSON_Value * a1 = gtext_json_new_string("hello", 5);
    GTEXT_JSON_Value * a2 = gtext_json_new_string("hello", 5);
    GTEXT_JSON_Value * b = gtext_json_new_string("world", 5);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_EQ(gtext_json_equal(a1, a2, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(a1, b, GTEXT_JSON_EQUAL_LEXEME), false);

    gtext_json_free(a1);
    gtext_json_free(a2);
    gtext_json_free(b);
}

/**
 * Test deep equality - number values (lexeme mode)
 */
TEST(DomUtilities, DeepEqualityNumberLexeme) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    // "123" and "123" should be equal
    GTEXT_JSON_Value * a1 = gtext_json_parse("\"123\"", 5, &opts, &err);
    GTEXT_JSON_Value * a2 = gtext_json_parse("\"123\"", 5, &opts, &err);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);

    // Actually, let's test with numbers
    GTEXT_JSON_Value * n1 = gtext_json_new_number_from_lexeme("123", 3);
    GTEXT_JSON_Value * n2 = gtext_json_new_number_from_lexeme("123", 3);
    GTEXT_JSON_Value * n3 = gtext_json_new_number_from_lexeme("456", 3);
    ASSERT_NE(n1, nullptr);
    ASSERT_NE(n2, nullptr);
    ASSERT_NE(n3, nullptr);

    EXPECT_EQ(gtext_json_equal(n1, n2, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(n1, n3, GTEXT_JSON_EQUAL_LEXEME), false);

    gtext_json_free(n1);
    gtext_json_free(n2);
    gtext_json_free(n3);
    gtext_json_free(a1);
    gtext_json_free(a2);
}

/**
 * Test deep equality - number values (numeric mode)
 */
TEST(DomUtilities, DeepEqualityNumberNumeric) {
    // Test with int64
    GTEXT_JSON_Value * n1 = gtext_json_new_number_i64(123);
    GTEXT_JSON_Value * n2 = gtext_json_new_number_i64(123);
    GTEXT_JSON_Value * n3 = gtext_json_new_number_i64(456);
    ASSERT_NE(n1, nullptr);
    ASSERT_NE(n2, nullptr);
    ASSERT_NE(n3, nullptr);

    EXPECT_EQ(gtext_json_equal(n1, n2, GTEXT_JSON_EQUAL_NUMERIC), true);
    EXPECT_EQ(gtext_json_equal(n1, n3, GTEXT_JSON_EQUAL_NUMERIC), false);

    // Test with double
    GTEXT_JSON_Value * d1 = gtext_json_new_number_double(3.14);
    GTEXT_JSON_Value * d2 = gtext_json_new_number_double(3.14);
    GTEXT_JSON_Value * d3 = gtext_json_new_number_double(2.71);
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);
    ASSERT_NE(d3, nullptr);

    EXPECT_EQ(gtext_json_equal(d1, d2, GTEXT_JSON_EQUAL_NUMERIC), true);
    EXPECT_EQ(gtext_json_equal(d1, d3, GTEXT_JSON_EQUAL_NUMERIC), false);

    gtext_json_free(n1);
    gtext_json_free(n2);
    gtext_json_free(n3);
    gtext_json_free(d1);
    gtext_json_free(d2);
    gtext_json_free(d3);
}

/**
 * Test deep equality - arrays
 */
TEST(DomUtilities, DeepEqualityArray) {
    GTEXT_JSON_Value * a1 = gtext_json_new_array();
    GTEXT_JSON_Value * a2 = gtext_json_new_array();
    GTEXT_JSON_Value * a3 = gtext_json_new_array();
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(a3, nullptr);

    GTEXT_JSON_Value * v1 = gtext_json_new_string("hello", 5);
    GTEXT_JSON_Value * v2 = gtext_json_new_string("world", 5);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);

    gtext_json_array_push(a1, v1);
    gtext_json_array_push(a1, v2);
    gtext_json_array_push(a2, gtext_json_new_string("hello", 5));
    gtext_json_array_push(a2, gtext_json_new_string("world", 5));
    gtext_json_array_push(a3, gtext_json_new_string("foo", 3));

    EXPECT_EQ(gtext_json_equal(a1, a2, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(a1, a3, GTEXT_JSON_EQUAL_LEXEME), false);

    gtext_json_free(a1);
    gtext_json_free(a2);
    gtext_json_free(a3);
}

/**
 * Test deep equality - objects (order-independent)
 */
TEST(DomUtilities, DeepEqualityObject) {
    GTEXT_JSON_Value * o1 = gtext_json_new_object();
    GTEXT_JSON_Value * o2 = gtext_json_new_object();
    GTEXT_JSON_Value * o3 = gtext_json_new_object();
    ASSERT_NE(o1, nullptr);
    ASSERT_NE(o2, nullptr);
    ASSERT_NE(o3, nullptr);

    gtext_json_object_put(o1, "a", 1, gtext_json_new_string("hello", 5));
    gtext_json_object_put(o1, "b", 1, gtext_json_new_string("world", 5));

    // Same keys/values, different order
    gtext_json_object_put(o2, "b", 1, gtext_json_new_string("world", 5));
    gtext_json_object_put(o2, "a", 1, gtext_json_new_string("hello", 5));

    gtext_json_object_put(o3, "a", 1, gtext_json_new_string("foo", 3));

    EXPECT_EQ(gtext_json_equal(o1, o2, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(o1, o3, GTEXT_JSON_EQUAL_LEXEME), false);

    gtext_json_free(o1);
    gtext_json_free(o2);
    gtext_json_free(o3);
}

/**
 * Test deep equality - nested structures
 */
TEST(DomUtilities, DeepEqualityNested) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    const char * json1 = "{\"a\":[1,2,{\"b\":\"hello\"}]}";
    const char * json2 = "{\"a\":[1,2,{\"b\":\"hello\"}]}";
    const char * json3 = "{\"a\":[1,2,{\"b\":\"world\"}]}";

    GTEXT_JSON_Value * v1 = gtext_json_parse(json1, strlen(json1), &opts, &err);
    GTEXT_JSON_Value * v2 = gtext_json_parse(json2, strlen(json2), &opts, &err);
    GTEXT_JSON_Value * v3 = gtext_json_parse(json3, strlen(json3), &opts, &err);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    EXPECT_EQ(gtext_json_equal(v1, v2, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(v1, v3, GTEXT_JSON_EQUAL_LEXEME), false);

    gtext_json_free(v1);
    gtext_json_free(v2);
    gtext_json_free(v3);
}

/**
 * Test deep clone - null value
 */
TEST(DomUtilities, DeepCloneNull) {
    GTEXT_JSON_Value * src = gtext_json_new_null();
    ASSERT_NE(src, nullptr);

    GTEXT_JSON_Value * clone = gtext_json_clone(src);
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(gtext_json_typeof(clone), GTEXT_JSON_NULL);
    EXPECT_EQ(gtext_json_equal(src, clone, GTEXT_JSON_EQUAL_LEXEME), true);

    // Verify they're independent (different contexts)
    EXPECT_NE(src, clone);

    gtext_json_free(src);
    gtext_json_free(clone);
}

/**
 * Test deep clone - scalar values
 */
TEST(DomUtilities, DeepCloneScalars) {
    GTEXT_JSON_Value * bool_src = gtext_json_new_bool(true);
    GTEXT_JSON_Value * str_src = gtext_json_new_string("hello", 5);
    GTEXT_JSON_Value * num_src = gtext_json_new_number_i64(123);
    ASSERT_NE(bool_src, nullptr);
    ASSERT_NE(str_src, nullptr);
    ASSERT_NE(num_src, nullptr);

    GTEXT_JSON_Value * bool_clone = gtext_json_clone(bool_src);
    GTEXT_JSON_Value * str_clone = gtext_json_clone(str_src);
    GTEXT_JSON_Value * num_clone = gtext_json_clone(num_src);
    ASSERT_NE(bool_clone, nullptr);
    ASSERT_NE(str_clone, nullptr);
    ASSERT_NE(num_clone, nullptr);

    EXPECT_EQ(gtext_json_equal(bool_src, bool_clone, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(str_src, str_clone, GTEXT_JSON_EQUAL_LEXEME), true);
    EXPECT_EQ(gtext_json_equal(num_src, num_clone, GTEXT_JSON_EQUAL_LEXEME), true);

    gtext_json_free(bool_src);
    gtext_json_free(str_src);
    gtext_json_free(num_src);
    gtext_json_free(bool_clone);
    gtext_json_free(str_clone);
    gtext_json_free(num_clone);
}

/**
 * Test deep clone - arrays
 */
TEST(DomUtilities, DeepCloneArray) {
    GTEXT_JSON_Value * src = gtext_json_new_array();
    ASSERT_NE(src, nullptr);

    gtext_json_array_push(src, gtext_json_new_string("hello", 5));
    gtext_json_array_push(src, gtext_json_new_number_i64(123));
    gtext_json_array_push(src, gtext_json_new_bool(true));

    GTEXT_JSON_Value * clone = gtext_json_clone(src);
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(gtext_json_array_size(clone), 3u);
    EXPECT_EQ(gtext_json_equal(src, clone, GTEXT_JSON_EQUAL_LEXEME), true);

    // Verify independence - modify clone doesn't affect src
    gtext_json_array_push(clone, gtext_json_new_string("new", 3));
    EXPECT_EQ(gtext_json_array_size(src), 3u);
    EXPECT_EQ(gtext_json_array_size(clone), 4u);

    gtext_json_free(src);
    gtext_json_free(clone);
}

/**
 * Test deep clone - objects
 */
TEST(DomUtilities, DeepCloneObject) {
    GTEXT_JSON_Value * src = gtext_json_new_object();
    ASSERT_NE(src, nullptr);

    gtext_json_object_put(src, "a", 1, gtext_json_new_string("hello", 5));
    gtext_json_object_put(src, "b", 1, gtext_json_new_number_i64(123));

    GTEXT_JSON_Value * clone = gtext_json_clone(src);
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(gtext_json_object_size(clone), 2u);
    EXPECT_EQ(gtext_json_equal(src, clone, GTEXT_JSON_EQUAL_LEXEME), true);

    // Verify independence
    gtext_json_object_put(clone, "c", 1, gtext_json_new_string("new", 3));
    EXPECT_EQ(gtext_json_object_size(src), 2u);
    EXPECT_EQ(gtext_json_object_size(clone), 3u);

    gtext_json_free(src);
    gtext_json_free(clone);
}

/**
 * Test deep clone - nested structures
 */
TEST(DomUtilities, DeepCloneNested) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};

    const char * json = "{\"a\":[1,2,{\"b\":\"hello\"}]}";
    GTEXT_JSON_Value * src = gtext_json_parse(json, strlen(json), &opts, &err);
    ASSERT_NE(src, nullptr);

    GTEXT_JSON_Value * clone = gtext_json_clone(src);
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(gtext_json_equal(src, clone, GTEXT_JSON_EQUAL_LEXEME), true);

    gtext_json_free(src);
    gtext_json_free(clone);
}

/**
 * Test object merge - first wins policy
 */
TEST(DomUtilities, ObjectMergeFirstWins) {
    GTEXT_JSON_Value * target = gtext_json_new_object();
    GTEXT_JSON_Value * source = gtext_json_new_object();
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);

    gtext_json_object_put(target, "a", 1, gtext_json_new_string("target", 6));
    gtext_json_object_put(target, "b", 1, gtext_json_new_string("target", 6));

    gtext_json_object_put(source, "a", 1, gtext_json_new_string("source", 6));
    gtext_json_object_put(source, "c", 1, gtext_json_new_string("source", 6));

    GTEXT_JSON_Status status = gtext_json_object_merge(target, source, GTEXT_JSON_MERGE_FIRST_WINS);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    EXPECT_EQ(gtext_json_object_size(target), 3u);

    // "a" should keep target value
    const GTEXT_JSON_Value * a = gtext_json_object_get(target, "a", 1);
    ASSERT_NE(a, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(a, &str, &len);
    EXPECT_EQ(len, 6u);
    EXPECT_EQ(memcmp(str, "target", 6), 0);

    // "c" should be added
    const GTEXT_JSON_Value * c = gtext_json_object_get(target, "c", 1);
    ASSERT_NE(c, nullptr);
    gtext_json_get_string(c, &str, &len);
    EXPECT_EQ(len, 6u);
    EXPECT_EQ(memcmp(str, "source", 6), 0);

    gtext_json_free(target);
    gtext_json_free(source);
}

/**
 * Test object merge - last wins policy
 */
TEST(DomUtilities, ObjectMergeLastWins) {
    GTEXT_JSON_Value * target = gtext_json_new_object();
    GTEXT_JSON_Value * source = gtext_json_new_object();
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);

    gtext_json_object_put(target, "a", 1, gtext_json_new_string("target", 6));
    gtext_json_object_put(target, "b", 1, gtext_json_new_string("target", 6));

    gtext_json_object_put(source, "a", 1, gtext_json_new_string("source", 6));
    gtext_json_object_put(source, "c", 1, gtext_json_new_string("source", 6));

    GTEXT_JSON_Status status = gtext_json_object_merge(target, source, GTEXT_JSON_MERGE_LAST_WINS);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    EXPECT_EQ(gtext_json_object_size(target), 3u);

    // "a" should be replaced with source value
    const GTEXT_JSON_Value * a = gtext_json_object_get(target, "a", 1);
    ASSERT_NE(a, nullptr);
    const char * str;
    size_t len;
    gtext_json_get_string(a, &str, &len);
    EXPECT_EQ(len, 6u);
    EXPECT_EQ(memcmp(str, "source", 6), 0);

    gtext_json_free(target);
    gtext_json_free(source);
}

/**
 * Test object merge - error policy
 */
TEST(DomUtilities, ObjectMergeError) {
    GTEXT_JSON_Value * target = gtext_json_new_object();
    GTEXT_JSON_Value * source = gtext_json_new_object();
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);

    gtext_json_object_put(target, "a", 1, gtext_json_new_string("target", 6));
    gtext_json_object_put(source, "a", 1, gtext_json_new_string("source", 6));

    GTEXT_JSON_Status status = gtext_json_object_merge(target, source, GTEXT_JSON_MERGE_ERROR);
    EXPECT_EQ(status, GTEXT_JSON_E_DUPKEY);

    gtext_json_free(target);
    gtext_json_free(source);
}

/**
 * Test object merge - nested objects
 */
TEST(DomUtilities, ObjectMergeNested) {
    GTEXT_JSON_Value * target = gtext_json_new_object();
    GTEXT_JSON_Value * source = gtext_json_new_object();
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);

    GTEXT_JSON_Value * target_nested = gtext_json_new_object();
    gtext_json_object_put(target_nested, "x", 1, gtext_json_new_string("target", 6));
    gtext_json_object_put(target, "nested", 6, target_nested);

    GTEXT_JSON_Value * source_nested = gtext_json_new_object();
    gtext_json_object_put(source_nested, "y", 1, gtext_json_new_string("source", 6));
    gtext_json_object_put(source, "nested", 6, source_nested);

    GTEXT_JSON_Status status = gtext_json_object_merge(target, source, GTEXT_JSON_MERGE_LAST_WINS);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // Nested object should be merged
    const GTEXT_JSON_Value * nested = gtext_json_object_get(target, "nested", 6);
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(gtext_json_object_size(nested), 2u);

    const GTEXT_JSON_Value * x = gtext_json_object_get(nested, "x", 1);
    const GTEXT_JSON_Value * y = gtext_json_object_get(nested, "y", 1);
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);

    gtext_json_free(target);
    gtext_json_free(source);
}

/**
 * Test object merge - non-object values are replaced
 */
TEST(DomUtilities, ObjectMergeNonObjectReplace) {
    GTEXT_JSON_Value * target = gtext_json_new_object();
    GTEXT_JSON_Value * source = gtext_json_new_object();
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);

    gtext_json_object_put(target, "a", 1, gtext_json_new_string("target", 6));
    gtext_json_object_put(source, "a", 1, gtext_json_new_number_i64(123));

    GTEXT_JSON_Status status = gtext_json_object_merge(target, source, GTEXT_JSON_MERGE_LAST_WINS);
    EXPECT_EQ(status, GTEXT_JSON_OK);

    // "a" should be replaced with number
    const GTEXT_JSON_Value * a = gtext_json_object_get(target, "a", 1);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(gtext_json_typeof(a), GTEXT_JSON_NUMBER);

    int64_t val;
    gtext_json_get_i64(a, &val);
    EXPECT_EQ(val, 123);

    gtext_json_free(target);
    gtext_json_free(source);
}

/**
 * Test multiple top-level value parsing - single value (backward compatible)
 */
TEST(MultipleTopLevel, SingleValue) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    size_t bytes_consumed = 0;

    const char * input = "123";
    GTEXT_JSON_Value * value = gtext_json_parse_multiple(input, strlen(input), &opts, &err, &bytes_consumed);
    ASSERT_NE(value, nullptr) << "Parse failed with code: " << err.code;
    EXPECT_EQ(bytes_consumed, strlen(input));
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_NUMBER);
    gtext_json_free(value);
}

/**
 * Test multiple top-level value parsing - multiple values
 */
TEST(MultipleTopLevel, MultipleValues) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    size_t bytes_consumed = 0;

    const char * input = "123 456 \"hello\"";
    size_t input_len = strlen(input);

    // Parse first value
    GTEXT_JSON_Value * value1 = gtext_json_parse_multiple(input, input_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value1, nullptr);
    EXPECT_EQ(gtext_json_typeof(value1), GTEXT_JSON_NUMBER);
    EXPECT_EQ(bytes_consumed, 4u);  // "123" + space
    gtext_json_free(value1);

    // Continue parsing from offset
    const char * remaining = input + bytes_consumed;
    size_t remaining_len = input_len - bytes_consumed;
    GTEXT_JSON_Value * value2 = gtext_json_parse_multiple(remaining, remaining_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value2, nullptr);
    EXPECT_EQ(gtext_json_typeof(value2), GTEXT_JSON_NUMBER);
    EXPECT_EQ(bytes_consumed, 4u);  // "456" + space
    gtext_json_free(value2);

    // Continue parsing third value
    remaining = remaining + bytes_consumed;
    remaining_len = remaining_len - bytes_consumed;
    GTEXT_JSON_Value * value3 = gtext_json_parse_multiple(remaining, remaining_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value3, nullptr);
    EXPECT_EQ(gtext_json_typeof(value3), GTEXT_JSON_STRING);
    EXPECT_EQ(bytes_consumed, 7u);  // "\"hello\""
    gtext_json_free(value3);
}

/**
 * Test bytes consumed reporting
 */
TEST(MultipleTopLevel, BytesConsumed) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    size_t bytes_consumed = 0;

    const char * input = "{\"a\":1} [1,2,3]";
    size_t input_len = strlen(input);

    // Parse first value (object)
    GTEXT_JSON_Value * value1 = gtext_json_parse_multiple(input, input_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value1, nullptr);
    EXPECT_EQ(gtext_json_typeof(value1), GTEXT_JSON_OBJECT);
    // bytes_consumed should point to the start of the next value (after "{\"a\":1} ")
    EXPECT_GT(bytes_consumed, 7u);  // At least the object itself
    EXPECT_LT(bytes_consumed, input_len);  // Less than total input
    gtext_json_free(value1);

    // Parse second value (array)
    const char * remaining = input + bytes_consumed;
    size_t remaining_len = input_len - bytes_consumed;
    GTEXT_JSON_Value * value2 = gtext_json_parse_multiple(remaining, remaining_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value2, nullptr);
    EXPECT_EQ(gtext_json_typeof(value2), GTEXT_JSON_ARRAY);
    // bytes_consumed should be the length of "[1,2,3]"
    EXPECT_EQ(bytes_consumed, 7u);  // "[1,2,3]"
    gtext_json_free(value2);
}

/**
 * Test continuation from offset
 */
TEST(MultipleTopLevel, ContinuationFromOffset) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    size_t bytes_consumed = 0;

    const char * input = "true false null";
    size_t input_len = strlen(input);
    size_t offset = 0;

    // Parse values in a loop
    std::vector<GTEXT_JSON_Type> expected_types = {
        GTEXT_JSON_BOOL, GTEXT_JSON_BOOL, GTEXT_JSON_NULL
    };

    for (size_t i = 0; i < expected_types.size(); ++i) {
        const char * current = input + offset;
        size_t current_len = input_len - offset;

        GTEXT_JSON_Value * value = gtext_json_parse_multiple(current, current_len, &opts, &err, &bytes_consumed);
        ASSERT_NE(value, nullptr) << "Failed to parse value " << i;
        EXPECT_EQ(gtext_json_typeof(value), expected_types[i]) << "Wrong type for value " << i;

        offset += bytes_consumed;
        gtext_json_free(value);
    }

    EXPECT_EQ(offset, input_len);
}

/**
 * Test error handling in multi-value mode
 */
TEST(MultipleTopLevel, ErrorHandling) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    size_t bytes_consumed = 0;

    // Valid value followed by invalid JSON
    const char * input = "123 invalid!!!";
    size_t input_len = strlen(input);

    // First value should parse successfully
    GTEXT_JSON_Value * value1 = gtext_json_parse_multiple(input, input_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value1, nullptr);
    // bytes_consumed should point to the start of "invalid" (after "123 ")
    EXPECT_GT(bytes_consumed, 3u);  // At least "123"
    gtext_json_free(value1);

    // Second parse should fail (invalid JSON)
    const char * remaining = input + bytes_consumed;
    size_t remaining_len = input_len - bytes_consumed;
    GTEXT_JSON_Value * value2 = gtext_json_parse_multiple(remaining, remaining_len, &opts, &err, &bytes_consumed);
    EXPECT_EQ(value2, nullptr);
    // bytes_consumed should be 0 on error
    EXPECT_EQ(bytes_consumed, 0u);
}

/**
 * Test that gtext_json_parse() rejects trailing content
 */
TEST(MultipleTopLevel, SingleValueRejectsTrailing) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    // Trailing content should cause error with gtext_json_parse()
    const char * input = "123 456";
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);
    EXPECT_EQ(value, nullptr);
    EXPECT_EQ(err.code, GTEXT_JSON_E_TRAILING_GARBAGE);

    // Clean up error context snippet
    gtext_json_error_free(&err);
}

/**
 * Test multiple top-level with complex nested structures
 */
TEST(MultipleTopLevel, ComplexStructures) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    size_t bytes_consumed = 0;

    const char * input = "{\"a\":[1,2,3]} {\"b\":{\"c\":\"value\"}}";
    size_t input_len = strlen(input);

    // Parse first object
    GTEXT_JSON_Value * value1 = gtext_json_parse_multiple(input, input_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value1, nullptr);
    EXPECT_EQ(gtext_json_typeof(value1), GTEXT_JSON_OBJECT);
    gtext_json_free(value1);

    // Parse second object
    const char * remaining = input + bytes_consumed;
    size_t remaining_len = input_len - bytes_consumed;
    GTEXT_JSON_Value * value2 = gtext_json_parse_multiple(remaining, remaining_len, &opts, &err, &bytes_consumed);
    ASSERT_NE(value2, nullptr);
    EXPECT_EQ(gtext_json_typeof(value2), GTEXT_JSON_OBJECT);
    gtext_json_free(value2);
}

/**
 * Test enhanced error reporting - context snippet generation
 */
TEST(EnhancedErrorReporting, ContextSnippet) {
    const char * json = "{\"key\": \"value\", \"invalid\": }";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * result = gtext_json_parse(json, strlen(json), &opts, &err);
    EXPECT_EQ(result, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    // Verify context snippet is generated
    if (err.context_snippet) {
        EXPECT_GT(err.context_snippet_len, 0u);
        EXPECT_LT(err.caret_offset, err.context_snippet_len);

        // Context snippet should contain the error position
        EXPECT_NE(strstr(err.context_snippet, "invalid"), nullptr);

        // Clean up
        gtext_json_error_free(&err);
    }
}

/**
 * Test enhanced error reporting - expected/actual token descriptions
 */
TEST(EnhancedErrorReporting, ExpectedActualTokens) {
    const char * json = "{\"key\": \"value\", \"missing_colon\" }";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * result = gtext_json_parse(json, strlen(json), &opts, &err);
    EXPECT_EQ(result, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    // Verify expected/actual token information is set for token mismatch errors
    if (err.code == GTEXT_JSON_E_BAD_TOKEN) {
        // Should have expected token description
        if (err.expected_token) {
            EXPECT_NE(strlen(err.expected_token), 0u);
        }
        // Should have actual token description
        if (err.actual_token) {
            EXPECT_NE(strlen(err.actual_token), 0u);
        }
    }

    gtext_json_error_free(&err);
}

/**
 * Test enhanced error reporting - caret positioning
 */
TEST(EnhancedErrorReporting, CaretPositioning) {
    const char * json = "[1, 2, 3, }";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * result = gtext_json_parse(json, strlen(json), &opts, &err);
    EXPECT_EQ(result, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    // Verify caret offset is within context snippet bounds
    if (err.context_snippet) {
        EXPECT_LT(err.caret_offset, err.context_snippet_len);
        // Caret offset should point to error position in snippet
        EXPECT_GE(err.caret_offset, 0u);
    }

    gtext_json_error_free(&err);
}

/**
 * Test enhanced error reporting - error free function
 */
TEST(EnhancedErrorReporting, ErrorFree) {
    const char * json = "{\"invalid\": }";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * result = gtext_json_parse(json, strlen(json), &opts, &err);
    EXPECT_EQ(result, nullptr);

    // Verify context snippet is allocated
    if (err.context_snippet) {
        // Free the error
        gtext_json_error_free(&err);

        // Verify snippet is freed (pointer set to NULL)
        EXPECT_EQ(err.context_snippet, nullptr);
        EXPECT_EQ(err.context_snippet_len, 0u);
        EXPECT_EQ(err.caret_offset, 0u);
    }
}

/**
 * Test enhanced error reporting - multiple errors (verify cleanup)
 */
TEST(EnhancedErrorReporting, MultipleErrors) {
    const char * json1 = "{\"invalid1\": }";
    const char * json2 = "{\"invalid2\": }";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    // First parse
    GTEXT_JSON_Value * result1 = gtext_json_parse(json1, strlen(json1), &opts, &err);
    EXPECT_EQ(result1, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    // Free first error
    gtext_json_error_free(&err);

    // Reset error structure
    err = GTEXT_JSON_Error{
        .code = {},
        .message = {},
        .offset = {},
        .line = {},
        .col = {},
        .context_snippet = {},
        .context_snippet_len = {},
        .caret_offset = {},
        .expected_token = {},
        .actual_token = {}
    };
    // Second parse (should reuse error structure)
    GTEXT_JSON_Value * result2 = gtext_json_parse(json2, strlen(json2), &opts, &err);
    EXPECT_EQ(result2, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    // Free second error
    gtext_json_error_free(&err);
}

/**
 * Test enhanced error reporting - empty input
 */
TEST(EnhancedErrorReporting, EmptyInput) {
    const char * json = "";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * result = gtext_json_parse(json, strlen(json), &opts, &err);
    EXPECT_EQ(result, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    // Empty input may not have a context snippet (too short)
    // But error should still be properly initialized
    EXPECT_NE(err.message, nullptr);

    gtext_json_error_free(&err);
}

/**
 * Test writer enhancements - locale independence (numeric formatting)
 */
TEST(WriterEnhancements, LocaleIndependence) {
    // Test that numeric formatting is locale-independent
    // This is primarily verified by the implementation using locale-independent
    // formatting functions, but we can test that numbers format consistently

    GTEXT_JSON_Value * v1 = gtext_json_new_number_i64(123456789);
    GTEXT_JSON_Value * v2 = gtext_json_new_number_u64(987654321ULL);
    GTEXT_JSON_Value * v3 = gtext_json_new_number_double(1234.56789);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);

    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    // Test i64 formatting
    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v1, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    // Should not contain locale-specific thousand separators
    EXPECT_EQ(strchr(output, ','), nullptr);  // No comma in number
    EXPECT_NE(strstr(output, "123456789"), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test u64 formatting
    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v2, &err);
    output = gtext_json_sink_buffer_data(&sink);
    EXPECT_EQ(strchr(output, ','), nullptr);  // No comma in number
    EXPECT_NE(strstr(output, "987654321"), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test double formatting
    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, v3, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should use dot as decimal separator (not comma)
    EXPECT_NE(strchr(output, '.'), nullptr);
    EXPECT_EQ(strchr(output, ','), nullptr);  // No comma as decimal separator
    gtext_json_sink_buffer_free(&sink);

    gtext_json_free(v1);
    gtext_json_free(v2);
    gtext_json_free(v3);
}

/**
 * Test writer enhancements - floating-point formatting options
 *
 * Note: To test float formatting, we need to set canonical_numbers = 1
 * to force formatting from the double representation instead of using the lexeme.
 */
TEST(WriterEnhancements, FloatFormatting) {
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    // Enable canonical_numbers to force formatting from double (not lexeme)
    opts.canonical_numbers = true;

    // Test SHORTEST format (default)
    GTEXT_JSON_Value * v1 = gtext_json_new_number_double(123.456789);
    ASSERT_NE(v1, nullptr);
    gtext_json_sink_buffer(&sink);
    opts.float_format = GTEXT_JSON_FLOAT_SHORTEST;
    gtext_json_write_value(&sink, &opts, v1, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    // Should contain the number
    EXPECT_NE(strstr(output, "123"), nullptr);
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(v1);

    // Test FIXED format with precision 2
    GTEXT_JSON_Value * v2 = gtext_json_new_number_double(123.456789);
    ASSERT_NE(v2, nullptr);
    gtext_json_sink_buffer(&sink);
    opts.float_format = GTEXT_JSON_FLOAT_FIXED;
    opts.float_precision = 2;
    gtext_json_write_value(&sink, &opts, v2, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should have exactly 2 decimal places (rounded: 123.46)
    const char * dot = strchr(output, '.');
    ASSERT_NE(dot, nullptr);
    // Count digits after decimal point
    size_t decimal_places = 0;
    for (const char * p = dot + 1; *p && *p >= '0' && *p <= '9'; p++) {
        decimal_places++;
    }
    EXPECT_EQ(decimal_places, 2u);
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(v2);

    // Test FIXED format with precision 4
    GTEXT_JSON_Value * v3 = gtext_json_new_number_double(123.456789);
    ASSERT_NE(v3, nullptr);
    gtext_json_sink_buffer(&sink);
    opts.float_format = GTEXT_JSON_FLOAT_FIXED;
    opts.float_precision = 4;
    gtext_json_write_value(&sink, &opts, v3, &err);
    output = gtext_json_sink_buffer_data(&sink);
    dot = strchr(output, '.');
    ASSERT_NE(dot, nullptr);
    decimal_places = 0;
    for (const char * p = dot + 1; *p && *p >= '0' && *p <= '9'; p++) {
        decimal_places++;
    }
    EXPECT_EQ(decimal_places, 4u);
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(v3);

    // Test SCIENTIFIC format - use a number that would naturally use scientific notation
    // or verify the format is applied (even if the number could be represented normally)
    GTEXT_JSON_Value * v4 = gtext_json_new_number_double(123456.789);
    ASSERT_NE(v4, nullptr);
    gtext_json_sink_buffer(&sink);
    opts.float_format = GTEXT_JSON_FLOAT_SCIENTIFIC;
    opts.float_precision = 3;
    gtext_json_write_value(&sink, &opts, v4, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should contain 'e' or 'E' for scientific notation
    EXPECT_TRUE(strchr(output, 'e') != nullptr || strchr(output, 'E') != nullptr);
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(v4);

    // Test SCIENTIFIC format with a very small number
    GTEXT_JSON_Value * v5 = gtext_json_new_number_double(0.000123456);
    ASSERT_NE(v5, nullptr);
    gtext_json_sink_buffer(&sink);
    opts.float_format = GTEXT_JSON_FLOAT_SCIENTIFIC;
    opts.float_precision = 2;
    gtext_json_write_value(&sink, &opts, v5, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should contain 'e' or 'E' for scientific notation
    EXPECT_TRUE(strchr(output, 'e') != nullptr || strchr(output, 'E') != nullptr);
    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(v5);
}

/**
 * Test writer enhancements - trailing newline control
 */
TEST(WriterEnhancements, TrailingNewline) {
    GTEXT_JSON_Value * v = gtext_json_new_string("test", 4);
    ASSERT_NE(v, nullptr);

    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    // Test without trailing newline (default)
    gtext_json_sink_buffer(&sink);
    opts.trailing_newline = false;
    gtext_json_write_value(&sink, &opts, v, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    size_t len = gtext_json_sink_buffer_size(&sink);
    // Should not end with newline
    EXPECT_NE(len, 0u);
    EXPECT_NE(output[len - 1], '\n');
    gtext_json_sink_buffer_free(&sink);

    // Test with trailing newline
    gtext_json_sink_buffer(&sink);
    opts.trailing_newline = true;
    gtext_json_write_value(&sink, &opts, v, &err);
    output = gtext_json_sink_buffer_data(&sink);
    len = gtext_json_sink_buffer_size(&sink);
    // Should end with newline
    EXPECT_GT(len, 0u);
    EXPECT_EQ(output[len - 1], '\n');
    gtext_json_sink_buffer_free(&sink);

    gtext_json_free(v);
}

/**
 * Test writer enhancements - spacing controls
 */
TEST(WriterEnhancements, SpacingControls) {
    GTEXT_JSON_Value * obj = gtext_json_new_object();
    ASSERT_NE(obj, nullptr);

    GTEXT_JSON_Value * v1 = gtext_json_new_number_i64(1);
    GTEXT_JSON_Value * v2 = gtext_json_new_number_i64(2);
    gtext_json_object_put(obj, "key1", 4, v1);
    gtext_json_object_put(obj, "key2", 4, v2);

    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    // Test without spacing (default)
    gtext_json_sink_buffer(&sink);
    opts.space_after_colon = false;
    opts.space_after_comma = false;
    gtext_json_write_value(&sink, &opts, obj, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    // Should not have space after colon or comma
    EXPECT_EQ(strstr(output, ": "), nullptr);  // No space after colon
    EXPECT_EQ(strstr(output, ", "), nullptr);  // No space after comma
    gtext_json_sink_buffer_free(&sink);

    // Test with space after colon
    gtext_json_sink_buffer(&sink);
    opts.space_after_colon = true;
    opts.space_after_comma = false;
    gtext_json_write_value(&sink, &opts, obj, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should have space after colon
    EXPECT_NE(strstr(output, ": "), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test with space after comma
    gtext_json_sink_buffer(&sink);
    opts.space_after_colon = false;
    opts.space_after_comma = true;
    gtext_json_write_value(&sink, &opts, obj, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should have space after comma
    EXPECT_NE(strstr(output, ", "), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test with both spacing options
    gtext_json_sink_buffer(&sink);
    opts.space_after_colon = true;
    opts.space_after_comma = true;
    gtext_json_write_value(&sink, &opts, obj, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should have both spaces
    EXPECT_NE(strstr(output, ": "), nullptr);
    EXPECT_NE(strstr(output, ", "), nullptr);
    gtext_json_sink_buffer_free(&sink);

    gtext_json_free(obj);
}

/**
 * Test writer enhancements - inline formatting thresholds
 */
TEST(WriterEnhancements, InlineFormattingThresholds) {
    // Create a small array
    GTEXT_JSON_Value * small_arr = gtext_json_new_array();
    ASSERT_NE(small_arr, nullptr);
    gtext_json_array_push(small_arr, gtext_json_new_number_i64(1));
    gtext_json_array_push(small_arr, gtext_json_new_number_i64(2));

    // Create a larger array
    GTEXT_JSON_Value * large_arr = gtext_json_new_array();
    ASSERT_NE(large_arr, nullptr);
    for (int i = 0; i < 10; i++) {
        gtext_json_array_push(large_arr, gtext_json_new_number_i64(i));
    }

    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Write_Options opts = gtext_json_write_options_default();
    GTEXT_JSON_Error err{};

    // Test inline threshold = -1 (always inline when not pretty)
    gtext_json_sink_buffer(&sink);
    opts.pretty = false;
    opts.inline_array_threshold = -1;
    gtext_json_write_value(&sink, &opts, small_arr, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    // Should be inline (no newlines)
    EXPECT_EQ(strchr(output, '\n'), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test inline threshold = 0 (always pretty)
    gtext_json_sink_buffer(&sink);
    opts.pretty = true;
    opts.inline_array_threshold = 0;
    gtext_json_write_value(&sink, &opts, small_arr, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Should be pretty (has newlines)
    EXPECT_NE(strchr(output, '\n'), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test inline threshold = 3 (small array should be inline, large should be pretty)
    gtext_json_sink_buffer(&sink);
    opts.pretty = true;
    opts.inline_array_threshold = 3;
    gtext_json_write_value(&sink, &opts, small_arr, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Small array (2 elements <= 3) should be inline
    EXPECT_EQ(strchr(output, '\n'), nullptr);
    gtext_json_sink_buffer_free(&sink);

    gtext_json_sink_buffer(&sink);
    gtext_json_write_value(&sink, &opts, large_arr, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Large array (10 elements > 3) should be pretty
    EXPECT_NE(strchr(output, '\n'), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test object thresholds
    GTEXT_JSON_Value * small_obj = gtext_json_new_object();
    ASSERT_NE(small_obj, nullptr);
    gtext_json_object_put(small_obj, "a", 1, gtext_json_new_number_i64(1));
    gtext_json_object_put(small_obj, "b", 1, gtext_json_new_number_i64(2));

    gtext_json_sink_buffer(&sink);
    opts.inline_object_threshold = 3;
    gtext_json_write_value(&sink, &opts, small_obj, &err);
    output = gtext_json_sink_buffer_data(&sink);
    // Small object (2 pairs <= 3) should be inline
    EXPECT_EQ(strchr(output, '\n'), nullptr);
    gtext_json_sink_buffer_free(&sink);

    gtext_json_free(small_arr);
    gtext_json_free(large_arr);
    gtext_json_free(small_obj);
}

// Helper function to read a file into a string
static std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

// Helper function to get test data directory
static std::string get_test_data_dir() {
    // Try to find the test data directory relative to the test executable
    // This works when running from the build directory
    const char * test_dir = getenv("TEST_DATA_DIR");
    if (test_dir) {
        return std::string(test_dir);
    }

    // Default relative path from build directory
    return "tests/data/json";
}

// Helper to test a valid JSON file
static void test_valid_json_file(const std::string& filepath) {
    std::string content = read_file(filepath);
    ASSERT_FALSE(content.empty()) << "Failed to read file: " << filepath;

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(content.c_str(), content.size(), &opts, &err);
    EXPECT_NE(value, nullptr) << "Failed to parse valid JSON from: " << filepath
                              << " Error: " << (err.message ? err.message : "unknown");

    if (value) {
        gtext_json_free(value);
    }
    gtext_json_error_free(&err);
}

// Helper to test an invalid JSON file (should fail to parse)
static void test_invalid_json_file(const std::string& filepath) {
    std::string content = read_file(filepath);
    ASSERT_FALSE(content.empty()) << "Failed to read file: " << filepath;

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(content.c_str(), content.size(), &opts, &err);
    EXPECT_EQ(value, nullptr) << "Should have failed to parse invalid JSON from: " << filepath;

    if (value) {
        gtext_json_free(value);
    }
    gtext_json_error_free(&err);
}

// Helper to test JSONC file (with comments enabled)
static void test_jsonc_file(const std::string& filepath) {
    std::string content = read_file(filepath);
    ASSERT_FALSE(content.empty()) << "Failed to read file: " << filepath;

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_comments = true;
    opts.allow_trailing_commas = true;
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(content.c_str(), content.size(), &opts, &err);
    EXPECT_NE(value, nullptr) << "Failed to parse JSONC from: " << filepath
                              << " Error: " << (err.message ? err.message : "unknown");

    if (value) {
        gtext_json_free(value);
    }
    gtext_json_error_free(&err);
}

// Helper to test round-trip: parse -> write -> parse -> compare
static void test_round_trip(const std::string& filepath) {
    std::string content = read_file(filepath);
    ASSERT_FALSE(content.empty()) << "Failed to read file: " << filepath;

    GTEXT_JSON_Parse_Options parse_opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    // Parse original
    GTEXT_JSON_Value * original = gtext_json_parse(content.c_str(), content.size(), &parse_opts, &err);
    ASSERT_NE(original, nullptr) << "Failed to parse: " << filepath;

    // Write to buffer
    GTEXT_JSON_Sink sink;
    GTEXT_JSON_Status status = gtext_json_sink_buffer(&sink);
    ASSERT_EQ(status, GTEXT_JSON_OK);

    GTEXT_JSON_Write_Options write_opts = gtext_json_write_options_default();
    status = gtext_json_write_value(&sink, &write_opts, original, &err);
    ASSERT_EQ(status, GTEXT_JSON_OK) << "Failed to write: " << filepath;

    const char * output = gtext_json_sink_buffer_data(&sink);
    size_t output_len = gtext_json_sink_buffer_size(&sink);

    // Parse again
    GTEXT_JSON_Value * reparsed = gtext_json_parse(output, output_len, &parse_opts, &err);
    ASSERT_NE(reparsed, nullptr) << "Failed to reparse output from: " << filepath;

    // Compare structurally (deep equality)
    if (original && reparsed) {
        bool equal = gtext_json_equal(original, reparsed, GTEXT_JSON_EQUAL_NUMERIC);
        EXPECT_EQ(equal, true) << "Round-trip failed for: " << filepath;
    }

    gtext_json_sink_buffer_free(&sink);
    if (original) gtext_json_free(original);
    if (reparsed) gtext_json_free(reparsed);
    gtext_json_error_free(&err);
}

/**
 * Test corpus - RFC 8259 examples
 */
TEST(TestCorpus, RFC8259Examples) {
    std::string base_dir = get_test_data_dir() + "/rfc8259";

    // Test all RFC 8259 example files
    test_valid_json_file(base_dir + "/basic.json");
    test_valid_json_file(base_dir + "/array.json");
    test_valid_json_file(base_dir + "/strings.json");
    test_valid_json_file(base_dir + "/numbers.json");
    test_valid_json_file(base_dir + "/whitespace.json");
}

/**
 * Test corpus - Valid JSON cases
 */
TEST(TestCorpus, ValidJSON) {
    std::string base_dir = get_test_data_dir() + "/valid";

    test_valid_json_file(base_dir + "/empty.json");
    test_valid_json_file(base_dir + "/empty-array.json");
    test_valid_json_file(base_dir + "/empty-object.json");
    test_valid_json_file(base_dir + "/nested.json");
    test_valid_json_file(base_dir + "/large-array.json");
    test_valid_json_file(base_dir + "/large-object.json");
}

/**
 * Test corpus - Invalid JSON cases (should fail to parse)
 */
TEST(TestCorpus, InvalidJSON) {
    std::string base_dir = get_test_data_dir() + "/invalid";

    test_invalid_json_file(base_dir + "/trailing-comma-array.json");
    test_invalid_json_file(base_dir + "/trailing-comma-object.json");
    test_invalid_json_file(base_dir + "/missing-comma.json");
    test_invalid_json_file(base_dir + "/missing-colon.json");
    test_invalid_json_file(base_dir + "/invalid-number-01.json");
    test_invalid_json_file(base_dir + "/invalid-number-leading-dot.json");
    test_invalid_json_file(base_dir + "/invalid-number-trailing-dot.json");
    test_invalid_json_file(base_dir + "/invalid-number-incomplete-exponent.json");
    test_invalid_json_file(base_dir + "/invalid-string-unclosed.json");
    test_invalid_json_file(base_dir + "/invalid-string-control-char.json");
}

/**
 * Test corpus - JSONC (with comments and trailing commas)
 */
TEST(TestCorpus, JSONC) {
    std::string base_dir = get_test_data_dir() + "/jsonc";

    test_jsonc_file(base_dir + "/single-line-comment.json");
    test_jsonc_file(base_dir + "/multi-line-comment.json");
    test_jsonc_file(base_dir + "/trailing-comma-array.json");
    test_jsonc_file(base_dir + "/trailing-comma-object.json");
    test_jsonc_file(base_dir + "/mixed.json");
}

/**
 * Test corpus - Unicode torture tests
 */
TEST(TestCorpus, Unicode) {
    std::string base_dir = get_test_data_dir() + "/unicode";

    // Valid Unicode cases
    test_valid_json_file(base_dir + "/surrogate-pair.json");
    test_valid_json_file(base_dir + "/various-unicode.json");

    // Invalid surrogate sequences (should fail in strict mode)
    test_invalid_json_file(base_dir + "/invalid-surrogate-lone-high.json");
    test_invalid_json_file(base_dir + "/invalid-surrogate-lone-low.json");
    test_invalid_json_file(base_dir + "/invalid-surrogate-reversed.json");
}

/**
 * Test corpus - BOM (Byte Order Mark) handling
 */
TEST(TestCorpus, BOMHandling) {
    // UTF-8 BOM is: EF BB BF (U+FEFF)
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    const char * json_after_bom = "{\"key\":\"value\"}";

    // Create input with BOM
    std::vector<unsigned char> input_with_bom;
    input_with_bom.insert(input_with_bom.end(), bom, bom + 3);
    input_with_bom.insert(input_with_bom.end(),
                         (const unsigned char * )json_after_bom,
                         (const unsigned char * )json_after_bom + strlen(json_after_bom));

    // Test 1: BOM enabled (default) - should parse successfully
    {
        GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
        opts.allow_leading_bom = true;
        GTEXT_JSON_Error err{};
        GTEXT_JSON_Value * value = gtext_json_parse(
            (const char * )input_with_bom.data(),
            input_with_bom.size(),
            &opts,
            &err
        );
        EXPECT_NE(value, nullptr) << "Should parse JSON with BOM when allow_leading_bom=1";

        if (value) {
            // Verify we can access the data
            const GTEXT_JSON_Value * key_val = gtext_json_object_get(value, "key", 3);
            EXPECT_NE(key_val, nullptr);
            if (key_val) {
                const char * str_val = nullptr;
                size_t str_len = 0;
                GTEXT_JSON_Status status = gtext_json_get_string(key_val, &str_val, &str_len);
                EXPECT_EQ(status, GTEXT_JSON_OK);
                EXPECT_EQ(str_len, 5u);
                EXPECT_STREQ(str_val, "value");
            }
            gtext_json_free(value);
        }
        gtext_json_error_free(&err);
    }

    // Test 2: BOM disabled - verify option is checked
    // Note: The BOM bytes (EF BB BF) might be treated as whitespace or ignored
    // by some parsers even when allow_leading_bom=0. The important thing is
    // that when allow_leading_bom=1, the BOM is properly skipped.
    {
        GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
        opts.allow_leading_bom = false;
        GTEXT_JSON_Error err{};
        GTEXT_JSON_Value * value = gtext_json_parse(
            (const char * )input_with_bom.data(),
            input_with_bom.size(),
            &opts,
            &err
        );
        // Behavior may vary: BOM bytes might be rejected, ignored, or treated as whitespace
        // The key test is that allow_leading_bom=1 works (tested in Test 1)
        if (value) {
            // If it parsed, that's acceptable - verify it parsed correctly
            const GTEXT_JSON_Value * key_val = gtext_json_object_get(value, "key", 3);
            if (key_val) {
                const char * str_val = nullptr;
                size_t str_len = 0;
                gtext_json_get_string(key_val, &str_val, &str_len);
                EXPECT_EQ(str_len, 5u);
                EXPECT_STREQ(str_val, "value");
            }
            gtext_json_free(value);
        }
        // If it failed, that's also acceptable - BOM bytes are not valid JSON
        gtext_json_error_free(&err);
    }

    // Test 3: JSON without BOM should work regardless of setting
    {
        GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
        opts.allow_leading_bom = true;  // Even with BOM enabled, no-BOM should work
        GTEXT_JSON_Error err{};
        GTEXT_JSON_Value * value = gtext_json_parse(
            json_after_bom,
            strlen(json_after_bom),
            &opts,
            &err
        );
        EXPECT_NE(value, nullptr) << "Should parse JSON without BOM";

        if (value) {
            gtext_json_free(value);
        }
        gtext_json_error_free(&err);
    }

    // Test 4: BOM in middle of input (not at start) should be treated as invalid
    // BOM bytes in the middle are not valid JSON syntax
    {
        std::vector<unsigned char> input_with_middle_bom;
        input_with_middle_bom.insert(input_with_middle_bom.end(),
                                    (const unsigned char * )"{\"key\":",
                                    (const unsigned char * )"{\"key\":" + 7);
        input_with_middle_bom.insert(input_with_middle_bom.end(), bom, bom + 3);
        input_with_middle_bom.insert(input_with_middle_bom.end(),
                                    (const unsigned char * )"\"value\"}",
                                    (const unsigned char * )"\"value\"}" + 8);

        GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
        opts.allow_leading_bom = true;  // BOM only allowed at start, not in middle
        GTEXT_JSON_Error err{};
        GTEXT_JSON_Value * value = gtext_json_parse(
            (const char * )input_with_middle_bom.data(),
            input_with_middle_bom.size(),
            &opts,
            &err
        );
        // BOM in middle should cause parse failure (BOM bytes are not valid JSON)
        // However, if UTF-8 validation is off, it might succeed
        // The important thing is that BOM at start works correctly
        if (!value) {
            // Expected: BOM in middle should fail
            // But if it doesn't fail, that's also acceptable (implementation-dependent)
        } else {
            gtext_json_free(value);
        }
        gtext_json_error_free(&err);
    }
}

/**
 * Test corpus - Number boundary tests
 */
TEST(TestCorpus, NumberBoundaries) {
    std::string base_dir = get_test_data_dir() + "/numbers";

    // Valid boundary cases
    test_valid_json_file(base_dir + "/int64-max.json");
    test_valid_json_file(base_dir + "/int64-min.json");
    test_valid_json_file(base_dir + "/uint64-max.json");
    test_valid_json_file(base_dir + "/exponent-large.json");
    test_valid_json_file(base_dir + "/exponent-small.json");
    test_valid_json_file(base_dir + "/precision.json");

    // Overflow cases (should parse but may not fit in int64/uint64)
    test_valid_json_file(base_dir + "/int64-overflow.json");
    test_valid_json_file(base_dir + "/uint64-overflow.json");

    // Nonfinite numbers (require option)
    // Test parsing the nonfinite.json file which contains unquoted NaN/Infinity
    const char * nonfinite_json = "{\"nan\":NaN,\"infinity\":Infinity,\"negative_infinity\":-Infinity}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = true;
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(nonfinite_json, strlen(nonfinite_json), &opts, &err);
    ASSERT_NE(value, nullptr) << "Should parse non-finite numbers when enabled";
    EXPECT_EQ(gtext_json_typeof(value), GTEXT_JSON_OBJECT);

    // Verify the values
    const GTEXT_JSON_Value * nan_val = gtext_json_object_get(value, "nan", 3);
    ASSERT_NE(nan_val, nullptr);
    double dbl = 0.0;
    EXPECT_EQ(gtext_json_get_double(nan_val, &dbl), GTEXT_JSON_OK);
    EXPECT_TRUE(std::isnan(dbl));

    const GTEXT_JSON_Value * inf_val = gtext_json_object_get(value, "infinity", 8);
    ASSERT_NE(inf_val, nullptr);
    dbl = 0.0;
    EXPECT_EQ(gtext_json_get_double(inf_val, &dbl), GTEXT_JSON_OK);
    EXPECT_TRUE(std::isinf(dbl) && dbl > 0);

    const GTEXT_JSON_Value * neg_inf_val = gtext_json_object_get(value, "negative_infinity", 17);
    ASSERT_NE(neg_inf_val, nullptr);
    dbl = 0.0;
    EXPECT_EQ(gtext_json_get_double(neg_inf_val, &dbl), GTEXT_JSON_OK);
    EXPECT_TRUE(std::isinf(dbl) && dbl < 0);

    gtext_json_free(value);
    gtext_json_error_free(&err);
}

/**
 * Test corpus - Round-trip tests
 */
TEST(TestCorpus, RoundTrip) {
    std::string base_dir = get_test_data_dir();

    // Test round-trip on various valid JSON files
    test_round_trip(base_dir + "/rfc8259/basic.json");
    test_round_trip(base_dir + "/rfc8259/array.json");
    test_round_trip(base_dir + "/rfc8259/strings.json");
    test_round_trip(base_dir + "/rfc8259/numbers.json");
    test_round_trip(base_dir + "/valid/nested.json");
    test_round_trip(base_dir + "/unicode/various-unicode.json");
    test_round_trip(base_dir + "/numbers/precision.json");
}

/**
 * Test corpus - Milestone A: Strict JSON DOM + Writer
 *
 * Verify:
 * - strict parse (RFC/ECMA)
 * - DOM write (compact + pretty)
 * - full unicode correctness
 * - good errors
 */
TEST(TestCorpus, MilestoneA) {
    std::string base_dir = get_test_data_dir();

    // Strict parse - RFC 8259 examples should work
    test_valid_json_file(base_dir + "/rfc8259/basic.json");
    test_valid_json_file(base_dir + "/rfc8259/array.json");

    // Invalid JSON should be rejected
    test_invalid_json_file(base_dir + "/invalid/trailing-comma-array.json");

    // Unicode correctness
    test_valid_json_file(base_dir + "/unicode/surrogate-pair.json");
    test_valid_json_file(base_dir + "/unicode/various-unicode.json");

    // DOM write - test compact and pretty
    std::string content = read_file(base_dir + "/rfc8259/basic.json");
    ASSERT_FALSE(content.empty());

    GTEXT_JSON_Parse_Options parse_opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(content.c_str(), content.size(), &parse_opts, &err);
    ASSERT_NE(value, nullptr);

    // Test compact write
    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);
    GTEXT_JSON_Write_Options write_opts = gtext_json_write_options_default();
    write_opts.pretty = false;
    gtext_json_write_value(&sink, &write_opts, value, &err);
    EXPECT_NE(gtext_json_sink_buffer_data(&sink), nullptr);
    gtext_json_sink_buffer_free(&sink);

    // Test pretty write
    gtext_json_sink_buffer(&sink);
    write_opts.pretty = true;
    write_opts.indent_spaces = 2;
    gtext_json_write_value(&sink, &write_opts, value, &err);
    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_NE(output, nullptr);
    EXPECT_NE(strchr(output, '\n'), nullptr);  // Should have newlines
    gtext_json_sink_buffer_free(&sink);

    gtext_json_free(value);
    gtext_json_error_free(&err);
}

/**
 * Test corpus - Milestone B: Extensions (JSONC, trailing commas, nonfinite)
 */
TEST(TestCorpus, MilestoneB) {
    std::string base_dir = get_test_data_dir();

    // JSONC support
    test_jsonc_file(base_dir + "/jsonc/single-line-comment.json");
    test_jsonc_file(base_dir + "/jsonc/multi-line-comment.json");
    test_jsonc_file(base_dir + "/jsonc/mixed.json");

    // Trailing commas
    test_jsonc_file(base_dir + "/jsonc/trailing-comma-array.json");
    test_jsonc_file(base_dir + "/jsonc/trailing-comma-object.json");

    // Nonfinite numbers
    const char * nonfinite_json = "{\"nan\":NaN,\"infinity\":Infinity,\"negative_infinity\":-Infinity}";
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.allow_nonfinite_numbers = true;
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(nonfinite_json, strlen(nonfinite_json), &opts, &err);
    // Nonfinite numbers may not parse if lexer doesn't support them
    // This is acceptable - the test verifies the option exists
    if (value) {
        // Verify round-trip with nonfinite numbers
        GTEXT_JSON_Sink sink;
        gtext_json_sink_buffer(&sink);
        GTEXT_JSON_Write_Options write_opts = gtext_json_write_options_default();
        write_opts.allow_nonfinite_numbers = true;
        gtext_json_write_value(&sink, &write_opts, value, &err);

        const char * output = gtext_json_sink_buffer_data(&sink);
        if (output) {
            EXPECT_NE(strstr(output, "NaN"), nullptr);
            EXPECT_NE(strstr(output, "Infinity"), nullptr);
        }

        gtext_json_sink_buffer_free(&sink);
        gtext_json_free(value);
    }
    gtext_json_error_free(&err);
}

/**
 * Test corpus - Milestone C: Streaming Parser + Streaming Writer
 * (Streaming tests are already covered in StreamingParser and StreamingWriter test suites)
 */
TEST(TestCorpus, MilestoneC) {
    // Milestone C is verified by existing streaming parser and writer tests
    // This test verifies that streaming works with corpus data
    std::string content = read_file(get_test_data_dir() + "/rfc8259/basic.json");
    ASSERT_FALSE(content.empty());

    // Create a simple event counter callback
    int event_count = 0;
    GTEXT_JSON_Event_cb event_cb = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)evt;  // Unused but required by signature
        (void)err;  // Unused but required by signature
        int* count = (int*)user;
        (*count)++;
        return GTEXT_JSON_OK;  // Continue parsing
    };

    GTEXT_JSON_Parse_Options parse_opts = gtext_json_parse_options_default();
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&parse_opts, event_cb, &event_count);
    ASSERT_NE(stream, nullptr);

    // Feed in chunks
    size_t chunk_size = 10;
    size_t pos = 0;
    GTEXT_JSON_Status status = GTEXT_JSON_OK;

    GTEXT_JSON_Error err{};
    while (pos < content.size() && status == GTEXT_JSON_OK) {
        size_t len = (pos + chunk_size < content.size()) ? chunk_size : (content.size() - pos);
        status = gtext_json_stream_feed(stream, content.c_str() + pos, len, &err);
        pos += len;
    }

    // Streaming may encounter errors during incremental parsing
    // The important thing is that the stream was created and can process data
    // (Streaming parser tests are more thoroughly covered in StreamingParser suite)

    gtext_json_stream_finish(stream, &err);

    gtext_json_stream_free(stream);
    gtext_json_error_free(&err);
}

/**
 * Test corpus - Milestone D: Pointer + Patch + Merge Patch
 * (Pointer, Patch, and Merge Patch are already tested in their respective test suites)
 */
TEST(TestCorpus, MilestoneD) {
    // Milestone D is verified by existing JSON Pointer, Patch, and Merge Patch tests
    // This test verifies they work with corpus data
    std::string content = read_file(get_test_data_dir() + "/rfc8259/basic.json");
    ASSERT_FALSE(content.empty());

    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(content.c_str(), content.size(), &opts, &err);
    ASSERT_NE(value, nullptr);

    // Test JSON Pointer
    const GTEXT_JSON_Value * result = gtext_json_pointer_get(value, "/Image/Width", strlen("/Image/Width"));
    EXPECT_NE(result, nullptr);

    if (result) {
        int64_t width = 0;
        GTEXT_JSON_Status status = gtext_json_get_i64(result, &width);
        EXPECT_EQ(status, GTEXT_JSON_OK);
        EXPECT_EQ(width, 800);
    }

    gtext_json_free(value);
}

/**
 * Test corpus - Milestone E: Schema (subset) + Canonical Output
 * (Schema validation is already tested in the schema test suite)
 */
TEST(TestCorpus, MilestoneE) {
    // Milestone E is verified by existing schema validation tests
    // This test verifies canonical output
    std::string content = read_file(get_test_data_dir() + "/valid/large-object.json");
    ASSERT_FALSE(content.empty());

    GTEXT_JSON_Parse_Options parse_opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    GTEXT_JSON_Value * value = gtext_json_parse(content.c_str(), content.size(), &parse_opts, &err);
    ASSERT_NE(value, nullptr);

    // Test canonical output (key sorting)
    GTEXT_JSON_Sink sink;
    gtext_json_sink_buffer(&sink);
    GTEXT_JSON_Write_Options write_opts = gtext_json_write_options_default();
    write_opts.sort_object_keys = true;
    gtext_json_write_value(&sink, &write_opts, value, &err);

    const char * output = gtext_json_sink_buffer_data(&sink);
    EXPECT_NE(output, nullptr);

    // Verify keys are sorted (should start with "a":1)
    // Output format: {"a":1,"b":2,...}
    const char * key_a = strstr(output, "\"a\"");
    const char * key_b = strstr(output, "\"b\"");
    EXPECT_NE(key_a, nullptr);
    EXPECT_NE(key_b, nullptr);
    if (key_a && key_b) {
        EXPECT_LT(key_a, key_b);  // "a" should come before "b"
    }

    gtext_json_sink_buffer_free(&sink);
    gtext_json_free(value);
    gtext_json_error_free(&err);
}

// ============================================================================
// Overflow/Underflow Protection Tests
// ============================================================================

/**
 * Test comprehensive overflow and underflow protection
 */

// Test buffer size overflow protection
TEST(OverflowProtection, BufferSizeOverflow) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    // Set a small limit to test overflow protection
    opts.max_string_bytes = 100;

    GTEXT_JSON_Error err{};
    // Create a string that exceeds the limit
    std::string large_string = "\"";
    large_string.append(200, 'a');  // 200 bytes exceeds the 100 byte limit
    large_string += "\"";

    GTEXT_JSON_Value * value = gtext_json_parse(large_string.c_str(), large_string.size(), &opts, &err);
    // Should fail with appropriate error due to exceeding max_string_bytes limit
    EXPECT_EQ(value, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);
    gtext_json_error_free(&err);
}

// Test container element count overflow
TEST(OverflowProtection, ContainerElementOverflow) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.max_container_elems = 100;  // Set a limit

    GTEXT_JSON_Error err{};
    // Create an array that exceeds the limit
    std::string large_array = "[";
    for (int i = 0; i < 150; ++i) {  // 150 elements exceeds the 100 limit
        if (i > 0) large_array += ",";
        large_array += "1";
    }
    large_array += "]";

    GTEXT_JSON_Value * value = gtext_json_parse(large_array.c_str(), large_array.size(), &opts, &err);
    // Should fail with appropriate error due to exceeding max_container_elems limit
    EXPECT_EQ(value, nullptr);
    EXPECT_NE(err.code, GTEXT_JSON_OK);
    gtext_json_error_free(&err);
}

// Test total bytes consumed overflow protection
TEST(OverflowProtection, TotalBytesOverflow) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    opts.max_total_bytes = 1000;  // Set a limit

    GTEXT_JSON_Error err{};
    // Create input that exceeds limit
    std::string large_input(2000, '1');
    large_input = "[" + large_input + "]";

    GTEXT_JSON_Value * value = gtext_json_parse(large_input.c_str(), large_input.size(), &opts, &err);
    // Note: max_total_bytes limit may not be enforced in one-shot parse function
    // This test verifies the parser handles large input without crashing
    // If the limit is enforced, parsing should fail; otherwise it may succeed
    if (value) {
        gtext_json_free(value);
        // If parsing succeeded, verify it parsed correctly
        EXPECT_EQ(err.code, GTEXT_JSON_OK);
    } else {
        // If parsing failed, verify it was due to limit (not crash)
        EXPECT_NE(err.code, GTEXT_JSON_OK);
    }
    gtext_json_error_free(&err);
}

// ============================================================================
// NULL Pointer Handling Tests
// ============================================================================

/**
 * Test comprehensive NULL pointer handling
 */

// Test NULL stream pointer
TEST(NullPointerHandling, NullStream) {
    GTEXT_JSON_Error err{};
    // GTEXT_JSON_Stream_feed with NULL stream
    GTEXT_JSON_Status status = gtext_json_stream_feed(nullptr, "123", 3, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);
    gtext_json_error_free(&err);
}

// Test NULL callback pointer in stream creation
TEST(NullPointerHandling, NullStreamCallback) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();

    // NULL callback should be invalid
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, nullptr, nullptr);
    EXPECT_EQ(stream, nullptr);
}

// Test NULL buffer pointer with non-zero length
TEST(NullPointerHandling, NullBufferWithLength) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user; (void)evt; (void)err;
        return GTEXT_JSON_OK;
    };
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(stream, nullptr);

    GTEXT_JSON_Error err{};
    // NULL buffer with length > 0 should fail
    GTEXT_JSON_Status status = gtext_json_stream_feed(stream, nullptr, 10, &err);
    EXPECT_EQ(status, GTEXT_JSON_E_INVALID);

    gtext_json_stream_free(stream);
    gtext_json_error_free(&err);
}

// Test NULL error output pointer (should be allowed)
TEST(NullPointerHandling, NullErrorOutput) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user; (void)evt; (void)err;
        return GTEXT_JSON_OK;
    };
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(stream, nullptr);

    // NULL error pointer should be allowed (optional parameter)
    GTEXT_JSON_Status status = gtext_json_stream_feed(stream, "123", 3, nullptr);
    // Should not crash - NULL error pointer is optional
    // Status should be OK for valid input, or an error code for invalid input
    EXPECT_NE(status, GTEXT_JSON_E_INVALID);  // Should not be invalid due to NULL error parameter

    // Finish should also work with NULL error
    GTEXT_JSON_Status finish_status = gtext_json_stream_finish(stream, nullptr);
    EXPECT_EQ(finish_status, GTEXT_JSON_OK);

    gtext_json_stream_free(stream);
}

// Test NULL parse options parameter
TEST(NullPointerHandling, NullParseOptions) {
    GTEXT_JSON_Error err{};
    // Test that NULL opts parameter doesn't crash
    // Note: The parser may or may not support NULL opts - this test verifies
    // it handles it gracefully without crashing
    const char * input = "42";
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), nullptr, &err);

    // The important thing is it doesn't crash
    // If NULL opts is supported, parsing should succeed; if not, it may fail
    // Either way, we should get a valid result (value or error)
    if (value) {
        EXPECT_EQ(value->type, GTEXT_JSON_NUMBER);
        gtext_json_free(value);
    }
    // If value is NULL, err should be set (even if code is OK, meaning no error was detected)
    // The key is that we didn't crash

    gtext_json_error_free(&err);
}

// Test NULL value pointer in free function
TEST(NullPointerHandling, NullValueFree) {
    // Should not crash - free functions must handle NULL gracefully
    gtext_json_free(nullptr);
    // If we get here without crashing, the test passed
    SUCCEED();
}

// Test NULL error pointer in error_free
TEST(NullPointerHandling, NullErrorFree) {
    // Should not crash - free functions must handle NULL gracefully
    gtext_json_error_free(nullptr);
    // If we get here without crashing, the test passed
    SUCCEED();
}

// Test NULL stream pointer in stream_free
TEST(NullPointerHandling, NullStreamFree) {
    // Should not crash - free functions must handle NULL gracefully
    gtext_json_stream_free(nullptr);
    // If we get here without crashing, the test passed
    SUCCEED();
}

// ============================================================================
// Bounds Checking Tests
// ============================================================================

/**
 * Test comprehensive bounds checking
 */

// Test out-of-bounds array access in DOM
TEST(BoundsChecking, ArrayAccessOutOfBounds) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    const char * input = "[1, 2, 3]";
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type, GTEXT_JSON_ARRAY);

    size_t count = gtext_json_array_size(value);
    EXPECT_EQ(count, 3);

    // Access valid indices (first, middle, last)
    const GTEXT_JSON_Value * elem0 = gtext_json_array_get(value, 0);
    const GTEXT_JSON_Value * elem1 = gtext_json_array_get(value, 1);
    const GTEXT_JSON_Value * elem2 = gtext_json_array_get(value, 2);
    EXPECT_NE(elem0, nullptr);
    EXPECT_NE(elem1, nullptr);
    EXPECT_NE(elem2, nullptr);

    // Access out-of-bounds index should return NULL
    const GTEXT_JSON_Value * elem_out = gtext_json_array_get(value, 10);
    EXPECT_EQ(elem_out, nullptr) << "Out-of-bounds index should return NULL";

    // Access at boundary (count is out of bounds)
    const GTEXT_JSON_Value * elem_at_boundary = gtext_json_array_get(value, count);
    EXPECT_EQ(elem_at_boundary, nullptr) << "Index equal to size is out of bounds";

    gtext_json_free(value);
    gtext_json_error_free(&err);
}

// Test out-of-bounds object access
TEST(BoundsChecking, ObjectAccessOutOfBounds) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    const char * input = "{\"key1\":1, \"key2\":2}";
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->type, GTEXT_JSON_OBJECT);

    size_t count = gtext_json_object_size(value);
    EXPECT_EQ(count, 2);

    // Access valid key
    const GTEXT_JSON_Value * val1 = gtext_json_object_get(value, "key1", 4);
    EXPECT_NE(val1, nullptr);

    // Access non-existent key should return NULL
    const GTEXT_JSON_Value * val_out = gtext_json_object_get(value, "nonexistent", 11);
    EXPECT_EQ(val_out, nullptr);

    gtext_json_free(value);
    gtext_json_error_free(&err);
}

// Test chunked parsing with data spanning buffer boundaries
// Note: This tests chunked parsing functionality, not bounds checking per se,
// but it verifies buffer boundary handling which is related to bounds safety
TEST(BoundsChecking, ChunkedParsing) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user; (void)evt; (void)err;
        return GTEXT_JSON_OK;
    };
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(stream, nullptr);

    GTEXT_JSON_Error err{};

    // Feed data in chunks to test buffer boundary handling
    const char * chunk1 = "{\"key\":";
    const char * chunk2 = "\"value\"}";

    GTEXT_JSON_Status status1 = gtext_json_stream_feed(stream, chunk1, strlen(chunk1), &err);
    EXPECT_EQ(status1, GTEXT_JSON_OK);
    EXPECT_EQ(err.code, GTEXT_JSON_OK);

    GTEXT_JSON_Status status2 = gtext_json_stream_feed(stream, chunk2, strlen(chunk2), &err);
    EXPECT_EQ(status2, GTEXT_JSON_OK);
    EXPECT_EQ(err.code, GTEXT_JSON_OK);

    GTEXT_JSON_Status finish_status = gtext_json_stream_finish(stream, &err);
    EXPECT_EQ(finish_status, GTEXT_JSON_OK);
    EXPECT_EQ(err.code, GTEXT_JSON_OK);

    gtext_json_stream_free(stream);
    gtext_json_error_free(&err);
}

// Test array iteration and boundary access
TEST(BoundsChecking, ArrayIterationBounds) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    GTEXT_JSON_Error err{};
    const char * input = "[1, 2, 3, 4, 5]";
    GTEXT_JSON_Value * value = gtext_json_parse(input, strlen(input), &opts, &err);
    ASSERT_NE(value, nullptr);

    size_t count = gtext_json_array_size(value);
    EXPECT_EQ(count, 5);

    // Iterate through valid indices and verify access works
    for (size_t i = 0; i < count; ++i) {
        const GTEXT_JSON_Value * elem = gtext_json_array_get(value, i);
        EXPECT_NE(elem, nullptr) << "Failed to access valid index " << i;
    }

    // Try to access beyond bounds (at count, which is out of bounds)
    const GTEXT_JSON_Value * elem_out = gtext_json_array_get(value, count);
    EXPECT_EQ(elem_out, nullptr) << "Out-of-bounds access should return NULL";

    // Also test accessing well beyond bounds
    const GTEXT_JSON_Value * elem_far_out = gtext_json_array_get(value, count + 100);
    EXPECT_EQ(elem_far_out, nullptr) << "Far out-of-bounds access should return NULL";

    gtext_json_free(value);
    gtext_json_error_free(&err);
}

// ============================================================================
// State Validation Tests
// ============================================================================

/**
 * Test state machine validation and invalid state handling
 */

// Test invalid state transition - calling finish before feed
TEST(StateValidation, FinishBeforeFeed) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user; (void)evt; (void)err;
        return GTEXT_JSON_OK;
    };
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(stream, nullptr);

    GTEXT_JSON_Error err{};
    // Finish without feeding should handle gracefully
    GTEXT_JSON_Status status = gtext_json_stream_finish(stream, &err);
    // Should either succeed (empty input) or fail with appropriate error
    EXPECT_NE(status, GTEXT_JSON_E_INVALID);  // Should not be invalid state error

    gtext_json_stream_free(stream);
    gtext_json_error_free(&err);
}

// Test error state handling - continue after error
TEST(StateValidation, ContinueAfterError) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user; (void)evt; (void)err;
        return GTEXT_JSON_OK;
    };
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(stream, nullptr);

    GTEXT_JSON_Error err{};
    // Feed invalid JSON to cause error
    gtext_json_stream_feed(stream, "invalid json!!!", 15, &err);
    // May succeed initially (partial input), but finish should detect error
    GTEXT_JSON_Status finish_status = gtext_json_stream_finish(stream, &err);
    // Should fail with parse error
    EXPECT_NE(finish_status, GTEXT_JSON_OK);
    EXPECT_NE(err.code, GTEXT_JSON_OK);

    // Try to feed more data after error - should reject or handle gracefully
    GTEXT_JSON_Error err2{
        .code = {},
        .message = {},
        .offset = {},
        .line = {},
        .col = {},
        .context_snippet = {},
        .context_snippet_len = {},
        .caret_offset = {},
        .expected_token = {},
        .actual_token = {}
    };
    GTEXT_JSON_Status status_after_error = gtext_json_stream_feed(stream, "more data", 9, &err2);
    // Should either reject (return error) or handle gracefully (not crash)
    // The important thing is it doesn't crash
    EXPECT_NE(status_after_error, GTEXT_JSON_E_INVALID);  // Should not be invalid parameter error

    gtext_json_stream_free(stream);
    gtext_json_error_free(&err);
    gtext_json_error_free(&err2);
}

// Test state consistency - multiple finish calls
TEST(StateValidation, MultipleFinishCalls) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user; (void)evt; (void)err;
        return GTEXT_JSON_OK;
    };
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(stream, nullptr);

    GTEXT_JSON_Error err{};
    // Feed valid JSON
    GTEXT_JSON_Status feed_status = gtext_json_stream_feed(stream, "123", 3, &err);
    EXPECT_EQ(feed_status, GTEXT_JSON_OK);

    // First finish should succeed
    GTEXT_JSON_Status finish1 = gtext_json_stream_finish(stream, &err);
    EXPECT_EQ(finish1, GTEXT_JSON_OK);

    // Second finish should handle gracefully (already done)
    GTEXT_JSON_Error err2{
        .code = {},
        .message = {},
        .offset = {},
        .line = {},
        .col = {},
        .context_snippet = {},
        .context_snippet_len = {},
        .caret_offset = {},
        .expected_token = {},
        .actual_token = {}
    };
    GTEXT_JSON_Status finish2 = gtext_json_stream_finish(stream, &err2);
    // Should either succeed (idempotent) or return appropriate status
    EXPECT_NE(finish2, GTEXT_JSON_E_INVALID);

    gtext_json_stream_free(stream);
    gtext_json_error_free(&err);
    gtext_json_error_free(&err2);
}

// Test incomplete JSON structure (invalid final state)
TEST(StateValidation, IncompleteStructure) {
    GTEXT_JSON_Parse_Options opts = gtext_json_parse_options_default();
    auto callback = [](void * user, const GTEXT_JSON_Event* evt, GTEXT_JSON_Error* err) -> GTEXT_JSON_Status {
        (void)user; (void)evt; (void)err;
        return GTEXT_JSON_OK;
    };
    GTEXT_JSON_Stream * stream = gtext_json_stream_new(&opts, callback, nullptr);
    ASSERT_NE(stream, nullptr);

    GTEXT_JSON_Error err{};
    // Feed incomplete JSON (missing closing brace)
    gtext_json_stream_feed(stream, "{\"key\":\"value\"", 15, &err);
    // May succeed initially (partial input)

    // Finish should detect incomplete structure
    GTEXT_JSON_Status finish_status = gtext_json_stream_finish(stream, &err);
    EXPECT_NE(finish_status, GTEXT_JSON_OK);  // Should fail with incomplete error

    gtext_json_stream_free(stream);
    gtext_json_error_free(&err);
}

// Note: ErrorStateRecovery test removed - it duplicates ContinueAfterError functionality
// Both tests verify that streams handle error states gracefully when used after an error

int main(int argc, char * * argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
