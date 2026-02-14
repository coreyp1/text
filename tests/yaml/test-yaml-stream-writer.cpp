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

static std::string write_events(const std::vector<GTEXT_YAML_Event> &events) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  GTEXT_YAML_Writer *writer = gtext_yaml_writer_new(sink, nullptr);
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

  std::string output = write_events(events);
  EXPECT_EQ(output, "---\nhello\n");
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

  std::string output = write_events(events);
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

  std::string output = write_events(events);
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

  std::string output = write_events(events);
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
