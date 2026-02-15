/**
 * @file test-yaml-warnings.cpp
 * @brief Tests for YAML warning callback behavior.
 */

#include <gtest/gtest.h>
#include <vector>
#include <cstring>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

struct warning_capture {
  std::vector<GTEXT_YAML_Warning_Code> codes;
};

static void warning_callback(const GTEXT_YAML_Warning *warning, void *user) {
  if (!warning || !user) {
    return;
  }
  warning_capture *cap = static_cast<warning_capture *>(user);
  cap->codes.push_back(warning->code);
}

static bool has_warning(
    const warning_capture &cap,
    GTEXT_YAML_Warning_Code code) {
  for (size_t i = 0; i < cap.codes.size(); i++) {
    if (cap.codes[i] == code) {
      return true;
    }
  }
  return false;
}

TEST(YamlWarnings, Yaml11Boolean) {
  const char *yaml = "key: yes\n";
  warning_capture cap;

  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.warning_callback = warning_callback;
  opts.warning_user_data = &cap;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
  ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

  EXPECT_TRUE(has_warning(cap, GTEXT_YAML_WARNING_YAML11_BOOL));

  const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
  ASSERT_NE(root, nullptr);
  const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
  ASSERT_NE(value, nullptr);
  EXPECT_STREQ(gtext_yaml_node_as_string(value), "yes");

  gtext_yaml_free(doc);
}

TEST(YamlWarnings, Yaml11Octal) {
  const char *yaml = "key: 0123\n";
  warning_capture cap;

  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.warning_callback = warning_callback;
  opts.warning_user_data = &cap;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
  ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

  EXPECT_TRUE(has_warning(cap, GTEXT_YAML_WARNING_YAML11_OCTAL));

  const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
  ASSERT_NE(root, nullptr);
  const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
  ASSERT_NE(value, nullptr);
  EXPECT_STREQ(gtext_yaml_node_as_string(value), "0123");

  gtext_yaml_free(doc);
}

TEST(YamlWarnings, Yaml11Sexagesimal) {
  const char *yaml = "key: 12:34:56\n";
  warning_capture cap;

  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.warning_callback = warning_callback;
  opts.warning_user_data = &cap;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
  ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

  EXPECT_TRUE(has_warning(cap, GTEXT_YAML_WARNING_YAML11_SEXAGESIMAL));

  const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
  ASSERT_NE(root, nullptr);
  const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
  ASSERT_NE(value, nullptr);
  EXPECT_STREQ(gtext_yaml_node_as_string(value), "12:34:56");

  gtext_yaml_free(doc);
}

TEST(YamlWarnings, DuplicateKey) {
  const char *yaml = "a: 1\na: 2\n";
  warning_capture cap;

  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.warning_callback = warning_callback;
  opts.warning_user_data = &cap;
  opts.dupkeys = GTEXT_YAML_DUPKEY_LAST_WINS;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
  ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

  EXPECT_TRUE(has_warning(cap, GTEXT_YAML_WARNING_DUPLICATE_KEY));

  const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
  ASSERT_NE(root, nullptr);
  const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "a");
  ASSERT_NE(value, nullptr);
  EXPECT_STREQ(gtext_yaml_node_as_string(value), "2");

  gtext_yaml_free(doc);
}

TEST(YamlWarnings, WarningsAsErrors) {
  const char *yaml = "key: yes\n";

  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.warnings_as_errors = true;

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
  EXPECT_EQ(doc, nullptr);
  EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
  EXPECT_NE(err.message, nullptr);
}

TEST(YamlWarnings, WarningMaskSuppresses) {
  const char *yaml = "key: yes\n";
  warning_capture cap;

  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.warning_callback = warning_callback;
  opts.warning_user_data = &cap;
  opts.warning_mask = GTEXT_YAML_WARNING_MASK(GTEXT_YAML_WARNING_YAML11_BOOL);

  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
  ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");

  EXPECT_FALSE(has_warning(cap, GTEXT_YAML_WARNING_YAML11_BOOL));

  gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
