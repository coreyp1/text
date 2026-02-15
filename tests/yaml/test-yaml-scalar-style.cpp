/**
 * @file test-yaml-scalar-style.cpp
 * @brief Tests for scalar style preservation and overrides.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static void expect_style(const GTEXT_YAML_Node *node, GTEXT_YAML_Scalar_Style style) {
	GTEXT_YAML_Scalar_Style actual = GTEXT_YAML_SCALAR_STYLE_PLAIN;
	ASSERT_TRUE(gtext_yaml_node_scalar_style(node, &actual));
	EXPECT_EQ(actual, style);
}

TEST(YamlScalarStyle, PreservesParsedStyle) {
	const char *yaml =
		"single: 'one'\n"
		"double: \"two\"\n"
		"literal: |\n"
		"  line1\n"
		"  line2\n"
		"folded: >\n"
		"  line1\n"
		"  line2\n"
		"plain: plain\n";

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (error.message ? error.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *node = NULL;
	node = gtext_yaml_mapping_get(root, "single");
	ASSERT_NE(node, nullptr);
	expect_style(node, GTEXT_YAML_SCALAR_STYLE_SINGLE_QUOTED);

	node = gtext_yaml_mapping_get(root, "double");
	ASSERT_NE(node, nullptr);
	expect_style(node, GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED);

	node = gtext_yaml_mapping_get(root, "literal");
	ASSERT_NE(node, nullptr);
	expect_style(node, GTEXT_YAML_SCALAR_STYLE_LITERAL);

	node = gtext_yaml_mapping_get(root, "folded");
	ASSERT_NE(node, nullptr);
	expect_style(node, GTEXT_YAML_SCALAR_STYLE_FOLDED);

	node = gtext_yaml_mapping_get(root, "plain");
	ASSERT_NE(node, nullptr);
	expect_style(node, GTEXT_YAML_SCALAR_STYLE_PLAIN);

	GTEXT_YAML_Sink sink;
	ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);

	GTEXT_YAML_Write_Options write_opts = gtext_yaml_write_options_default();
	write_opts.pretty = true;
	ASSERT_EQ(gtext_yaml_write_document(doc, &sink, &write_opts), GTEXT_YAML_OK);

	const char *out = gtext_yaml_sink_buffer_data(&sink);
	ASSERT_NE(out, nullptr);
	EXPECT_NE(strstr(out, "single: 'one'"), nullptr);
	EXPECT_NE(strstr(out, "double: \"two\""), nullptr);
	EXPECT_NE(strstr(out, "literal: |"), nullptr);
	EXPECT_NE(strstr(out, "folded: >"), nullptr);

	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);
}

TEST(YamlScalarStyle, OverridesStyleInWriter) {
	const char *yaml = "key: value\n";

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (error.message ? error.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
	ASSERT_NE(value, nullptr);

	ASSERT_TRUE(gtext_yaml_node_set_scalar_style((GTEXT_YAML_Node *)value, GTEXT_YAML_SCALAR_STYLE_DOUBLE_QUOTED));

	GTEXT_YAML_Sink sink;
	ASSERT_EQ(gtext_yaml_sink_buffer(&sink), GTEXT_YAML_OK);

	GTEXT_YAML_Write_Options write_opts = gtext_yaml_write_options_default();
	write_opts.pretty = true;
	ASSERT_EQ(gtext_yaml_write_document(doc, &sink, &write_opts), GTEXT_YAML_OK);

	const char *out = gtext_yaml_sink_buffer_data(&sink);
	ASSERT_NE(out, nullptr);
	EXPECT_NE(strstr(out, "key: \"value\""), nullptr);

	gtext_yaml_sink_buffer_free(&sink);
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
