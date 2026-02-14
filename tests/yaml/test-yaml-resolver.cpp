/**
 * @file test-yaml-resolver.cpp
 * @brief Tests for Phase 5 implicit typing and explicit tags.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static GTEXT_YAML_Document *parse_with_options(const char *yaml, GTEXT_YAML_Parse_Options *opts, GTEXT_YAML_Error *error) {
	return gtext_yaml_parse(yaml, strlen(yaml), opts, error);
}

TEST(YamlResolver, CoreImplicitScalars) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = parse_with_options("true", NULL, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_BOOL);
	gtext_yaml_free(doc);

	doc = parse_with_options("42", NULL, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_INT);
	gtext_yaml_free(doc);

	doc = parse_with_options("3.14", NULL, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_FLOAT);
	gtext_yaml_free(doc);

	doc = parse_with_options("~", NULL, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_NULL);
	gtext_yaml_free(doc);
}

TEST(YamlResolver, JsonSchemaScalars) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.schema = GTEXT_YAML_SCHEMA_JSON;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = parse_with_options("true", &opts, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_BOOL);
	gtext_yaml_free(doc);

	doc = parse_with_options("True", &opts, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_STRING);
	gtext_yaml_free(doc);

	doc = parse_with_options("0x10", &opts, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_STRING);
	gtext_yaml_free(doc);
}

TEST(YamlResolver, FailsafeSchemaScalars) {
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.schema = GTEXT_YAML_SCHEMA_FAILSAFE;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = parse_with_options("true", &opts, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_STRING);
	gtext_yaml_free(doc);
}

TEST(YamlResolver, ExplicitTagOverrides) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = parse_with_options("!!int \"42\"", NULL, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc)), GTEXT_YAML_INT);
	gtext_yaml_free(doc);
}

TEST(YamlResolver, InvalidExplicitTagValue) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = parse_with_options("!!int nope", NULL, &error);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(error.code, GTEXT_YAML_E_INVALID);
}

TEST(YamlResolver, TagHandleResolution) {
	const char *yaml =
		"%TAG !e! tag:example.com,2026:\n"
		"---\n"
		"!e!thing value\n";

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = parse_with_options(yaml, NULL, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_STREQ(gtext_yaml_node_tag(root), "tag:example.com,2026:thing");

	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
