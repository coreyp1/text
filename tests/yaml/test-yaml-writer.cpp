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

static GTEXT_YAML_Document *build_sample_mapping_doc() {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  if (!doc) return nullptr;

  GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, nullptr, nullptr);
  GTEXT_YAML_Node *items_key = gtext_yaml_node_new_scalar(doc, "items", nullptr, nullptr);
  GTEXT_YAML_Node *settings_key = gtext_yaml_node_new_scalar(doc, "settings", nullptr, nullptr);
  GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, nullptr, nullptr);
  GTEXT_YAML_Node *one = gtext_yaml_node_new_scalar(doc, "a", nullptr, nullptr);
  GTEXT_YAML_Node *two = gtext_yaml_node_new_scalar(doc, "b", nullptr, nullptr);
  seq = gtext_yaml_sequence_append(doc, seq, one);
  seq = gtext_yaml_sequence_append(doc, seq, two);

  GTEXT_YAML_Node *settings = gtext_yaml_node_new_mapping(doc, nullptr, nullptr);
  GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc, "key", nullptr, nullptr);
  GTEXT_YAML_Node *value = gtext_yaml_node_new_scalar(doc, "value", nullptr, nullptr);
  settings = gtext_yaml_mapping_set(doc, settings, key, value);

  map = gtext_yaml_mapping_set(doc, map, items_key, seq);
  map = gtext_yaml_mapping_set(doc, map, settings_key, settings);
  if (!map || !gtext_yaml_document_set_root(doc, map)) {
    gtext_yaml_free(doc);
    return nullptr;
  }

  return doc;
}

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

TEST(YamlWriter, IndentWidth) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, nullptr, nullptr);
  GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc, "items", nullptr, nullptr);
  GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, nullptr, nullptr);
  GTEXT_YAML_Node *one = gtext_yaml_node_new_scalar(doc, "a", nullptr, nullptr);
  seq = gtext_yaml_sequence_append(doc, seq, one);
  map = gtext_yaml_mapping_set(doc, map, key, seq);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, map));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.indent_spaces = 4;

  std::string output = write_doc(doc, &opts);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/indent-4.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_free(doc);
}

TEST(YamlWriter, ScalarSingleQuoted) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(doc, "hello world", nullptr, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, scalar));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED;

  std::string output = write_doc(doc, &opts);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/scalar-single-quoted.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_free(doc);
}

TEST(YamlWriter, FoldedLineWidth) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(doc, "one two three", nullptr, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, scalar));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_FOLDED;
  opts.line_width = 6;

  std::string output = write_doc(doc, &opts);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/folded-line-width.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_free(doc);
}

TEST(YamlWriter, FlowStyleForced) {
  GTEXT_YAML_Document *doc = build_sample_mapping_doc();
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.flow_style = GTEXT_YAML_FLOW_STYLE_FLOW;

  std::string output = write_doc(doc, &opts);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/flow-style.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_free(doc);
}

TEST(YamlWriter, BlockStyleForced) {
  GTEXT_YAML_Document *doc = build_sample_mapping_doc();
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.flow_style = GTEXT_YAML_FLOW_STYLE_BLOCK;

  std::string output = write_doc(doc, &opts);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/block-style.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_free(doc);
}

TEST(YamlWriter, ScalarLiteral) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(
      doc, "line 1\nline 2", nullptr, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, scalar));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.scalar_style = GTEXT_YAML_SCALAR_STYLE_LITERAL;
  opts.pretty = true;

  std::string output = write_doc(doc, &opts);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/literal-scalar.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

  gtext_yaml_free(doc);
}

TEST(YamlWriter, FoldedAutoLineWidth) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(
      doc, "one two three", nullptr, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, scalar));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.line_width = 6;

    std::string output = write_doc(doc, &opts);
    std::string expected = read_file(
      get_test_data_dir() + "/formatting/auto-fold-quoted.yaml");
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(output, expected);

  gtext_yaml_free(doc);
}

TEST(YamlWriter, NewlineCrlf) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, nullptr, nullptr);
  GTEXT_YAML_Node *key_a = gtext_yaml_node_new_scalar(doc, "a", nullptr, nullptr);
  GTEXT_YAML_Node *val_a = gtext_yaml_node_new_scalar(doc, "1", nullptr, nullptr);
  GTEXT_YAML_Node *key_b = gtext_yaml_node_new_scalar(doc, "b", nullptr, nullptr);
  GTEXT_YAML_Node *val_b = gtext_yaml_node_new_scalar(doc, "2", nullptr, nullptr);
  map = gtext_yaml_mapping_set(doc, map, key_a, val_a);
  map = gtext_yaml_mapping_set(doc, map, key_b, val_b);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, map));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.newline = "\r\n";

  std::string output = write_doc(doc, &opts);
  EXPECT_EQ(output, "a: 1\r\nb: 2");

  gtext_yaml_free(doc);
}

TEST(YamlWriter, TrailingNewline) {
  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(doc, "hello", nullptr, nullptr);
  ASSERT_NE(scalar, nullptr);
  EXPECT_TRUE(gtext_yaml_document_set_root(doc, scalar));

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.trailing_newline = true;

  std::string output = write_doc(doc, &opts);
  std::string expected = read_file(
      get_test_data_dir() + "/formatting/trailing-newline.yaml");
  ASSERT_FALSE(expected.empty());
  EXPECT_EQ(output, expected);

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
