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

/* 6.8.1 splits the version question in two. A minor version this parser does
 * not implement is still parsed - the minor versions are meant to stay
 * compatible - but the document should not go by in silence, so it warns. A
 * *major* version it does not implement is refused outright, which is an
 * error rather than a warning and is pinned in the spec corpus.
 *
 * Checked through the callback rather than through the parse: "%YAML 1.7"
 * parses either way, so a test that only asked whether it parsed would have
 * passed with no warning code at all. */
TEST(YamlWarnings, NewerMinorVersionWarnsAndStillParses) {
  struct Case { const char *yaml; bool warns; };
  const Case cases[] = {
    {"%YAML 1.2\n---\nv\n", false},
    {"%YAML 1.1\n---\nv\n", false},
    {"%YAML 1.3\n---\nv\n", true},
    {"%YAML 1.7\n---\nv\n", true},
    {"%YAML 1.12345\n---\nv\n", true},
  };

  for (const Case &c : cases) {
    warning_capture cap;
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.warning_callback = warning_callback;
    opts.warning_user_data = &cap;

    GTEXT_YAML_Error err;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Document *doc =
        gtext_yaml_parse(c.yaml, strlen(c.yaml), &opts, &err);
    ASSERT_NE(doc, nullptr) << c.yaml
        << ": " << (err.message ? err.message : "parse failed");
    EXPECT_EQ(has_warning(cap, GTEXT_YAML_WARNING_YAML_VERSION), c.warns)
        << c.yaml;
    gtext_yaml_free(doc);
  }
}

TEST(YamlWarnings, TheVersionWarningObeysTheMaskAndTheErrorPromotion) {
  const char *yaml = "%YAML 1.7\n---\nv\n";

  /* Masked off: parsed, and the callback never hears about it. */
  {
    warning_capture cap;
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.warning_callback = warning_callback;
    opts.warning_user_data = &cap;
    opts.warning_mask =
        GTEXT_YAML_WARNING_MASK(GTEXT_YAML_WARNING_YAML_VERSION);

    GTEXT_YAML_Error err;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Document *doc =
        gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
    ASSERT_NE(doc, nullptr) << (err.message ? err.message : "parse failed");
    EXPECT_FALSE(has_warning(cap, GTEXT_YAML_WARNING_YAML_VERSION));
    gtext_yaml_free(doc);
  }

  /* Promoted: refused, with the warning's own message. */
  {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    opts.warnings_as_errors = true;

    GTEXT_YAML_Error err;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Document *doc =
        gtext_yaml_parse(yaml, strlen(yaml), &opts, &err);
    EXPECT_EQ(doc, nullptr);
    if (doc) { gtext_yaml_free(doc); return; }
    ASSERT_NE(err.message, nullptr);
    EXPECT_NE(strstr(err.message, "minor version"), nullptr) << err.message;
  }
}
