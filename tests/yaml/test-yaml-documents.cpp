/**
 * @file test-yaml-documents.cpp
 * @brief Tests for multi-document YAML streams with --- and ... separators
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <stdlib.h>
#include <string.h>
}

//
// Helper: No-op callback for basic tests
//
static GTEXT_YAML_Status noop_cb(GTEXT_YAML_Stream *s, const void *evp, void *user) {
    (void)s; (void)evp; (void)user;
    return GTEXT_YAML_OK;
}

//
// Test: Single document with explicit ---
//
TEST(YamlDocuments, SingleDocumentExplicit) {
    const char *input = "---\nkey: value\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Single document without ---
//
TEST(YamlDocuments, SingleDocumentImplicit) {
    const char *input = "key: value\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Two documents separated by ---
//
TEST(YamlDocuments, TwoDocuments) {
    const char *input = "---\nfirst: 1\n---\nsecond: 2\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Three documents
//
TEST(YamlDocuments, ThreeDocuments) {
    const char *input = "---\nfirst: 1\n---\nsecond: 2\n---\nthird: 3\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Document with explicit end marker ...
//
TEST(YamlDocuments, DocumentWithEndMarker) {
    const char *input = "---\nkey: value\n...\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Two documents with end marker ...
//
TEST(YamlDocuments, TwoDocumentsWithEndMarkers) {
    const char *input = "---\nfirst: 1\n...\n---\nsecond: 2\n...\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Empty document (just ---)
//
TEST(YamlDocuments, EmptyDocument) {
    const char *input = "---\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Multiple empty documents
//
TEST(YamlDocuments, MultipleEmptyDocuments) {
    const char *input = "---\n---\n---\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Document with only ... (end marker without start)
//
TEST(YamlDocuments, OnlyEndMarker) {
    const char *input = "...\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    if (st == GTEXT_YAML_OK) {
        st = gtext_yaml_stream_finish(s);
    }
    
    // Implementation-dependent: may treat as empty document or error
    // Just verify no crash
    
    gtext_yaml_stream_free(s);
}

//
// Test: Mixed implicit and explicit document starts
//
TEST(YamlDocuments, MixedImplicitExplicit) {
    const char *input = "first: 1\n---\nsecond: 2\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Document with complex content between markers
//
TEST(YamlDocuments, ComplexDocuments) {
    const char *input = 
        "---\n"
        "users:\n"
        "  - name: Alice\n"
        "    age: 30\n"
        "  - name: Bob\n"
        "    age: 25\n"
        "---\n"
        "config:\n"
        "  host: localhost\n"
        "  port: 8080\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Chunked feeding across document boundaries
//
TEST(YamlDocuments, ChunkedAcrossBoundaries) {
    const char *part1 = "---\nfirst: 1\n-";
    const char *part2 = "--\nsecond: 2\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, part1, strlen(part1));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_feed(s, part2, strlen(part2));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}

//
// Test: Document separators in quoted strings (should not be treated as separators)
//
TEST(YamlDocuments, SeparatorsInQuotedStrings) {
    const char *input = "---\nkey: \"This has --- and ... inside\"\n";
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    ASSERT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
}
