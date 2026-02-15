/**
 * @file test-yaml-config-mode.cpp
 * @brief Tests for GTEXT_YAML_MODE_CONFIG preset behavior.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlConfigMode, DisablesJsonFastPath) {
	const char *yaml = "{\"a\": 1}";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.mode = GTEXT_YAML_MODE_CONFIG;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);

	gtext_yaml_free(doc);
}

TEST(YamlConfigMode, RejectsNonStringKeys) {
	const char *yaml = "!!int 1: foo\n";
	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.mode = GTEXT_YAML_MODE_CONFIG;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	EXPECT_EQ(doc, nullptr);
	EXPECT_EQ(error.code, GTEXT_YAML_E_INVALID);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
