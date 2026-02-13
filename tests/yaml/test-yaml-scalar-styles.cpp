/**
 * @file test-yaml-scalar-styles.cpp
 * @brief Comprehensive tests for all YAML scalar styles
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <stdlib.h>
#include <string.h>
}

//
// Helper: Capture last scalar value
//
static char *last_scalar = NULL;
static size_t last_len = 0;

static GTEXT_YAML_Status capture_cb(GTEXT_YAML_Stream *s, const void *evp, void *user) {
  (void)s; (void)user;
  const GTEXT_YAML_Event *e = (const GTEXT_YAML_Event *)evp;
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
// Test: Plain scalar (no quotes)
//
TEST(YamlScalarStyles, PlainScalar) {
  const char *input = "hello";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_STREQ(last_scalar, "hello");
  EXPECT_EQ(last_len, 5);
  
  reset_scalar();
}

//
// Test: Single-quoted scalar
//
TEST(YamlScalarStyles, SingleQuoted) {
  const char *input = "'hello world'";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_STREQ(last_scalar, "hello world");
  
  reset_scalar();
}

//
// Test: Double-quoted scalar
//
TEST(YamlScalarStyles, DoubleQuoted) {
  const char *input = "\"hello world\"";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_STREQ(last_scalar, "hello world");
  
  reset_scalar();
}

//
// Test: Single-quoted with escaped single quote
//
TEST(YamlScalarStyles, SingleQuotedWithEscape) {
  const char *input = "'it''s working'";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_STREQ(last_scalar, "it's working");
  
  reset_scalar();
}

//
// Test: Double-quoted with escapes
//
TEST(YamlScalarStyles, DoubleQuotedWithEscapes) {
  const char *input = "\"line1\\nline2\\ttab\"";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  // Should contain newline and tab
  EXPECT_EQ(last_scalar[5], '\n');
  EXPECT_EQ(last_scalar[11], '\t');
  
  reset_scalar();
}

//
// Test: Literal scalar (|)
//
TEST(YamlScalarStyles, LiteralScalar) {
  const char *input = "|\n  line1\n  line2\n";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  // Literal should preserve newlines
  EXPECT_TRUE(strstr(last_scalar, "line1") != NULL);
  EXPECT_TRUE(strstr(last_scalar, "line2") != NULL);
  
  reset_scalar();
}

//
// Test: Folded scalar (>)
//
TEST(YamlScalarStyles, FoldedScalar) {
  const char *input = ">\n  line1\n  line2\n";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  // Folded should join lines with spaces
  EXPECT_TRUE(strstr(last_scalar, "line1") != NULL);
  EXPECT_TRUE(strstr(last_scalar, "line2") != NULL);
  
  reset_scalar();
}

//
// Test: Empty scalar
//
TEST(YamlScalarStyles, EmptyScalar) {
  const char *input = "\"\"";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_EQ(last_len, 0);
  EXPECT_STREQ(last_scalar, "");
  
  reset_scalar();
}

//
// Test: Scalar with special characters
//
TEST(YamlScalarStyles, SpecialCharacters) {
  const char *input = "\"@#$%^&*()\"";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_STREQ(last_scalar, "@#$%^&*()");
  
  reset_scalar();
}

//
// Test: Multi-line plain scalar
//
TEST(YamlScalarStyles, MultiLinePlain) {
  const char *input = "this is\n  a multi-line\n  plain scalar";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  // Should capture at least the first part
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_TRUE(last_len > 0);
  
  reset_scalar();
}

//
// Test: Literal scalar with strip chomping (|-)
//
TEST(YamlScalarStyles, LiteralScalarStrip) {
  const char *input = "|-\n  line1\n  line2\n\n\n";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  // Strip chomping should remove all trailing newlines
  // Content should be "line1\nline2" without trailing newlines
  EXPECT_TRUE(strstr(last_scalar, "line1") != NULL);
  EXPECT_TRUE(strstr(last_scalar, "line2") != NULL);
  // Should NOT end with multiple newlines
  if (last_len > 0) {
    EXPECT_NE(last_scalar[last_len - 1], '\n');
  }
  
  reset_scalar();
}

//
// Test: Literal scalar with keep chomping (|+)
//
TEST(YamlScalarStyles, LiteralScalarKeep) {
  const char *input = "|+\n  line1\n  line2\n\n\n";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  // Keep chomping should preserve all trailing newlines
  EXPECT_TRUE(strstr(last_scalar, "line1") != NULL);
  EXPECT_TRUE(strstr(last_scalar, "line2") != NULL);
  // Should have multiple trailing newlines preserved
  EXPECT_GT(last_len, 12); // At least "line1\nline2\n" + extra newlines
  
  reset_scalar();
}

//
// Test: Folded scalar with strip chomping (>-)
//
TEST(YamlScalarStyles, FoldedScalarStrip) {
  const char *input = ">-\n  line1\n  line2\n\n\n";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  // Strip chomping should remove trailing newlines
  EXPECT_TRUE(strstr(last_scalar, "line1") != NULL);
  EXPECT_TRUE(strstr(last_scalar, "line2") != NULL);
  if (last_len > 0) {
    EXPECT_NE(last_scalar[last_len - 1], '\n');
  }
  
  reset_scalar();
}

//
// Test: Folded scalar with keep chomping (>+)
//
TEST(YamlScalarStyles, FoldedScalarKeep) {
  const char *input = ">+\n  line1\n  line2\n\n\n";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  // Keep chomping should preserve trailing newlines
  EXPECT_TRUE(strstr(last_scalar, "line1") != NULL);
  EXPECT_TRUE(strstr(last_scalar, "line2") != NULL);
  // Should have some content
  EXPECT_GT(last_len, 10);
  
  reset_scalar();
}

//
// Cleanup
//
class ScalarStylesCleanup : public ::testing::Environment {
public:
  virtual ~ScalarStylesCleanup() {}
  virtual void TearDown() {
    reset_scalar();
  }
};

// Register cleanup
static ::testing::Environment* const scalar_styles_env =
    ::testing::AddGlobalTestEnvironment(new ScalarStylesCleanup);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
