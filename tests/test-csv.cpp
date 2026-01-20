#include <gtest/gtest.h>
#include <ghoti.io/text/csv.h>
#include "../src/csv/csv_internal.h"
#include <string.h>
#include <vector>
#include <string>

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

// Task 5-7: Streaming Parser Tests
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

// Task 8-9: Table Parsing Tests
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
