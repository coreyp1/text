#include <gtest/gtest.h>
#include <ghoti.io/text/csv.h>
#include "../src/csv/csv_internal.h"
#include <string.h>
#include <vector>
#include <string>

// Core Types and Error Handling
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

// Dialect and Options Structures
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

// Internal Infrastructure - Arena
TEST(CsvArena, ContextCreation) {
    // Note: csv_context_new is internal, but we can test through public API
    // For now, we test that the structure exists and can be used
    // Full arena tests will be with table API
    EXPECT_TRUE(true);  // Placeholder - arena will be tested with table API
}

// Newline, BOM, and UTF-8 Utilities
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

// Streaming Parser Tests
TEST(CsvStream, BasicParsing) {
    const char* input = "a,b,c\n1,2,3\n";
    size_t input_len = strlen(input);

    size_t record_count = 0;
    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* counts = (std::pair<size_t*, std::vector<std::string>*>*)user_data;

        switch (event->type) {
            case TEXT_CSV_EVENT_RECORD_BEGIN:
                (*counts->first)++;
                counts->second->clear();
                break;
            case TEXT_CSV_EVENT_FIELD:
                counts->second->push_back(std::string(event->data, event->data_len));
                break;
            case TEXT_CSV_EVENT_RECORD_END:
                break;
            case TEXT_CSV_EVENT_END:
                break;
        }
        return TEXT_CSV_OK;
    };

    std::pair<size_t*, std::vector<std::string>*> user_data(&record_count, &fields);

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &user_data);
    ASSERT_NE(stream, nullptr);

    text_csv_status status = text_csv_stream_feed(stream, input, input_len, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(record_count, 2u);  // Two records
}

TEST(CsvStream, QuotedFields) {
    const char* input = "\"a,b\",\"c\"\"d\",\"e\nf\"\n";
    size_t input_len = strlen(input);

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    text_csv_status status = text_csv_stream_feed(stream, input, input_len, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "a,b");
    EXPECT_EQ(fields[1], "c\"d");  // Doubled quote unescaped
    EXPECT_EQ(fields[2], "e\nf");  // Newline in quoted field
}

// Test field that spans multiple feed() calls - quoted field
TEST(CsvStream, FieldSpanningChunksQuoted) {
    const char* chunk1 = "\"field1";
    const char* chunk2 = " that spans";
    const char* chunk3 = " chunks\",field2\n";

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    // Feed in chunks
    text_csv_status status = text_csv_stream_feed(stream, chunk1, strlen(chunk1), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk2, strlen(chunk2), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk3, strlen(chunk3), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0], "field1 that spans chunks");
    EXPECT_EQ(fields[1], "field2");
}

// Test unquoted field spanning multiple chunks
TEST(CsvStream, FieldSpanningChunksUnquoted) {
    const char* chunk1 = "field1";
    const char* chunk2 = "part2";
    const char* chunk3 = "part3,field2\n";

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    text_csv_status status = text_csv_stream_feed(stream, chunk1, strlen(chunk1), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk2, strlen(chunk2), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk3, strlen(chunk3), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0], "field1part2part3");
    EXPECT_EQ(fields[1], "field2");
}

// Test quoted field with newlines spanning multiple chunks
TEST(CsvStream, FieldSpanningChunksWithNewlines) {
    const char* chunk1 = "\"line1\n";
    const char* chunk2 = "line2\n";
    const char* chunk3 = "line3\",next\n";

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    text_csv_status status = text_csv_stream_feed(stream, chunk1, strlen(chunk1), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk2, strlen(chunk2), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk3, strlen(chunk3), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0], "line1\nline2\nline3");
    EXPECT_EQ(fields[1], "next");
}

// Test field spanning many small chunks (byte-by-byte)
TEST(CsvStream, FieldSpanningManySmallChunks) {
    const char* full_field = "\"This is a field that will be split into many tiny chunks\"";
    size_t full_len = strlen(full_field);

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    // Feed byte by byte
    for (size_t i = 0; i < full_len; ++i) {
        text_csv_status status = text_csv_stream_feed(stream, full_field + i, 1, nullptr);
        EXPECT_EQ(status, TEXT_CSV_OK) << "Failed at byte " << i;
    }

    text_csv_status status = text_csv_stream_feed(stream, ",next\n", 6, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0], "This is a field that will be split into many tiny chunks");
    EXPECT_EQ(fields[1], "next");
}

// Test large field approaching max_field_bytes limit
TEST(CsvStream, LargeFieldSpanningChunks) {
    // Create a field that's close to but under the limit
    const size_t field_size = 10000;  // 10KB field
    std::string large_field = "\"";
    large_field.reserve(field_size + 100);
    for (size_t i = 0; i < field_size; ++i) {
        large_field += 'A' + (i % 26);
    }
    large_field += "\",small\n";

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.max_field_bytes = 20000;  // Set limit higher than field size
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    // Split into chunks of 1000 bytes
    const size_t chunk_size = 1000;
    for (size_t i = 0; i < large_field.size(); i += chunk_size) {
        size_t chunk_len = std::min(chunk_size, large_field.size() - i);
        text_csv_status status = text_csv_stream_feed(stream, large_field.c_str() + i, chunk_len, nullptr);
        EXPECT_EQ(status, TEXT_CSV_OK) << "Failed at offset " << i;
    }

    text_csv_status status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0].size(), field_size);
    EXPECT_EQ(fields[1], "small");
}

// Test multiple fields spanning chunks in same record
TEST(CsvStream, MultipleFieldsSpanningChunks) {
    const char* chunk1 = "\"field1";
    const char* chunk2 = "part2\",\"field2";
    const char* chunk3 = "part2\",field3\n";

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    text_csv_status status = text_csv_stream_feed(stream, chunk1, strlen(chunk1), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk2, strlen(chunk2), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk3, strlen(chunk3), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "field1part2");
    EXPECT_EQ(fields[1], "field2part2");
    EXPECT_EQ(fields[2], "field3");
}

// Test field that completes exactly at chunk boundary
TEST(CsvStream, FieldCompletingAtChunkBoundary) {
    const char* chunk1 = "\"field1\",";
    const char* chunk2 = "field2\n";

    std::vector<std::string> fields;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        auto* fields_vec = (std::vector<std::string>*)user_data;
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            fields_vec->push_back(std::string(event->data, event->data_len));
        }
        return TEXT_CSV_OK;
    };

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_stream* stream = text_csv_stream_new(&opts, callback, &fields);
    ASSERT_NE(stream, nullptr);

    text_csv_status status = text_csv_stream_feed(stream, chunk1, strlen(chunk1), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_feed(stream, chunk2, strlen(chunk2), nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_stream_free(stream);

    EXPECT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0], "field1");
    EXPECT_EQ(fields[1], "field2");
}

// Table Parsing Tests
TEST(CsvTable, BasicParsing) {
    const char* input = "a,b,c\n1,2,3\n4,5,6\n";
    size_t input_len = strlen(input);

    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.dialect.treat_first_row_as_header = true;  // First row is header
    text_csv_error err;
    text_csv_table* table = text_csv_parse_table(input, input_len, &opts, &err);

    ASSERT_NE(table, nullptr);

    EXPECT_EQ(text_csv_row_count(table), 2u);
    EXPECT_EQ(text_csv_col_count(table, 0), 3u);
    EXPECT_EQ(text_csv_col_count(table, 1), 3u);

    size_t len;
    const char* field = text_csv_field(table, 0, 0, &len);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(std::string(field, len), "1");

    field = text_csv_field(table, 0, 1, &len);
    EXPECT_EQ(std::string(field, len), "2");

    field = text_csv_field(table, 1, 2, &len);
    EXPECT_EQ(std::string(field, len), "6");

    text_csv_free_table(table);
}

TEST(CsvTable, EmptyTable) {
    const char* input = "";
    size_t input_len = strlen(input);

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_error err;
    text_csv_table* table = text_csv_parse_table(input, input_len, &opts, &err);

    ASSERT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 0u);

    text_csv_free_table(table);
}

TEST(CsvTable, HeaderProcessing) {
    const char* input = "name,age,city\nJohn,30,NYC\nJane,25,LA\n";
    size_t input_len = strlen(input);

    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.dialect.treat_first_row_as_header = true;
    text_csv_error err;
    text_csv_table* table = text_csv_parse_table(input, input_len, &opts, &err);

    ASSERT_NE(table, nullptr);

    // Should have 2 data rows (header excluded)
    EXPECT_EQ(text_csv_row_count(table), 2u);

    // Test header lookup
    size_t name_idx, age_idx, city_idx;
    EXPECT_EQ(text_csv_header_index(table, "name", &name_idx), TEXT_CSV_OK);
    EXPECT_EQ(text_csv_header_index(table, "age", &age_idx), TEXT_CSV_OK);
    EXPECT_EQ(text_csv_header_index(table, "city", &city_idx), TEXT_CSV_OK);

    EXPECT_EQ(name_idx, 0u);
    EXPECT_EQ(age_idx, 1u);
    EXPECT_EQ(city_idx, 2u);

    // Access data using header indices
    size_t len;
    const char* name = text_csv_field(table, 0, name_idx, &len);
    EXPECT_EQ(std::string(name, len), "John");

    const char* age = text_csv_field(table, 0, age_idx, &len);
    EXPECT_EQ(std::string(age, len), "30");

    text_csv_free_table(table);
}

TEST(CsvTable, DuplicateColumnNames) {
    const char* input = "a,a,b\n1,2,3\n";
    size_t input_len = strlen(input);

    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.dialect.treat_first_row_as_header = true;
    opts.dialect.header_dup_mode = TEXT_CSV_DUPCOL_ERROR;
    text_csv_error err;
    text_csv_table* table = text_csv_parse_table(input, input_len, &opts, &err);

    // Should fail with duplicate column error
    EXPECT_EQ(table, nullptr);
    EXPECT_EQ(err.code, TEXT_CSV_E_INVALID);
}

TEST(CsvTable, QuotedFieldsInTable) {
    const char* input = "\"a,b\",\"c\"\"d\"\n\"1,2\",\"3\"\"4\"\n";
    size_t input_len = strlen(input);

    text_csv_parse_options opts = text_csv_parse_options_default();
    text_csv_error err;
    text_csv_table* table = text_csv_parse_table(input, input_len, &opts, &err);

    ASSERT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 2u);

    size_t len;
    const char* field1 = text_csv_field(table, 0, 0, &len);
    EXPECT_EQ(std::string(field1, len), "a,b");

    const char* field2 = text_csv_field(table, 0, 1, &len);
    EXPECT_EQ(std::string(field2, len), "c\"d");

    // Check second row
    const char* field3 = text_csv_field(table, 1, 0, &len);
    EXPECT_EQ(std::string(field3, len), "1,2");

    const char* field4 = text_csv_field(table, 1, 1, &len);
    EXPECT_EQ(std::string(field4, len), "3\"4");

    text_csv_free_table(table);
}

// Writer Infrastructure - Sink Abstraction
TEST(CsvSink, CallbackSink) {
    std::string output;

    // Custom write callback that appends to string
    auto write_callback = [](void* user, const char* bytes, size_t len) -> text_csv_status {
        std::string* str = (std::string*)user;
        str->append(bytes, len);
        return TEXT_CSV_OK;
    };

    text_csv_sink sink;
    sink.write = write_callback;
    sink.user = &output;

    // Write some data
    const char* test_data = "Hello, World!";
    text_csv_status result = sink.write(sink.user, test_data, strlen(test_data));
    EXPECT_EQ(result, TEXT_CSV_OK);
    EXPECT_EQ(output, "Hello, World!");

    // Write more data
    const char* more_data = " Test";
    result = sink.write(sink.user, more_data, strlen(more_data));
    EXPECT_EQ(result, TEXT_CSV_OK);
    EXPECT_EQ(output, "Hello, World! Test");
}

TEST(CsvSink, GrowableBuffer) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Write some data
    const char* test_data = "Hello, CSV!";
    status = sink.write(sink.user, test_data, strlen(test_data));
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Check buffer contents
    const char* data = text_csv_sink_buffer_data(&sink);
    size_t size = text_csv_sink_buffer_size(&sink);
    EXPECT_NE(data, nullptr);
    EXPECT_EQ(size, strlen(test_data));
    EXPECT_EQ(std::string(data, size), "Hello, CSV!");

    // Write more data
    const char* more_data = " More data";
    status = sink.write(sink.user, more_data, strlen(more_data));
    EXPECT_EQ(status, TEXT_CSV_OK);

    size = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(size, strlen(test_data) + strlen(more_data));
    EXPECT_EQ(std::string(data, size), "Hello, CSV! More data");

    // Cleanup
    text_csv_sink_buffer_free(&sink);
    EXPECT_EQ(sink.write, nullptr);
    EXPECT_EQ(sink.user, nullptr);
}

TEST(CsvSink, GrowableBufferLargeOutput) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Write a large amount of data to test buffer growth
    std::string large_data;
    for (int i = 0; i < 1000; i++) {
        large_data += "This is a test string. ";
    }

    status = sink.write(sink.user, large_data.c_str(), large_data.size());
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* data = text_csv_sink_buffer_data(&sink);
    size_t size = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(size, large_data.size());
    EXPECT_EQ(std::string(data, size), large_data);

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvSink, FixedBuffer) {
    char buffer[100];
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_fixed_buffer(&sink, buffer, sizeof(buffer));
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Write some data
    const char* test_data = "Hello, CSV!";
    status = sink.write(sink.user, test_data, strlen(test_data));
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Check buffer contents
    size_t used = text_csv_sink_fixed_buffer_used(&sink);
    bool truncated = text_csv_sink_fixed_buffer_truncated(&sink);
    EXPECT_EQ(used, strlen(test_data));
    EXPECT_FALSE(truncated);
    EXPECT_EQ(std::string(buffer, used), "Hello, CSV!");

    // Write more data
    const char* more_data = " More data";
    status = sink.write(sink.user, more_data, strlen(more_data));
    EXPECT_EQ(status, TEXT_CSV_OK);

    used = text_csv_sink_fixed_buffer_used(&sink);
    EXPECT_EQ(used, strlen(test_data) + strlen(more_data));
    EXPECT_FALSE(truncated);

    // Cleanup
    text_csv_sink_fixed_buffer_free(&sink);
    EXPECT_EQ(sink.write, nullptr);
    EXPECT_EQ(sink.user, nullptr);
}

TEST(CsvSink, FixedBufferTruncation) {
    char buffer[20];  // Small buffer
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_fixed_buffer(&sink, buffer, sizeof(buffer));
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Write data that exceeds buffer size
    const char* large_data = "This is a very long string that will exceed the buffer size";
    status = sink.write(sink.user, large_data, strlen(large_data));
    EXPECT_EQ(status, TEXT_CSV_E_WRITE);  // Should return error due to truncation

    // Check truncation flag
    bool truncated = text_csv_sink_fixed_buffer_truncated(&sink);
    EXPECT_TRUE(truncated);

    // Check that we wrote as much as possible
    size_t used = text_csv_sink_fixed_buffer_used(&sink);
    EXPECT_LT(used, strlen(large_data));
    EXPECT_LE(used, sizeof(buffer) - 1);  // -1 for null terminator

    text_csv_sink_fixed_buffer_free(&sink);
}

TEST(CsvSink, FixedBufferInvalidParams) {
    text_csv_sink sink;

    // Test NULL sink
    text_csv_status status = text_csv_sink_fixed_buffer(nullptr, (char*)"test", 4);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);

    // Test NULL buffer
    status = text_csv_sink_fixed_buffer(&sink, nullptr, 10);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);

    // Test zero size
    char buffer[10];
    status = text_csv_sink_fixed_buffer(&sink, buffer, 0);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);
}

TEST(CsvSink, GrowableBufferInvalidParams) {
    // Test NULL sink
    text_csv_status status = text_csv_sink_buffer(nullptr);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);
}

TEST(CsvSink, BufferAccessorsInvalidSink) {
    text_csv_sink sink;
    sink.write = nullptr;
    sink.user = nullptr;

    // Test invalid sink for growable buffer accessors
    const char* data = text_csv_sink_buffer_data(&sink);
    EXPECT_EQ(data, nullptr);

    size_t size = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(size, 0u);

    // Test invalid sink for fixed buffer accessors
    size_t used = text_csv_sink_fixed_buffer_used(&sink);
    EXPECT_EQ(used, 0u);

    bool truncated = text_csv_sink_fixed_buffer_truncated(&sink);
    EXPECT_FALSE(truncated);
}

// Field Escaping and Quoting Rules
TEST(CsvWriter, FieldQuotingNeededDelimiter) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_if_needed = true;
    opts.quote_all_fields = false;
    opts.quote_empty_fields = false;

    // Field with delimiter should be quoted
    const char* field = "hello,world";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(output_len, strlen(field) + 2); // +2 for quotes
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldQuotingNeededQuote) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_if_needed = true;
    opts.quote_all_fields = false;
    opts.quote_empty_fields = false;

    // Field with quote should be quoted
    const char* field = "hello\"world";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldQuotingNeededNewline) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_if_needed = true;
    opts.quote_all_fields = false;
    opts.quote_empty_fields = false;

    // Field with newline should be quoted
    const char* field = "hello\nworld";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldQuotingNotNeeded) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_if_needed = true;
    opts.quote_all_fields = false;
    opts.quote_empty_fields = false;

    // Simple field without special chars should not be quoted
    const char* field = "hello";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(output_len, strlen(field));
    EXPECT_NE(output[0], '"');
    EXPECT_STREQ(output, field);

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldQuotingAllFields) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_all_fields = true;
    opts.quote_if_needed = false;
    opts.quote_empty_fields = false;

    // All fields should be quoted when quote_all_fields is true
    const char* field = "hello";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(output_len, strlen(field) + 2); // +2 for quotes
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldQuotingEmptyField) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_empty_fields = true;
    opts.quote_all_fields = false;
    opts.quote_if_needed = false;

    // Empty field should be quoted when quote_empty_fields is true
    status = csv_write_field(&sink, nullptr, 0, &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(output_len, 2u); // Just two quotes
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[1], '"');

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldEscapingDoubledQuote) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.dialect.escape = TEXT_CSV_ESCAPE_DOUBLED_QUOTE;
    opts.quote_all_fields = true;

    // Field with quote should have quotes doubled
    const char* field = "hello\"world";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    // Original: hello"world (11 chars)
    // Escaped: hello""world (12 chars)
    // With quotes: "hello""world" (14 chars)
    EXPECT_EQ(output_len, 14u);
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    // Verify the escaped content: should be "hello""world"
    std::string result(output, output_len);
    // Find the doubled quote sequence
    size_t pos = result.find("\"\"");
    EXPECT_NE(pos, std::string::npos); // Should find doubled quote
    EXPECT_GT(pos, 0u); // Should be after opening quote
    EXPECT_LT(pos, output_len - 1); // Should be before closing quote

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldEscapingBackslash) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.dialect.escape = TEXT_CSV_ESCAPE_BACKSLASH;
    opts.quote_all_fields = true;

    // Field with quote should have quote escaped with backslash
    const char* field = "hello\"world";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    // Original: hello"world (11 chars)
    // Escaped: hello\"world (12 chars)
    // With quotes: "hello\"world" (14 chars)
    EXPECT_EQ(output_len, 14u);
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    // Verify the escaped content: should contain \"
    std::string result(output, output_len);
    size_t pos = result.find("\\\"");
    EXPECT_NE(pos, std::string::npos); // Should find backslash-quote sequence
    EXPECT_GT(pos, 0u); // Should be after opening quote
    EXPECT_LT(pos, output_len - 1); // Should be before closing quote

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldEscapingBackslashBackslash) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.dialect.escape = TEXT_CSV_ESCAPE_BACKSLASH;
    opts.quote_all_fields = true;

    // Field with backslash should have backslash escaped
    const char* field = "hello\\world";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    // Original: hello\world (11 chars)
    // Escaped: hello\\world (12 chars)
    // With quotes: "hello\\world" (14 chars)
    EXPECT_EQ(output_len, 14u);
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    // Verify the escaped content: should contain double backslash
    std::string result(output, output_len);
    size_t pos = result.find("\\\\");
    EXPECT_NE(pos, std::string::npos); // Should find doubled backslash
    EXPECT_GT(pos, 0u); // Should be after opening quote
    EXPECT_LT(pos, output_len - 1); // Should be before closing quote

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldEscapingNone) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.dialect.escape = TEXT_CSV_ESCAPE_NONE;
    opts.quote_all_fields = true;

    // Field with quote - no escaping (may cause issues, but tests the mode)
    const char* field = "hello\"world";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    // Original: hello"world (11 chars)
    // With quotes: "hello"world" (13 chars) - no escaping
    EXPECT_EQ(output_len, 13u);
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    // Verify no escaping occurred - should have single quote in middle
    std::string result(output, output_len);
    // Should contain the original quote character (not escaped)
    size_t pos = result.find("\"", 1); // Find quote after opening quote
    EXPECT_NE(pos, std::string::npos);
    EXPECT_LT(pos, output_len - 1); // Should be before closing quote
    // Next character should not be another quote (not doubled)
    if (pos + 1 < output_len - 1) {
        EXPECT_NE(result[pos + 1], '"');
    }

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldEscapingMultipleQuotes) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.dialect.escape = TEXT_CSV_ESCAPE_DOUBLED_QUOTE;
    opts.quote_all_fields = true;

    // Field with multiple quotes
    const char* field = "say \"hello\" and \"goodbye\"";
    status = csv_write_field(&sink, field, strlen(field), &opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(output[0], '"');
    EXPECT_EQ(output[output_len - 1], '"');

    // Count quotes in output (should be doubled)
    int quote_count = 0;
    for (size_t i = 0; i < output_len; i++) {
        if (output[i] == '"') {
            quote_count++;
        }
    }
    // Original has 4 quotes, doubled = 8, plus 2 for outer quotes = 10
    EXPECT_EQ(quote_count, 10);

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldRoundTripSimple) {
    // Test round-trip: write a field, then parse it back
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options write_opts = text_csv_write_options_default();
    write_opts.quote_if_needed = true;

    const char* original = "hello";
    status = csv_write_field(&sink, original, strlen(original), &write_opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* written = text_csv_sink_buffer_data(&sink);
    size_t written_len = text_csv_sink_buffer_size(&sink);

    // Parse it back using streaming parser
    text_csv_parse_options parse_opts = text_csv_parse_options_default();
    std::string field_value;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            std::string* field = (std::string*)user_data;
            *field = std::string(event->data, event->data_len);
        }
        return TEXT_CSV_OK;
    };

    text_csv_stream* stream = text_csv_stream_new(&parse_opts, callback, &field_value);
    EXPECT_NE(stream, nullptr);

    status = text_csv_stream_feed(stream, written, written_len, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Verify round-trip
    EXPECT_EQ(field_value, original);

    text_csv_stream_free(stream);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvWriter, FieldRoundTripWithQuotes) {
    // Test round-trip with quoted field containing quotes
    // Note: For a complete round-trip, we need to write a full record
    // This test verifies the escaping works correctly
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options write_opts = text_csv_write_options_default();
    write_opts.quote_if_needed = true;

    const char* original = "say \"hello\"";
    status = csv_write_field(&sink, original, strlen(original), &write_opts);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Add newline to make it a complete record
    const char* newline = "\n";
    status = sink.write(sink.user, newline, 1);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* written = text_csv_sink_buffer_data(&sink);
    size_t written_len = text_csv_sink_buffer_size(&sink);

    // Parse it back
    text_csv_parse_options parse_opts = text_csv_parse_options_default();
    std::string field_value;

    text_csv_event_cb callback = [](const text_csv_event* event, void* user_data) -> text_csv_status {
        if (event->type == TEXT_CSV_EVENT_FIELD) {
            std::string* field = (std::string*)user_data;
            *field = std::string(event->data, event->data_len);
        }
        return TEXT_CSV_OK;
    };

    text_csv_stream* stream = text_csv_stream_new(&parse_opts, callback, &field_value);
    EXPECT_NE(stream, nullptr);

    status = text_csv_stream_feed(stream, written, written_len, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_stream_finish(stream, nullptr);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Verify round-trip
    EXPECT_EQ(field_value, original);

    text_csv_stream_free(stream);
    text_csv_sink_buffer_free(&sink);
}

// Streaming Writer Tests
TEST(CsvStreamingWriter, CreateAndFree) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, CreateInvalidParams) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();

    // Test NULL sink
    text_csv_writer* writer = text_csv_writer_new(nullptr, &opts);
    EXPECT_EQ(writer, nullptr);

    // Test NULL options
    writer = text_csv_writer_new(&sink, nullptr);
    EXPECT_EQ(writer, nullptr);

    // Test invalid sink (no write function)
    text_csv_sink invalid_sink;
    invalid_sink.write = nullptr;
    invalid_sink.user = nullptr;
    writer = text_csv_writer_new(&invalid_sink, &opts);
    EXPECT_EQ(writer, nullptr);

    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, SimpleRecord) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    // Write a simple record: "hello,world"
    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "hello", 5);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "world", 5);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(std::string(output, output_len), "hello,world\n");

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, MultipleRecords) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    // Write two records
    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);
    status = text_csv_writer_field(writer, "a", 1);
    EXPECT_EQ(status, TEXT_CSV_OK);
    status = text_csv_writer_field(writer, "b", 1);
    EXPECT_EQ(status, TEXT_CSV_OK);
    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);
    status = text_csv_writer_field(writer, "c", 1);
    EXPECT_EQ(status, TEXT_CSV_OK);
    status = text_csv_writer_field(writer, "d", 1);
    EXPECT_EQ(status, TEXT_CSV_OK);
    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(std::string(output, output_len), "a,b\nc,d\n");

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, QuotedFields) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_if_needed = true;
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    // Write a record with a field containing a delimiter
    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "hello,world", 11);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "test", 4);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    // Field with delimiter should be quoted
    EXPECT_EQ(std::string(output, output_len), "\"hello,world\",test\n");

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, EmptyField) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.quote_empty_fields = true;
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "", 0);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "nonempty", 8);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    // Empty field should be quoted
    EXPECT_EQ(std::string(output, output_len), "\"\",nonempty\n");

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, StructuralEnforcementFieldWithoutRecord) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    // Try to write field without starting a record - should fail
    status = text_csv_writer_field(writer, "test", 4);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, StructuralEnforcementRecordEndWithoutRecord) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    // Try to end record without starting one - should fail
    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, StructuralEnforcementDoubleRecordBegin) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Try to start another record without ending the first - should fail
    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, StructuralEnforcementFieldAfterFinish) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "test", 4);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Try to write after finish - should fail
    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_E_INVALID);

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, FinishClosesOpenRecord) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "test", 4);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Finish without explicitly ending record - should close it
    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(std::string(output, output_len), "test\n");

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, CustomNewline) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.newline = "\r\n";
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "test", 4);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(std::string(output, output_len), "test\r\n");

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvStreamingWriter, CustomDelimiter) {
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options opts = text_csv_write_options_default();
    opts.dialect.delimiter = ';';
    text_csv_writer* writer = text_csv_writer_new(&sink, &opts);
    EXPECT_NE(writer, nullptr);

    status = text_csv_writer_record_begin(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "a", 1);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_field(writer, "b", 1);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_record_end(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_writer_finish(writer);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);
    EXPECT_EQ(std::string(output, output_len), "a;b\n");

    text_csv_writer_free(writer);
    text_csv_sink_buffer_free(&sink);
}

// ============================================================================
// Table Serialization Tests
// ============================================================================

TEST(CsvTableWrite, SimpleTable) {
    // Parse a simple CSV table (3 rows: header-like row + 2 data rows)
    const char* input = "a,b,c\n1,2,3\n4,5,6";
    text_csv_table* table = text_csv_parse_table(input, strlen(input), nullptr, nullptr);
    EXPECT_NE(table, nullptr);
    // Input has 3 lines, so 3 rows (no header processing, so all are data rows)
    EXPECT_GE(text_csv_row_count(table), 2u);

    // Write table to buffer
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_write_table(&sink, nullptr, table);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);

    // Output should match input (with possible newline differences)
    std::string output_str(output, output_len);
    EXPECT_TRUE(output_str.find("a,b,c") != std::string::npos);
    EXPECT_TRUE(output_str.find("1,2,3") != std::string::npos);
    EXPECT_TRUE(output_str.find("4,5,6") != std::string::npos);

    text_csv_free_table(table);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvTableWrite, RoundTripSimple) {
    // Parse a simple CSV
    const char* input = "a,b,c\n1,2,3\n4,5,6\n";
    text_csv_table* table1 = text_csv_parse_table(input, strlen(input), nullptr, nullptr);
    EXPECT_NE(table1, nullptr);

    // Write it
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_write_table(&sink, nullptr, table1);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);

    // Parse the output
    text_csv_table* table2 = text_csv_parse_table(output, output_len, nullptr, nullptr);
    EXPECT_NE(table2, nullptr);

    // Verify round-trip: same number of rows
    EXPECT_EQ(text_csv_row_count(table1), text_csv_row_count(table2));

    // Verify fields match
    for (size_t row = 0; row < text_csv_row_count(table1); row++) {
        EXPECT_EQ(text_csv_col_count(table1, row), text_csv_col_count(table2, row));
        for (size_t col = 0; col < text_csv_col_count(table1, row); col++) {
            size_t len1, len2;
            const char* field1 = text_csv_field(table1, row, col, &len1);
            const char* field2 = text_csv_field(table2, row, col, &len2);
            EXPECT_EQ(len1, len2);
            EXPECT_EQ(std::string(field1, len1), std::string(field2, len2));
        }
    }

    text_csv_free_table(table1);
    text_csv_free_table(table2);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvTableWrite, RoundTripWithQuotes) {
    // Parse CSV with quoted fields
    const char* input = "a,\"b,c\",d\n\"1,2\",3,\"4\"\n";
    text_csv_table* table1 = text_csv_parse_table(input, strlen(input), nullptr, nullptr);
    EXPECT_NE(table1, nullptr);

    // Write it
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_write_table(&sink, nullptr, table1);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);

    // Parse the output
    text_csv_table* table2 = text_csv_parse_table(output, output_len, nullptr, nullptr);
    EXPECT_NE(table2, nullptr);

    // Verify round-trip
    EXPECT_EQ(text_csv_row_count(table1), text_csv_row_count(table2));
    for (size_t row = 0; row < text_csv_row_count(table1); row++) {
        EXPECT_EQ(text_csv_col_count(table1, row), text_csv_col_count(table2, row));
        for (size_t col = 0; col < text_csv_col_count(table1, row); col++) {
            size_t len1, len2;
            const char* field1 = text_csv_field(table1, row, col, &len1);
            const char* field2 = text_csv_field(table2, row, col, &len2);
            EXPECT_EQ(len1, len2);
            EXPECT_EQ(std::string(field1, len1), std::string(field2, len2));
        }
    }

    text_csv_free_table(table1);
    text_csv_free_table(table2);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvTableWrite, WithHeader) {
    // Parse CSV with header
    const char* input = "name,age,city\nAlice,30,NYC\nBob,25,LA";
    text_csv_parse_options parse_opts = text_csv_parse_options_default();
    parse_opts.dialect.treat_first_row_as_header = true;

    text_csv_table* table = text_csv_parse_table(input, strlen(input), &parse_opts, nullptr);
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 2u); // Excludes header

    // Write table
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_write_table(&sink, nullptr, table);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);

    // Output should include header
    std::string output_str(output, output_len);
    EXPECT_TRUE(output_str.find("name,age,city") != std::string::npos);
    EXPECT_TRUE(output_str.find("Alice,30,NYC") != std::string::npos);
    EXPECT_TRUE(output_str.find("Bob,25,LA") != std::string::npos);

    text_csv_free_table(table);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvTableWrite, RoundTripWithHeader) {
    // Parse CSV with header
    const char* input = "name,age\nAlice,30\nBob,25";
    text_csv_parse_options parse_opts = text_csv_parse_options_default();
    parse_opts.dialect.treat_first_row_as_header = true;

    text_csv_table* table1 = text_csv_parse_table(input, strlen(input), &parse_opts, nullptr);
    EXPECT_NE(table1, nullptr);

    // Write it
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_write_table(&sink, nullptr, table1);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);

    // Parse the output with header
    text_csv_table* table2 = text_csv_parse_table(output, output_len, &parse_opts, nullptr);
    EXPECT_NE(table2, nullptr);

    // Verify round-trip
    EXPECT_EQ(text_csv_row_count(table1), text_csv_row_count(table2));
    for (size_t row = 0; row < text_csv_row_count(table1); row++) {
        EXPECT_EQ(text_csv_col_count(table1, row), text_csv_col_count(table2, row));
        for (size_t col = 0; col < text_csv_col_count(table1, row); col++) {
            size_t len1, len2;
            const char* field1 = text_csv_field(table1, row, col, &len1);
            const char* field2 = text_csv_field(table2, row, col, &len2);
            EXPECT_EQ(len1, len2);
            EXPECT_EQ(std::string(field1, len1), std::string(field2, len2));
        }
    }

    text_csv_free_table(table1);
    text_csv_free_table(table2);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvTableWrite, EmptyTable) {
    // Create empty table by parsing empty input
    const char* input = "";
    text_csv_table* table = text_csv_parse_table(input, 0, nullptr, nullptr);
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 0u);

    // Write empty table
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    status = text_csv_write_table(&sink, nullptr, table);
    EXPECT_EQ(status, TEXT_CSV_OK);

    // Empty table should produce empty output (or just newline if trailing_newline is set)
    size_t output_len = text_csv_sink_buffer_size(&sink);
    // With default options (trailing_newline=false), empty table produces empty output
    EXPECT_EQ(output_len, 0u);

    text_csv_free_table(table);
    text_csv_sink_buffer_free(&sink);
}

TEST(CsvTableWrite, CustomDialect) {
    // Parse CSV with semicolon delimiter
    const char* input = "a;b;c\n1;2;3";
    text_csv_parse_options parse_opts = text_csv_parse_options_default();
    parse_opts.dialect.delimiter = ';';

    text_csv_table* table = text_csv_parse_table(input, strlen(input), &parse_opts, nullptr);
    EXPECT_NE(table, nullptr);

    // Write with same dialect
    text_csv_sink sink;
    text_csv_status status = text_csv_sink_buffer(&sink);
    EXPECT_EQ(status, TEXT_CSV_OK);

    text_csv_write_options write_opts = text_csv_write_options_default();
    write_opts.dialect.delimiter = ';';

    status = text_csv_write_table(&sink, &write_opts, table);
    EXPECT_EQ(status, TEXT_CSV_OK);

    const char* output = text_csv_sink_buffer_data(&sink);
    size_t output_len = text_csv_sink_buffer_size(&sink);

    // Output should use semicolon delimiter
    std::string output_str(output, output_len);
    EXPECT_TRUE(output_str.find("a;b;c") != std::string::npos);
    EXPECT_TRUE(output_str.find("1;2;3") != std::string::npos);

    text_csv_free_table(table);
    text_csv_sink_buffer_free(&sink);
}

// In-Situ Mode Tests
TEST(CsvTableInSitu, BasicInSitu) {
    // Simple CSV with unquoted fields - should use in-situ mode when possible
    const char* input = "a,b,c";
    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.in_situ_mode = true;
    opts.validate_utf8 = false;  // UTF-8 validation disables in-situ mode

    text_csv_table* table = text_csv_parse_table(input, strlen(input), &opts, nullptr);
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 1u);
    EXPECT_EQ(text_csv_col_count(table, 0), 3u);

    // Verify content is correct (in-situ mode may or may not be used depending on implementation)
    size_t len;
    const char* field0 = text_csv_field(table, 0, 0, &len);
    EXPECT_NE(field0, nullptr);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(std::string(field0, len), "a");

    const char* field1 = text_csv_field(table, 0, 1, &len);
    EXPECT_NE(field1, nullptr);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(std::string(field1, len), "b");

    text_csv_free_table(table);
}

TEST(CsvTableInSitu, QuotedFieldsFallback) {
    // Quoted fields with doubled quotes need unescaping - should fall back to copy
    const char* input = "a,\"b\"\"c\",d";
    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.in_situ_mode = true;
    opts.validate_utf8 = false;

    text_csv_table* table = text_csv_parse_table(input, strlen(input), &opts, nullptr);
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 1u);
    EXPECT_EQ(text_csv_col_count(table, 0), 3u);

    // Verify content is correct
    size_t len;
    const char* field0 = text_csv_field(table, 0, 0, &len);
    EXPECT_EQ(std::string(field0, len), "a");

    // Middle field (quoted with doubled quotes) needs unescaping - should be copied
    const char* field1 = text_csv_field(table, 0, 1, &len);
    EXPECT_NE(field1, nullptr);
    EXPECT_EQ(len, 3u);  // "b"c" -> b"c (unescaped)
    EXPECT_EQ(std::string(field1, len), "b\"c");

    const char* field2 = text_csv_field(table, 0, 2, &len);
    EXPECT_EQ(std::string(field2, len), "d");

    text_csv_free_table(table);
}

TEST(CsvTableInSitu, Utf8ValidationDisablesInSitu) {
    // When UTF-8 validation is enabled, in-situ mode is disabled
    const char* input = "a,b,c";
    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.in_situ_mode = true;
    opts.validate_utf8 = true;  // This disables in-situ mode

    text_csv_table* table = text_csv_parse_table(input, strlen(input), &opts, nullptr);
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 1u);
    EXPECT_EQ(text_csv_col_count(table, 0), 3u);

    // Fields should be copied (not in-situ) when UTF-8 validation is enabled
    // We verify by checking content is correct
    size_t len;
    const char* field0 = text_csv_field(table, 0, 0, &len);
    EXPECT_NE(field0, nullptr);
    EXPECT_EQ(len, 1u);
    EXPECT_EQ(std::string(field0, len), "a");

    text_csv_free_table(table);
}

TEST(CsvTableInSitu, MixedMode) {
    // Mix of fields: some in-situ, some copied (due to unescaping)
    const char* input = "plain,\"quoted\"\"field\",another";
    text_csv_parse_options opts = text_csv_parse_options_default();
    opts.in_situ_mode = true;
    opts.validate_utf8 = false;

    text_csv_table* table = text_csv_parse_table(input, strlen(input), &opts, nullptr);
    EXPECT_NE(table, nullptr);
    EXPECT_EQ(text_csv_row_count(table), 1u);
    EXPECT_EQ(text_csv_col_count(table, 0), 3u);

    // Verify content is correct (regardless of in-situ or copied)
    size_t len;
    const char* field0 = text_csv_field(table, 0, 0, &len);
    EXPECT_EQ(std::string(field0, len), "plain");

    const char* field1 = text_csv_field(table, 0, 1, &len);
    EXPECT_EQ(std::string(field1, len), "quoted\"field");  // Unescaped

    const char* field2 = text_csv_field(table, 0, 2, &len);
    EXPECT_EQ(std::string(field2, len), "another");

    text_csv_free_table(table);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
