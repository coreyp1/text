/**
 * @file test-yaml-line-breaks.cpp
 * @brief Tests for line break normalization (LF/CRLF/CR)
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlLineBreaks, BlockScalarCrLfNormalized) {
	const char *yaml = "key: |\r\n  line1\r\n  line2\r\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "line1\nline2");

	gtext_yaml_free(doc);
}

TEST(YamlLineBreaks, BlockScalarCrNormalized) {
	const char *yaml = "key: |\r  line1\r  line2\r";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "line1\nline2");

	gtext_yaml_free(doc);
}

TEST(YamlLineBreaks, QuotedScalarCrLfNormalized) {
	const char *yaml = "key: \"line1\r\nline2\"\r\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "line1\nline2");

	gtext_yaml_free(doc);
}
