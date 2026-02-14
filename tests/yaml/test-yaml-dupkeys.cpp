/**
 * @file test-yaml-dupkeys.cpp
 * @brief Tests for duplicate key handling policies.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlDupkeys, DefaultIsError) {
	const char *yaml = "a: 1\na: 2\n";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(error.code, GTEXT_YAML_E_DUPKEY);
}

TEST(YamlDupkeys, FirstWins) {
	const char *yaml = "a: 1\na: 2\n";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.dupkeys = GTEXT_YAML_DUPKEY_FIRST_WINS;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(root), 1u);

	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "1");

	gtext_yaml_free(doc);
}

TEST(YamlDupkeys, LastWins) {
	const char *yaml = "a: 1\na: 2\n";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.dupkeys = GTEXT_YAML_DUPKEY_LAST_WINS;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(root), 1u);

	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "2");

	gtext_yaml_free(doc);
}

TEST(YamlDupkeys, CoreSchemaBoolCaseIsDuplicate) {
	const char *yaml = "true: 1\nTRUE: 2\n";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.schema = GTEXT_YAML_SCHEMA_CORE;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(error.code, GTEXT_YAML_E_DUPKEY);
}

TEST(YamlDupkeys, JsonSchemaBoolCaseIsDistinct) {
	const char *yaml = "true: 1\nTRUE: 2\n";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.schema = GTEXT_YAML_SCHEMA_JSON;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(root), 2u);

	gtext_yaml_free(doc);
}

TEST(YamlDupkeys, ExplicitStrNotEqualBool) {
	const char *yaml = "true: 1\n!!str true: 2\n";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.schema = GTEXT_YAML_SCHEMA_CORE;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(root), 2u);

	gtext_yaml_free(doc);
}

TEST(YamlDupkeys, CoreSchemaNullTildeIsDuplicate) {
	const char *yaml = "null: 1\n~: 2\n";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.schema = GTEXT_YAML_SCHEMA_CORE;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(error.code, GTEXT_YAML_E_DUPKEY);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
