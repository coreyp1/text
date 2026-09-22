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

#ifndef _WIN32
#include <sys/stat.h>
#endif

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

TEST(YamlFileIO, ParseFileNullPath) {
  GTEXT_YAML_Error err = {};
  GTEXT_YAML_Document *doc = gtext_yaml_parse_file(nullptr, nullptr, &err);
  EXPECT_EQ(doc, nullptr);
  EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlFileIO, ParseFileAllInvalidArgs) {
  GTEXT_YAML_Error err = {};
  GTEXT_YAML_Status status = gtext_yaml_parse_file_all(nullptr, nullptr, nullptr, nullptr, &err);
  EXPECT_EQ(status, GTEXT_YAML_E_INVALID);
  EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlFileIO, WriteFileInvalidArgs) {
  GTEXT_YAML_Error err = {};
  GTEXT_YAML_Status status = gtext_yaml_write_file(nullptr, nullptr, nullptr, &err);
  EXPECT_EQ(status, GTEXT_YAML_E_INVALID);
  EXPECT_EQ(err.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlFileIO, PreserveLineEndingsOnWrite) {
  std::string input_path = make_temp_path("newline_in");
  std::string output_path = make_temp_path("newline_out");
  const char *contents = "a: 1\r\nb: 2\r\n";

  FILE *file = fopen(input_path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  fwrite(contents, 1, strlen(contents), file);
  fclose(file);

  GTEXT_YAML_Document *doc = gtext_yaml_parse_file(input_path.c_str(), nullptr, nullptr);
  ASSERT_NE(doc, nullptr);

  GTEXT_YAML_Write_Options opts = gtext_yaml_write_options_default();
  opts.pretty = true;
  opts.newline = NULL;

  GTEXT_YAML_Status status = gtext_yaml_write_file(output_path.c_str(), doc, &opts, nullptr);
  EXPECT_EQ(status, GTEXT_YAML_OK);

  FILE *out = fopen(output_path.c_str(), "rb");
  ASSERT_NE(out, nullptr);
  char buffer[128];
  size_t read_bytes = fread(buffer, 1, sizeof(buffer) - 1, out);
  buffer[read_bytes] = '\0';
  fclose(out);

  std::string output(buffer);
  EXPECT_NE(output.find("\r\n"), std::string::npos);

  gtext_yaml_free(doc);
  remove(input_path.c_str());
  remove(output_path.c_str());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#ifndef _WIN32
/* Writing a file must not change who can read it.
 *
 * gtext_file_write_atomic() writes a temporary and renames it over the
 * destination, and a temporary is owner-only. Until cutil's
 * gcu_file_temp_commit() grew a permissions argument, the destination
 * inherited that and nobody chose it - so every save of a 644 configuration
 * file quietly narrowed it to 600, and nothing here would have noticed.
 *
 * GCU_FILE_PERMS_PRESERVE is what that call passes now. The two wrong
 * answers are wrong in opposite directions and one of them is the one a
 * caller reaches by reflex: PRIVATE is the zero value and narrows a config
 * nobody asked to narrow, DEFAULT widens one somebody deliberately ran
 * chmod 600 on. Replacing a file is not the same act as creating it.
 *
 * POSIX only. The mode bits have no Windows equivalent, and the permissions
 * question there is a different one rather than the same one spelled
 * differently. */
static mode_t mode_of(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0;
}

TEST(YamlFileIO, WritingAFileKeepsThePermissionsItAlreadyHad) {
  /* Both directions, so neither wrong answer passes: a narrow file must not
     be widened and a wide one must not be narrowed. */
  const mode_t modes[] = { 0600, 0640, 0644, 0664 };

  const char *source = "key: value\n";
  for (mode_t want : modes) {
    std::string path = make_temp_path("perms");
    FILE *file = fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    fputs("old: 1\n", file);
    fclose(file);
    /* Set it explicitly rather than trusting the umask, which differs
       between machines and would make this test's meaning depend on it. */
    ASSERT_EQ(chmod(path.c_str(), want), 0) << path;
    ASSERT_EQ(mode_of(path), want) << path;

    GTEXT_YAML_Error err;
    memset(&err, 0, sizeof(err));
    GTEXT_YAML_Document *doc =
      gtext_yaml_parse(source, strlen(source), nullptr, &err);
    ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
    gtext_yaml_error_free(&err);

    memset(&err, 0, sizeof(err));
    EXPECT_EQ(gtext_yaml_write_file(path.c_str(), doc, nullptr, &err),
      GTEXT_YAML_OK) << (err.message ? err.message : "");
    gtext_yaml_error_free(&err);
    gtext_yaml_free(doc);

    EXPECT_EQ(mode_of(path), want)
      << "writing over a file of mode " << std::oct << want
      << " left it " << mode_of(path);

    /* And it has to still be the document that was written, so a test that
       preserved the mode by not writing anything would fail here. */
    GTEXT_YAML_Document *back =
      gtext_yaml_parse_file(path.c_str(), nullptr, nullptr);
    ASSERT_NE(back, nullptr);
    const GTEXT_YAML_Node *value =
      gtext_yaml_mapping_get(gtext_yaml_document_root(back), "key");
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(gtext_yaml_node_as_string(value), "value");
    gtext_yaml_free(back);

    remove(path.c_str());
  }
}

TEST(YamlFileIO, WritingAFileThatDoesNotExistUsesTheOrdinaryDefault) {
  /* PRESERVE falls back to what an ordinary fopen() would have given when
     there is no destination yet - which is the umask's business, so this
     sets one rather than asserting whatever the machine happens to have. */
  const mode_t saved = umask(022);
  std::string path = make_temp_path("perms_new");
  remove(path.c_str());

  const char *source = "key: value\n";
  GTEXT_YAML_Error err;
  memset(&err, 0, sizeof(err));
  GTEXT_YAML_Document *doc =
    gtext_yaml_parse(source, strlen(source), nullptr, &err);
  ASSERT_NE(doc, nullptr) << (err.message ? err.message : "");
  gtext_yaml_error_free(&err);

  memset(&err, 0, sizeof(err));
  EXPECT_EQ(gtext_yaml_write_file(path.c_str(), doc, nullptr, &err),
    GTEXT_YAML_OK) << (err.message ? err.message : "");
  gtext_yaml_error_free(&err);
  gtext_yaml_free(doc);

  /* 0666 & ~022. Not 0600, which is what a renamed temporary would have
     given and what this whole argument exists to stop. */
  EXPECT_EQ(mode_of(path), 0644u)
    << "a newly created file came out " << std::oct << mode_of(path);

  remove(path.c_str());
  umask(saved);
}
#endif  /* _WIN32 */
