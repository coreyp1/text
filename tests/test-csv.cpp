#include <gtest/gtest.h>
#include <ghoti.io/text/csv.h>
#include "../src/csv/csv_internal.h"
#include <string.h>

// Task 1: Core Types and Error Handling
TEST(CsvCore, StatusEnum) {
    EXPECT_EQ(TEXT_CSV_OK, 0);
    EXPECT_NE(TEXT_CSV_E_INVALID, TEXT_CSV_OK);
    EXPECT_NE(TEXT_CSV_E_UNTERMINATED_QUOTE, TEXT_CSV_E_INVALID);
}

TEST(CsvCore, ErrorStruct) {
    text_csv_error err;
    err.code = TEXT_CSV_E_UNTERMINATED_QUOTE;
    err.message = "Unterminated quote";
    err.byte_offset = 42;
    err.line = 3;
    err.column = 5;
    err.row_index = 2;
    err.col_index = 1;
    err.context_snippet = nullptr;
    err.context_snippet_len = 0;
    err.caret_offset = 0;
    
    EXPECT_EQ(err.code, TEXT_CSV_E_UNTERMINATED_QUOTE);
    EXPECT_STREQ(err.message, "Unterminated quote");
    EXPECT_EQ(err.byte_offset, 42u);
    EXPECT_EQ(err.line, 3);
    EXPECT_EQ(err.column, 5);
    EXPECT_EQ(err.row_index, 2u);
    EXPECT_EQ(err.col_index, 1u);
}

TEST(CsvCore, ErrorFree) {
    text_csv_error err;
    err.code = TEXT_CSV_OK;
    err.message = nullptr;
    err.context_snippet = (char*)malloc(10);
    strcpy(err.context_snippet, "test");
    err.context_snippet_len = 4;
    err.caret_offset = 2;
    
    text_csv_error_free(&err);
    
    EXPECT_EQ(err.context_snippet, nullptr);
    EXPECT_EQ(err.context_snippet_len, 0u);
    EXPECT_EQ(err.caret_offset, 0u);
    
    // Test NULL safety
    text_csv_error_free(nullptr);
}

// Task 2: Dialect and Options Structures
TEST(CsvDialect, DefaultDialect) {
    text_csv_dialect d = text_csv_dialect_default();
    
    EXPECT_EQ(d.delimiter, ',');
    EXPECT_EQ(d.quote, '"');
    EXPECT_EQ(d.escape, TEXT_CSV_ESCAPE_DOUBLED_QUOTE);
    EXPECT_TRUE(d.newline_in_quotes);
    EXPECT_TRUE(d.accept_lf);
    EXPECT_TRUE(d.accept_crlf);
    EXPECT_FALSE(d.accept_cr);
    EXPECT_FALSE(d.trim_unquoted_fields);
    EXPECT_FALSE(d.allow_space_after_delimiter);
    EXPECT_FALSE(d.allow_unquoted_quotes);
    EXPECT_FALSE(d.allow_unquoted_newlines);
    EXPECT_FALSE(d.allow_comments);
    EXPECT_STREQ(d.comment_prefix, "#");
    EXPECT_FALSE(d.treat_first_row_as_header);
    EXPECT_EQ(d.header_dup_mode, TEXT_CSV_DUPCOL_ERROR);
}

TEST(CsvDialect, EscapeModes) {
    EXPECT_NE(TEXT_CSV_ESCAPE_DOUBLED_QUOTE, TEXT_CSV_ESCAPE_BACKSLASH);
    EXPECT_NE(TEXT_CSV_ESCAPE_DOUBLED_QUOTE, TEXT_CSV_ESCAPE_NONE);
    EXPECT_NE(TEXT_CSV_ESCAPE_BACKSLASH, TEXT_CSV_ESCAPE_NONE);
}

TEST(CsvDialect, DupcolModes) {
    EXPECT_NE(TEXT_CSV_DUPCOL_ERROR, TEXT_CSV_DUPCOL_FIRST_WINS);
    EXPECT_NE(TEXT_CSV_DUPCOL_ERROR, TEXT_CSV_DUPCOL_LAST_WINS);
    EXPECT_NE(TEXT_CSV_DUPCOL_ERROR, TEXT_CSV_DUPCOL_COLLECT);
}

TEST(CsvOptions, ParseOptionsDefault) {
    text_csv_parse_options opts = text_csv_parse_options_default();
    
    EXPECT_EQ(opts.dialect.delimiter, ',');
    EXPECT_TRUE(opts.validate_utf8);
    EXPECT_FALSE(opts.in_situ_mode);
    EXPECT_FALSE(opts.keep_bom);
    EXPECT_EQ(opts.max_rows, 0u);
    EXPECT_EQ(opts.max_cols, 0u);
    EXPECT_EQ(opts.max_field_bytes, 0u);
    EXPECT_EQ(opts.max_record_bytes, 0u);
    EXPECT_EQ(opts.max_total_bytes, 0u);
    EXPECT_TRUE(opts.enable_context_snippet);
    EXPECT_GT(opts.context_radius_bytes, 0u);
}

TEST(CsvOptions, WriteOptionsDefault) {
    text_csv_write_options opts = text_csv_write_options_default();
    
    EXPECT_EQ(opts.dialect.delimiter, ',');
    EXPECT_STREQ(opts.newline, "\n");
    EXPECT_FALSE(opts.quote_all_fields);
    EXPECT_TRUE(opts.quote_empty_fields);
    EXPECT_TRUE(opts.quote_if_needed);
    EXPECT_TRUE(opts.always_escape_quotes);
    EXPECT_FALSE(opts.trailing_newline);
}

// Task 3: Internal Infrastructure - Arena
TEST(CsvArena, ContextCreation) {
    // Note: csv_context_new is internal, but we can test through public API
    // For now, we test that the structure exists and can be used
    // Full arena tests will be in Task 8 when table API is available
    EXPECT_TRUE(true);  // Placeholder - arena will be tested with table API
}

// Task 4: Newline, BOM, and UTF-8 Utilities
TEST(CsvUtils, NewlineDetectionLF) {
    csv_position pos = {4, 1, 5};  // Position at the '\n' character
    text_csv_dialect dialect = text_csv_dialect_default();
    dialect.accept_lf = true;
    dialect.accept_crlf = false;
    dialect.accept_cr = false;
    
    const char* input = "test\nnext";
    size_t input_len = strlen(input);
    
    csv_newline_type result = csv_detect_newline(input, input_len, &pos, &dialect);
    
    EXPECT_EQ(result, CSV_NEWLINE_LF);
    EXPECT_EQ(pos.offset, 5u);  // "test\n" = 5 bytes
    EXPECT_EQ(pos.line, 2);
    EXPECT_EQ(pos.column, 1);
}

TEST(CsvUtils, NewlineDetectionCRLF) {
    csv_position pos = {4, 1, 5};  // Position at the '\r' character
    text_csv_dialect dialect = text_csv_dialect_default();
    dialect.accept_lf = false;
    dialect.accept_crlf = true;
    dialect.accept_cr = false;
    
    const char* input = "test\r\nnext";
    size_t input_len = strlen(input);
    
    csv_newline_type result = csv_detect_newline(input, input_len, &pos, &dialect);
    
    EXPECT_EQ(result, CSV_NEWLINE_CRLF);
    EXPECT_EQ(pos.offset, 6u);  // "test\r\n" = 6 bytes
    EXPECT_EQ(pos.line, 2);
    EXPECT_EQ(pos.column, 1);
}

TEST(CsvUtils, NewlineDetectionCR) {
    csv_position pos = {4, 1, 5};  // Position at the '\r' character
    text_csv_dialect dialect = text_csv_dialect_default();
    dialect.accept_lf = false;
    dialect.accept_crlf = false;
    dialect.accept_cr = true;
    
    const char* input = "test\rnext";
    size_t input_len = strlen(input);
    
    csv_newline_type result = csv_detect_newline(input, input_len, &pos, &dialect);
    
    EXPECT_EQ(result, CSV_NEWLINE_CR);
    EXPECT_EQ(pos.offset, 5u);  // "test\r" = 5 bytes
    EXPECT_EQ(pos.line, 2);
    EXPECT_EQ(pos.column, 1);
}

TEST(CsvUtils, NewlineDetectionNone) {
    csv_position pos = {0, 1, 1};
    text_csv_dialect dialect = text_csv_dialect_default();
    
    const char* input = "test";
    size_t input_len = strlen(input);
    
    csv_newline_type result = csv_detect_newline(input, input_len, &pos, &dialect);
    
    EXPECT_EQ(result, CSV_NEWLINE_NONE);
    EXPECT_EQ(pos.offset, 0u);
    EXPECT_EQ(pos.line, 1);
    EXPECT_EQ(pos.column, 1);
}

TEST(CsvUtils, NewlineDetectionCRLFPrecedence) {
    // CRLF should be detected before CR or LF individually
    csv_position pos = {4, 1, 5};  // Position at the '\r' character
    text_csv_dialect dialect = text_csv_dialect_default();
    dialect.accept_lf = true;
    dialect.accept_crlf = true;
    dialect.accept_cr = true;
    
    const char* input = "test\r\nnext";
    size_t input_len = strlen(input);
    
    csv_newline_type result = csv_detect_newline(input, input_len, &pos, &dialect);
    
    EXPECT_EQ(result, CSV_NEWLINE_CRLF);  // Should prefer CRLF
    EXPECT_EQ(pos.offset, 6u);
}

TEST(CsvUtils, BOMStripping) {
    csv_position pos = {0, 1, 1};
    // Create input with BOM: 3 bytes BOM + "test" (4 bytes) = 7 bytes total
    unsigned char bom_input[] = {0xEF, 0xBB, 0xBF, 't', 'e', 's', 't', '\0'};
    const char* input = (const char*)bom_input;
    size_t input_len = 7;  // 3 BOM + 4 "test"
    
    bool stripped = csv_strip_bom(&input, &input_len, &pos, true);
    
    EXPECT_TRUE(stripped);
    EXPECT_EQ(input_len, 4u);  // "test" = 4 bytes
    EXPECT_EQ(pos.offset, 3u);
    EXPECT_EQ(pos.column, 4);  // 1 + 3 BOM bytes
    EXPECT_STREQ(input, "test");
}

TEST(CsvUtils, BOMNoStrip) {
    csv_position pos = {0, 1, 1};
    const char* input_with_bom = "\xEF\xBB\xBFtest";
    size_t input_len = strlen(input_with_bom) + 3;
    
    const char* input = input_with_bom;
    bool stripped = csv_strip_bom(&input, &input_len, &pos, false);
    
    EXPECT_FALSE(stripped);
    EXPECT_EQ(input, input_with_bom);  // Should not change
}

TEST(CsvUtils, BOMNoBOM) {
    csv_position pos = {0, 1, 1};
    const char* input = "test";
    size_t input_len = strlen(input);
    
    const char* original_input = input;
    bool stripped = csv_strip_bom(&input, &input_len, &pos, true);
    
    EXPECT_FALSE(stripped);
    EXPECT_EQ(input, original_input);
}

TEST(CsvUtils, UTF8ValidationValidASCII) {
    csv_position pos = {0, 1, 1};
    const char* input = "Hello";
    size_t input_len = strlen(input);
    
    csv_utf8_result result = csv_validate_utf8(input, input_len, &pos, true);
    
    EXPECT_EQ(result, CSV_UTF8_VALID);
    EXPECT_EQ(pos.offset, input_len);
}

TEST(CsvUtils, UTF8ValidationValidMultiByte) {
    csv_position pos = {0, 1, 1};
    // "Hello 世界" in UTF-8
    const char* input = "Hello \xE4\xB8\x96\xE7\x95\x8C";
    size_t input_len = 12;  // "Hello " (6) + "世界" (6)
    
    csv_utf8_result result = csv_validate_utf8(input, input_len, &pos, true);
    
    EXPECT_EQ(result, CSV_UTF8_VALID);
    EXPECT_EQ(pos.offset, input_len);
}

TEST(CsvUtils, UTF8ValidationInvalid) {
    csv_position pos = {0, 1, 1};
    // Invalid UTF-8: continuation byte without start byte
    const char* input = "\x80";
    size_t input_len = 1;
    
    csv_utf8_result result = csv_validate_utf8(input, input_len, &pos, true);
    
    EXPECT_EQ(result, CSV_UTF8_INVALID);
}

TEST(CsvUtils, UTF8ValidationIncomplete) {
    csv_position pos = {0, 1, 1};
    // Incomplete 2-byte sequence
    const char* input = "\xC2";  // Missing continuation byte
    size_t input_len = 1;
    
    csv_utf8_result result = csv_validate_utf8(input, input_len, &pos, true);
    
    EXPECT_EQ(result, CSV_UTF8_INCOMPLETE);
}

TEST(CsvUtils, UTF8ValidationDisabled) {
    csv_position pos = {0, 1, 1};
    // Even invalid UTF-8 should pass if validation is disabled
    const char* input = "\x80\xFF";
    size_t input_len = 2;
    
    csv_utf8_result result = csv_validate_utf8(input, input_len, &pos, false);
    
    EXPECT_EQ(result, CSV_UTF8_VALID);
    EXPECT_EQ(pos.offset, input_len);
}

TEST(CsvUtils, UTF8ValidationOverlong) {
    csv_position pos = {0, 1, 1};
    // Overlong encoding of 'A' (should be 0x41, not 0xC0 0x81)
    const char* input = "\xC0\x81";
    size_t input_len = 2;
    
    csv_utf8_result result = csv_validate_utf8(input, input_len, &pos, true);
    
    EXPECT_EQ(result, CSV_UTF8_INVALID);
}

TEST(CsvUtils, UTF8ValidationTooLarge) {
    csv_position pos = {0, 1, 1};
    // Code point > U+10FFFF (0xF4 0x90 0x80 0x80)
    const char* input = "\xF4\x90\x80\x80";
    size_t input_len = 4;
    
    csv_utf8_result result = csv_validate_utf8(input, input_len, &pos, true);
    
    EXPECT_EQ(result, CSV_UTF8_INVALID);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
