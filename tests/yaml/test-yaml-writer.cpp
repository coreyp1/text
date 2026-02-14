/**
 * @file test-yaml-writer.cpp
 * @brief Tests for YAML DOM writer
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

#include <string>

static std::string write_doc(
    const GTEXT_YAML_Document *doc,
    const GTEXT_YAML_Write_Options *opts) {
  GTEXT_YAML_Sink sink;
  GTEXT_YAML_Status status = gtext_yaml_sink_buffer(&sink);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  status = gtext_yaml_write_document(doc, &sink, opts);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  std::string output = gtext_yaml_sink_buffer_data(&sink);
  gtext_yaml_sink_buffer_free(&sink);
  return output;
}

TEST(YamlWriter, ScalarPlain) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(doc, "hello", nullptr, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, scalar));

  std::string output = write_doc(doc, nullptr);
  EXPECT_EQ(output, "hello");

  gtext_yaml_free(doc);
}

TEST(YamlWriter, PrettyMapping) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, nullptr, nullptr);
  GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc, "a", nullptr, nullptr);
  GTEXT_YAML_Node *value = gtext_yaml_node_new_scalar(doc, "1", nullptr, nullptr);
  map = gtext_yaml_mapping_set(doc, map, key, value);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, map));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;

  std::string output = write_doc(doc, &opts);
  EXPECT_EQ(output, "a: 1");

  gtext_yaml_free(doc);
}

TEST(YamlWriter, PrettySequence) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, nullptr, nullptr);
  GTEXT_YAML_Node *one = gtext_yaml_node_new_scalar(doc, "a", nullptr, nullptr);
  GTEXT_YAML_Node *two = gtext_yaml_node_new_scalar(doc, "b", nullptr, nullptr);
  seq = gtext_yaml_sequence_append(doc, seq, one);
  ASSERT_NE(seq, nullptr);
  seq = gtext_yaml_sequence_append(doc, seq, two);
  ASSERT_NE(seq, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, seq));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;

  std::string output = write_doc(doc, &opts);
  EXPECT_EQ(output, "- a\n- b");

  gtext_yaml_free(doc);
}

TEST(YamlWriter, CanonicalScalarTag) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(doc, "hello", nullptr, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, scalar));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.canonical = true;

  std::string output = write_doc(doc, &opts);
  EXPECT_EQ(output, "!!str \"hello\"");

  gtext_yaml_free(doc);
}

TEST(YamlWriter, AnchorsAndAliases) {
  const char *yaml = "anchor: &a hello\nalias: *a\n";
  GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = false;

  std::string output = write_doc(doc, &opts);
  EXPECT_NE(output.find("&a"), std::string::npos);
  EXPECT_NE(output.find("*a"), std::string::npos);

  gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
