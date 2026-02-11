/**
 * @file test-yaml-utf8-comprehensive.cpp
 * @brief Comprehensive UTF-8 validation tests for YAML parser
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
// Test: Valid 1-byte UTF-8 (ASCII)
//
TEST(YamlUtf8Comprehensive, Valid1Byte) {
  const char *input = "\"Hello ABC 123\"";
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, input, strlen(input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_STREQ(last_scalar, "Hello ABC 123");
  
  reset_scalar();
}

//
// Test: Valid 2-byte UTF-8 (é)
//
TEST(YamlUtf8Comprehensive, Valid2Byte) {
  // é = 0xC3 0xA9
  const unsigned char input[] = {'"', 0xC3, 0xA9, '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_EQ(last_len, 2);  // é is 2 bytes in UTF-8
  
  reset_scalar();
}

//
// Test: Valid 3-byte UTF-8 (☺)
//
TEST(YamlUtf8Comprehensive, Valid3Byte) {
  // ☺ = 0xE2 0x98 0xBA
  const unsigned char input[] = {'"', 0xE2, 0x98, 0xBA, '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_EQ(last_len, 3);  // ☺ is 3 bytes in UTF-8
  
  reset_scalar();
}

//
// Test: Valid 4-byte UTF-8 (😀)
//
TEST(YamlUtf8Comprehensive, Valid4Byte) {
  // 😀 = 0xF0 0x9F 0x98 0x80
  const unsigned char input[] = {'"', 0xF0, 0x9F, 0x98, 0x80, '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_EQ(last_len, 4);  // 😀 is 4 bytes in UTF-8
  
  reset_scalar();
}

//
// Test: Mixed ASCII and UTF-8
//
TEST(YamlUtf8Comprehensive, MixedAsciiAndUtf8) {
  // "Hello é ☺"
  const unsigned char input[] = {'"', 'H', 'e', 'l', 'l', 'o', ' ', 
                                  0xC3, 0xA9, ' ', 0xE2, 0x98, 0xBA, '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_EQ(last_len, 12);  // "Hello " (6) + é (2) + " " (1) + ☺ (3)
  
  reset_scalar();
}

//
// Test: Invalid UTF-8 - truncated 2-byte sequence
// 0xC3 starts a 2-byte sequence but is followed by ASCII 'x' instead of continuation byte
//
TEST(YamlUtf8Comprehensive, InvalidTruncated2Byte) {
  const unsigned char input[] = {'"', 0xC3, 'x', '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  if (st == GTEXT_YAML_OK) {
    st = gtext_yaml_stream_finish(s);
  }
  
  // Should reject invalid UTF-8
  EXPECT_NE(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  reset_scalar();
}

//
// Test: Invalid UTF-8 - truncated 3-byte sequence
// 0xE2 0x98 starts a 3-byte sequence but lacks the final byte
//
TEST(YamlUtf8Comprehensive, InvalidTruncated3Byte) {
  const unsigned char input[] = {'"', 0xE2, 0x98, 'x', '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  if (st == GTEXT_YAML_OK) {
    st = gtext_yaml_stream_finish(s);
  }
  
  // Should reject invalid UTF-8
  EXPECT_NE(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  reset_scalar();
}

//
// Test: Invalid UTF-8 - overlong encoding
// 0xC0 0x80 is an overlong encoding of NULL (should be 0x00)
//
TEST(YamlUtf8Comprehensive, InvalidOverlong) {
  const unsigned char input[] = {'"', 0xC0, 0x80, '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  if (st == GTEXT_YAML_OK) {
    st = gtext_yaml_stream_finish(s);
  }
  
  // Should reject overlong encoding
  EXPECT_NE(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  reset_scalar();
}

//
// Test: Invalid UTF-8 - lone continuation byte
// 0x80 is a continuation byte without a starter byte
//
TEST(YamlUtf8Comprehensive, InvalidLoneContinuation) {
  const unsigned char input[] = {'"', 'a', 0x80, 'b', '"', 0};
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  if (st == GTEXT_YAML_OK) {
    st = gtext_yaml_stream_finish(s);
  }
  
  // Should reject lone continuation byte
  EXPECT_NE(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  reset_scalar();
}

//
// Test: Chinese characters (3-byte UTF-8)
//
TEST(YamlUtf8Comprehensive, ChineseCharacters) {
  // "中文" = 0xE4 0xB8 0xAD 0xE6 0x96 0x87
  const unsigned char input[] = {
    '"',
    0xE4, 0xB8, 0xAD,  // 中
    0xE6, 0x96, 0x87,  // 文
    '"', 0
  };
  reset_scalar();
  
  GTEXT_YAML_Stream *s = gtext_yaml_stream_new(NULL, capture_cb, NULL);
  ASSERT_NE(s, nullptr);
  
  GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, (const char *)input, strlen((const char *)input));
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  st = gtext_yaml_stream_finish(s);
  EXPECT_EQ(st, GTEXT_YAML_OK);
  
  gtext_yaml_stream_free(s);
  
  ASSERT_NE(last_scalar, nullptr);
  EXPECT_EQ(last_len, 6);  // 中 (3 bytes) + 文 (3 bytes)
  
  reset_scalar();
}
