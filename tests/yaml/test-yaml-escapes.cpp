#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <stdlib.h>
#include <string.h>
}

static char *last_scalar = NULL;
static size_t last_len = 0;

static GTEXT_YAML_Status capture_cb(GTEXT_YAML_Stream *s, const void * event_payload, void * user) {
    (void)s; (void)user;
    const GTEXT_YAML_Event *e = (const GTEXT_YAML_Event *)event_payload;
    if (e->type == GTEXT_YAML_EVENT_SCALAR) {
        if (last_scalar) free(last_scalar);
        last_len = e->data.scalar.len;
        last_scalar = (char *)malloc(last_len + 1);
        memcpy(last_scalar, e->data.scalar.ptr, last_len);
        last_scalar[last_len] = '\0';
    }
    return GTEXT_YAML_OK;
}

static void reset_scalar() {
    if (last_scalar) {
        free(last_scalar);
        last_scalar = NULL;
    }
    last_len = 0;
}

//
// Test: Basic Unicode escape (\u263A → ☺)
//
TEST(YamlEscapes, UnicodeEscape) {
    const char *in1 = "\"hello\\u263A\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, in1, strlen(in1));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_len, 8);  // "hello" (5) + ☺ (3 bytes UTF-8)
    const unsigned char *ub = (const unsigned char *)last_scalar;
    EXPECT_EQ(ub[5], 0xE2);
    EXPECT_EQ(ub[6], 0x98);
    EXPECT_EQ(ub[7], 0xBA);
    
    reset_scalar();
}

//
// Test: Newline escape (\n)
//
TEST(YamlEscapes, NewlineEscape) {
    const char *input = "\"line1\\nline2\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, "line1\nline2");
    
    reset_scalar();
}

//
// Test: Tab escape (\t)
//
TEST(YamlEscapes, TabEscape) {
    const char *input = "\"col1\\tcol2\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, "col1\tcol2");
    
    reset_scalar();
}

//
// Test: Carriage return escape (\r)
//
TEST(YamlEscapes, CarriageReturnEscape) {
    const char *input = "\"line1\\rline2\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, "line1\rline2");
    
    reset_scalar();
}

//
// Test: Backslash escape (\\)
//
TEST(YamlEscapes, BackslashEscape) {
    const char *input = "\"path\\\\to\\\\file\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, "path\\to\\file");
    
    reset_scalar();
}

//
// Test: Double quote escape (\")
//
TEST(YamlEscapes, DoubleQuoteEscape) {
    const char *input = "\"He said \\\"hello\\\"\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, "He said \"hello\"");
    
    reset_scalar();
}

//
// TODO: The following escape sequences are defined in YAML 1.2.2 but not yet implemented in the scanner:
// - \  (non-breaking space, 0xA0)
// - \_ (non-breaking space, 0xA0)
// - \N (next line, 0x85)
// - \L (line separator, 0x2028)
// - \P (paragraph separator, 0x2029)
//
// The following are now implemented:
// - \0 (null, 0x00) ✅
// - \a (bell, 0x07) ✅
// - \b (backspace, 0x08) ✅
// - \f (form feed, 0x0C) ✅
// - \v (vertical tab, 0x0B) ✅
// - \e (escape, 0x1B) ✅
//

//
// Test: Null escape (\0)
//
TEST(YamlEscapes, NullEscape) {
    const char *input = "\"text\\0more\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_len, 9);  // "text" (4) + \0 (1) + "more" (4)
    // Verify the null byte is actually there
    EXPECT_EQ(last_scalar[4], '\0');
    
    reset_scalar();
}

//
// Test: Bell escape (\a or \x07)
//
TEST(YamlEscapes, BellEscape) {
    const char *input = "\"alert\\a\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_len, 6);  // "alert" (5) + \a (1)
    EXPECT_EQ(last_scalar[5], '\a');
    
    reset_scalar();
}

//
// Test: Backspace escape (\b)
//
TEST(YamlEscapes, BackspaceEscape) {
    const char *input = "\"text\\bmore\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_scalar[4], '\b');
    
    reset_scalar();
}

//
// Test: Form feed escape (\f)
//
TEST(YamlEscapes, FormFeedEscape) {
    const char *input = "\"page1\\fpage2\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_scalar[5], '\f');
    
    reset_scalar();
}

//
// Test: Vertical tab escape (\v)
//
TEST(YamlEscapes, VerticalTabEscape) {
    const char *input = "\"line1\\vline2\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_scalar[5], '\v');
    
    reset_scalar();
}

//
// Test: Escape escape (\e or \x1B)
//
TEST(YamlEscapes, EscapeEscape) {
    const char *input = "\"\\e[31mred\\e[0m\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ((unsigned char)last_scalar[0], 0x1B);  // ESC character
    
    reset_scalar();
}

//
// Test: Hexadecimal escape (\xHH)
//
TEST(YamlEscapes, HexEscape) {
    const char *input = "\"\\x41\\x42\\x43\"";  // ABC
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, "ABC");
    
    reset_scalar();
}

//
// Test: 32-bit Unicode escape (\UHHHHHHHH)
//
TEST(YamlEscapes, Unicode32Escape) {
    const char *input = "\"\\U0001F600\"";  // 😀 (grinning face emoji)
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_EQ(last_len, 4);  // 😀 is 4 bytes in UTF-8
    const unsigned char *ub = (const unsigned char *)last_scalar;
    EXPECT_EQ(ub[0], 0xF0);
    EXPECT_EQ(ub[1], 0x9F);
    EXPECT_EQ(ub[2], 0x98);
    EXPECT_EQ(ub[3], 0x80);
    
    reset_scalar();
}

//
// Test: Multiple escapes in one string
//
TEST(YamlEscapes, MultipleEscapes) {
    const char *input = "\"\\\"Hello\\nWorld\\t!\\\"\"";
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, "\"Hello\nWorld\t!\"");
    
    reset_scalar();
}

//
// Test: Space escape (\ followed by space)
//
TEST(YamlEscapes, SpaceEscape) {
    const char *input = "\"\\ \"";  // Escaped space
    reset_scalar();
    
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    
    gtext_yaml_stream_free(s);
    
    ASSERT_NE(last_scalar, nullptr);
    EXPECT_STREQ(last_scalar, " ");
    
    reset_scalar();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
