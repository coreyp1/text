#include <gtest/gtest.h>
#include <text/text.h>
#include <text/json.h>

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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
