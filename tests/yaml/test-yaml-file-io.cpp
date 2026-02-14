/**
 * @file test-yaml-file-io.cpp
 * @brief Tests for YAML file I/O helpers
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

#include <string>
#include <unistd.h>

static std::string make_temp_path(const char *suffix) {
  std::string path = "/tmp/ghoti_yaml_";
  path += suffix;
  path += "_";
  path += std::to_string(getpid());
  return path;
}

TEST(YamlFileIO, ParseFile) {
  std::string path = make_temp_path("parse");
  const char *contents = "key: value\n";

  FILE *file = fopen(path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  fwrite(contents, 1, strlen(contents), file);
  fclose(file);

  GTEXT_YAML_Document *doc = gtext_yaml_parse_file(path.c_str(), nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
  ASSERT_NE(root, nullptr);
  const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
  ASSERT_NE(value, nullptr);
  EXPECT_STREQ(gtext_yaml_node_as_string(value), "value");

  gtext_yaml_free(doc);
  remove(path.c_str());
}

TEST(YamlFileIO, ParseFileAll) {
  std::string path = make_temp_path("multi");
  const char *contents = "---\nfirst: 1\n---\nsecond: 2\n";

  FILE *file = fopen(path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  fwrite(contents, 1, strlen(contents), file);
  fclose(file);

  GTEXT_YAML_Document **docs = nullptr;
  size_t count = 0;
  GTEXT_YAML_Status status = gtext_yaml_parse_file_all(
      path.c_str(), nullptr, &docs, &count, nullptr);
  EXPECT_EQ(status, GTEXT_YAML_OK);
  ASSERT_NE(docs, nullptr);
  ASSERT_EQ(count, 2u);

  gtext_yaml_free(docs[0]);
  gtext_yaml_free(docs[1]);
  free(docs);
  remove(path.c_str());
}

TEST(YamlFileIO, WriteFile) {
  std::string path = make_temp_path("write");

  GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
  ASSERT_NE(doc, nullptr);
  GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, nullptr, nullptr);
  GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc, "a", nullptr, nullptr);
  GTEXT_YAML_Node *val = gtext_yaml_node_new_scalar(doc, "1", nullptr, nullptr);
  map = gtext_yaml_mapping_set(doc, map, key, val);
  ASSERT_NE(map, nullptr);
  ASSERT_TRUE(gtext_yaml_document_set_root(doc, map));

  GTEXT_YAML_Status status = gtext_yaml_write_file(path.c_str(), doc, nullptr, nullptr);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  FILE *file = fopen(path.c_str(), "rb");
  ASSERT_NE(file, nullptr);
  char buffer[64];
  size_t read_bytes = fread(buffer, 1, sizeof(buffer) - 1, file);
  buffer[read_bytes] = '\0';
  fclose(file);

  EXPECT_EQ(std::string(buffer), "{a: 1}");

  gtext_yaml_free(doc);
  remove(path.c_str());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
