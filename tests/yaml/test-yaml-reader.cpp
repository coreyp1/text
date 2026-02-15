#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_internal.h>
#include <string.h>
}

TEST(YamlReader, PositionTracking) {
  const char * s = "line1\nline2\nlast";
  GTEXT_YAML_CharReader * r = gtext_yaml_char_reader_new(s, strlen(s));
  ASSERT_NE(r, nullptr);

  while (gtext_yaml_char_reader_peek(r) != -1) {
    int c = gtext_yaml_char_reader_consume(r);
    if (c == -1) break;
    // Check if we've consumed 6 characters (offset 6)
    if (gtext_yaml_char_reader_offset(r) == 6) break;
  }

  int line, col;
  gtext_yaml_char_reader_position(r, &line, &col);
  gtext_yaml_char_reader_free(r);

  EXPECT_EQ(line, 2);
  EXPECT_EQ(col, 1);
}

TEST(YamlReader, PositionTrackingCrLf) {
  const char * s = "line1\r\nline2";
  GTEXT_YAML_CharReader * r = gtext_yaml_char_reader_new(s, strlen(s));
  ASSERT_NE(r, nullptr);

  while (gtext_yaml_char_reader_peek(r) != -1) {
    int c = gtext_yaml_char_reader_consume(r);
    if (c == -1) break;
    if (gtext_yaml_char_reader_offset(r) == 7) break;
  }

  int line, col;
  gtext_yaml_char_reader_position(r, &line, &col);
  gtext_yaml_char_reader_free(r);

  EXPECT_EQ(line, 2);
  EXPECT_EQ(col, 1);
}

TEST(YamlReader, PositionTrackingCr) {
  const char * s = "line1\rline2";
  GTEXT_YAML_CharReader * r = gtext_yaml_char_reader_new(s, strlen(s));
  ASSERT_NE(r, nullptr);

  while (gtext_yaml_char_reader_peek(r) != -1) {
    int c = gtext_yaml_char_reader_consume(r);
    if (c == -1) break;
    if (gtext_yaml_char_reader_offset(r) == 6) break;
  }

  int line, col;
  gtext_yaml_char_reader_position(r, &line, &col);
  gtext_yaml_char_reader_free(r);

  EXPECT_EQ(line, 2);
  EXPECT_EQ(col, 1);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
