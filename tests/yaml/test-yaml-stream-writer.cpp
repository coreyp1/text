/**
 * @file test-yaml-stream-writer.cpp
 * @brief Tests for YAML streaming writer
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

#include <string>
#include <fstream>

static std::string read_file(const std::string & path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return "";
  }
  std::string content(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return content;
}

static std::string get_test_data_dir() {
  const char * test_dir = getenv("TEST_DATA_DIR");
  if (test_dir) {
    return std::string(test_dir);
  }

  return "tests/data/yaml";
}

static std::string write_events(
    const std::vector<GTEXT_YAML_Event> &events,
    const GTEXT_YAML_Write_Options *opts) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Writer *writer = gtext_yaml_writer_new(sink, opts);
  EXPECT_NE(writer, nullptr);

  for (const auto &ev : events) {
    status = gtext_yaml_writer_event(writer, &ev);
    EXPECT_EQ(status, GTEXT_YAML_OK);
  }

  status = gtext_yaml_writer_finish(writer);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  std::string output = gtext_yaml_sink_buffer_data(&sink);
  gtext_yaml_writer_free(writer);
  gtext_yaml_sink_buffer_free(&sink);
  return output;
}

TEST(YamlStreamWriter, ScalarDocument) {
  std::vector<GTEXT_YAML_Event> events;
  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "hello";
  ev.data.scalar.len = 5;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  events.push_back(ev);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.trailing_newline = true;

  std::string output = write_events(events, &opts);
  EXPECT_EQ(output, "---\nhello\n");
}

TEST(YamlStreamWriter, ScalarSingleQuoted) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  ASSERT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED;
  opts.trailing_newline = true;

  GTEXT_YAML_Writer *writer = gtext_yaml_writer_new(sink, &opts);
  ASSERT_NE(writer, nullptr);

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "hello world";
  ev.data.scalar.len = 11;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  EXPECT_EQ(gtext_yaml_writer_finish(writer), GTEXT_YAML_OK);

  std::string output = gtext_yaml_sink_buffer_data(&sink);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/stream-single-quoted.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_writer_free(writer);
  gtext_yaml_sink_buffer_free(&sink);
}

TEST(YamlStreamWriter, SequenceDocument) {
  std::vector<GTEXT_YAML_Event> events;
  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SEQUENCE_START;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "a";
  ev.data.scalar.len = 1;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "b";
  ev.data.scalar.len = 1;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SEQUENCE_END;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  events.push_back(ev);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.trailing_newline = true;

  std::string output = write_events(events, &opts);
  EXPECT_EQ(output, "---\n[a, b]\n");
}

TEST(YamlStreamWriter, MappingDocument) {
  std::vector<GTEXT_YAML_Event> events;
  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_MAPPING_START;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "key";
  ev.data.scalar.len = 3;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "value";
  ev.data.scalar.len = 5;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_MAPPING_END;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  events.push_back(ev);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.trailing_newline = true;

  std::string output = write_events(events, &opts);
  EXPECT_EQ(output, "---\n{key: value}\n");
}

TEST(YamlStreamWriter, AnchorsAndAliases) {
  std::vector<GTEXT_YAML_Event> events;
  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_MAPPING_START;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "a";
  ev.data.scalar.len = 1;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "hello";
  ev.data.scalar.len = 5;
  ev.anchor = "a1";
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "b";
  ev.data.scalar.len = 1;
  ev.anchor = nullptr;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_ALIAS;
  ev.data.alias_name = "a1";
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_MAPPING_END;
  events.push_back(ev);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  events.push_back(ev);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.trailing_newline = true;

  std::string output = write_events(events, &opts);
  EXPECT_NE(output.find("&a1"), std::string::npos);
  EXPECT_NE(output.find("*a1"), std::string::npos);
}

TEST(YamlStreamWriter, InvalidSequenceEnd) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  ASSERT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Writer *writer = gtext_yaml_writer_new(sink, nullptr);
  ASSERT_NE(writer, nullptr);

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_SEQUENCE_END;

  status = gtext_yaml_writer_event(writer, &ev);
  EXPECT_EQ(status, GTEXT_YAML_E_STATE);

  gtext_yaml_writer_free(writer);
  gtext_yaml_sink_buffer_free(&sink);
}

TEST(YamlStreamWriter, BlockSequenceDocument) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  ASSERT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;
  opts.trailing_newline = true;

  GTEXT_YAML_Writer *writer = gtext_yaml_writer_new(sink, &opts);
  ASSERT_NE(writer, nullptr);

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SEQUENCE_START;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "a";
  ev.data.scalar.len = 1;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.data.scalar.ptr = "b";
  ev.data.scalar.len = 1;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SEQUENCE_END;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  EXPECT_EQ(gtext_yaml_writer_finish(writer), GTEXT_YAML_OK);

  std::string output = gtext_yaml_sink_buffer_data(&sink);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/stream-block-seq.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_writer_free(writer);
  gtext_yaml_sink_buffer_free(&sink);
}

TEST(YamlStreamWriter, BlockMappingDocument) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  ASSERT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;
  opts.trailing_newline = true;

  GTEXT_YAML_Writer *writer = gtext_yaml_writer_new(sink, &opts);
  ASSERT_NE(writer, nullptr);

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_MAPPING_START;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "a";
  ev.data.scalar.len = 1;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.data.scalar.ptr = "1";
  ev.data.scalar.len = 1;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.data.scalar.ptr = "b";
  ev.data.scalar.len = 1;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SEQUENCE_START;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "2";
  ev.data.scalar.len = 1;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SEQUENCE_END;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_MAPPING_END;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  EXPECT_EQ(gtext_yaml_writer_finish(writer), GTEXT_YAML_OK);

  std::string output = gtext_yaml_sink_buffer_data(&sink);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/stream-block-map.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_writer_free(writer);
  gtext_yaml_sink_buffer_free(&sink);
}

TEST(YamlStreamWriter, LiteralScalarDocument) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  ASSERT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_LITERAL;
  opts.trailing_newline = true;

  GTEXT_YAML_Writer *writer = gtext_yaml_writer_new(sink, &opts);
  ASSERT_NE(writer, nullptr);

  GTEXT_YAML_Event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = GTEXT_YAML_EVENT_DOCUMENT_START;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_SCALAR;
  ev.data.scalar.ptr = "line 1\nline 2";
  ev.data.scalar.len = 13;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  ev.type = GTEXT_YAML_EVENT_DOCUMENT_END;
  EXPECT_EQ(gtext_yaml_writer_event(writer, &ev), GTEXT_YAML_OK);

  EXPECT_EQ(gtext_yaml_writer_finish(writer), GTEXT_YAML_OK);

  std::string output = gtext_yaml_sink_buffer_data(&sink);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/stream-literal-scalar.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_writer_free(writer);
  gtext_yaml_sink_buffer_free(&sink);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
