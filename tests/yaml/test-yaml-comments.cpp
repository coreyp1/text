/**
 * @file test-yaml-comments.cpp
 * @brief Tests for comment preservation and emission.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlComments, PreserveLeadingAndInline) {
	const char *yaml =
		"# top\n"
		"key: value # inline\n";

	GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
	opts.retain_comments = true;

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), &opts, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *key_node = NULL;
	const GTEXT_YAML_Node *value_node = NULL;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key_node, &value_node));
	ASSERT_NE(key_node, nullptr);
	ASSERT_NE(value_node, nullptr);

	EXPECT_STREQ(gtext_yaml_node_leading_comment(key_node), "top");
	EXPECT_STREQ(gtext_yaml_node_inline_comment(value_node), "inline");

	gtext_yaml_free(doc);
}

TEST(YamlComments, WriteComments) {
	const char *yaml = "key: value\n";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);

	const GTEXT_YAML_Node *key_node = NULL;
	const GTEXT_YAML_Node *value_node = NULL;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key_node, &value_node));

	ASSERT_EQ(gtext_yaml_node_set_leading_comment(doc, (GTEXT_YAML_Node *)key_node, "lead"), GTEXT_YAML_OK);
	ASSERT_EQ(gtext_yaml_node_set_inline_comment(doc, (GTEXT_YAML_Node *)value_node, "inline"), GTEXT_YAML_OK);

	GTEXT_YAML_Sink sink;
	ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);

	GTEXT_YAML_Write_Options write_opts = gtext_yaml_write_options_default();
	write_opts.pretty = true;
	GTEXT_YAML_Status status = gtext_yaml_write_document(doc, &sink, &write_opts);
	EXPECT_EQ(status, GTEXT_YAML_OK);

	const char *out = gtext_yaml_sink_buffer_data(&sink);
	ASSERT_NE(out, nullptr);
	EXPECT_NE(strstr(out, "# lead"), nullptr);
	EXPECT_NE(strstr(out, "value # inline"), nullptr);

	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
