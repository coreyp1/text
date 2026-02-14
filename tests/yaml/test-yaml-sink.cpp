/**
 * @file test-yaml-sink.cpp
 * @brief Tests for YAML sink helpers
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

TEST(YamlSink, GrowableBuffer) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  EXPECT_EQ(gtext_yaml_sink_buffer_size(&sink), 0u);
  const char * data = gtext_yaml_sink_buffer_data(&sink);
  ASSERT_NE(data, nullptr);
  EXPECT_STREQ(data, "");

  const char * test1 = "Hello";
  int result = sink.write(sink.user, test1, strlen(test1));
  EXPECT_EQ(result, 0);
  EXPECT_EQ(gtext_yaml_sink_buffer_size(&sink), 5u);
  EXPECT_STREQ(gtext_yaml_sink_buffer_data(&sink), "Hello");

  const char * test2 = ", World!";
  result = sink.write(sink.user, test2, strlen(test2));
  EXPECT_EQ(result, 0);
  EXPECT_EQ(gtext_yaml_sink_buffer_size(&sink), 13u);
  EXPECT_STREQ(gtext_yaml_sink_buffer_data(&sink), "Hello, World!");

  std::string large_data(1000, 'A');
  result = sink.write(sink.user, large_data.c_str(), large_data.size());
  EXPECT_EQ(result, 0);
  EXPECT_EQ(gtext_yaml_sink_buffer_size(&sink), 1013u);

  data = gtext_yaml_sink_buffer_data(&sink);
  EXPECT_EQ(strncmp(data, "Hello, World!", 13), 0);
  EXPECT_EQ(data[1012], 'A');

  gtext_yaml_sink_buffer_free(&sink);
  EXPECT_EQ(sink.write, nullptr);
  EXPECT_EQ(sink.user, nullptr);
}

TEST(YamlSink, FixedBuffer) {
  char buffer[64];
  GTEXT_YAML_Sink sink;

  GTEXT_YAML_Status status = gtext_yaml_sink_fixed_buffer(&sink, buffer, sizeof(buffer));
  EXPECT_EQ(status, GTEXT_YAML_OK);

  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_used(&sink), 0u);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_truncated(&sink), false);
  EXPECT_STREQ(buffer, "");

  const char * test1 = "Hello";
  int result = sink.write(sink.user, test1, strlen(test1));
  EXPECT_EQ(result, 0);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_used(&sink), 5u);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_truncated(&sink), false);
  EXPECT_STREQ(buffer, "Hello");

  const char * test2 = ", World!";
  result = sink.write(sink.user, test2, strlen(test2));
  EXPECT_EQ(result, 0);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_used(&sink), 13u);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_truncated(&sink), false);
  EXPECT_STREQ(buffer, "Hello, World!");

  const char * test3 = " This fits";
  result = sink.write(sink.user, test3, strlen(test3));
  EXPECT_EQ(result, 0);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_used(&sink), 23u);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_truncated(&sink), false);

  const char * test4 = " This is way too long and will definitely be truncated";
  result = sink.write(sink.user, test4, strlen(test4));
  EXPECT_NE(result, 0);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_truncated(&sink), true);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_used(&sink), sizeof(buffer) - 1);

  gtext_yaml_sink_fixed_buffer_free(&sink);
}

TEST(YamlSink, FixedBufferEdgeCases) {
  char tiny_buffer[1];
  GTEXT_YAML_Sink sink;

  GTEXT_YAML_Status status = gtext_yaml_sink_fixed_buffer(&sink, tiny_buffer, 1);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  const char * test = "X";
  int result = sink.write(sink.user, test, strlen(test));
  EXPECT_NE(result, 0);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_truncated(&sink), true);
  EXPECT_EQ(gtext_yaml_sink_fixed_buffer_used(&sink), 0u);
  EXPECT_STREQ(tiny_buffer, "");

  gtext_yaml_sink_fixed_buffer_free(&sink);

  status = gtext_yaml_sink_fixed_buffer(nullptr, tiny_buffer, 1);
  EXPECT_EQ(status, GTEXT_YAML_E_INVALID);

  status = gtext_yaml_sink_fixed_buffer(&sink, nullptr, 1);
  EXPECT_EQ(status, GTEXT_YAML_E_INVALID);

  status = gtext_yaml_sink_fixed_buffer(&sink, tiny_buffer, 0);
  EXPECT_EQ(status, GTEXT_YAML_E_INVALID);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
