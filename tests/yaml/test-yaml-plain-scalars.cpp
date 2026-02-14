/**
 * @file test-yaml-plain-scalars.cpp
 * @brief Tests for plain scalar parsing with spaces in block/flow contexts
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

/**
 * Test that plain scalars with spaces are parsed as single values in block context
 */
TEST(YamlPlainScalars, BlockContextMultiWord) {
	const char *yaml = "key: just a string\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "key");
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	
	const char *str = gtext_yaml_node_as_string(value);
	ASSERT_NE(str, nullptr);
	
	// This is the critical test: the value should be "just a string", not "just"
	EXPECT_STREQ(str, "just a string");
	
	gtext_yaml_free(doc);
}

/**
 * Test plain scalar with multiple spaces
 */
TEST(YamlPlainScalars, BlockContextMultipleSpaces) {
	const char *yaml = "description: This is a longer description with many words\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(root, "description");
	ASSERT_NE(value, nullptr);
	
	const char *str = gtext_yaml_node_as_string(value);
	EXPECT_STREQ(str, "This is a longer description with many words");
	
	gtext_yaml_free(doc);
}

/**
 * Test that plain scalars in flow context still work correctly (space-separated)
 */
TEST(YamlPlainScalars, FlowContextSeparation) {
	const char *yaml = "[one, two, three]\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	size_t count = gtext_yaml_sequence_length(root);
	EXPECT_EQ(count, 3u);
	
	// In flow context, these should be separate values
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(item0, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "one");
	
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "two");
	
	const GTEXT_YAML_Node *item2 = gtext_yaml_sequence_get(root, 2);
	ASSERT_NE(item2, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item2), "three");
	
	gtext_yaml_free(doc);
}

/**
 * Test plain scalar that contains a colon (not followed by space)
 */
TEST(YamlPlainScalars, ContainsColon) {
	const char *yaml = "url: http://example.com\ntime: 12:30:45\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	const GTEXT_YAML_Node *url_val = gtext_yaml_mapping_get(root, "url");
	ASSERT_NE(url_val, nullptr);
	const char *url_str = gtext_yaml_node_as_string(url_val);
	EXPECT_STREQ(url_str, "http://example.com");
	
	const GTEXT_YAML_Node *time_val = gtext_yaml_mapping_get(root, "time");
	ASSERT_NE(time_val, nullptr);
	const char *time_str = gtext_yaml_node_as_string(time_val);
	EXPECT_STREQ(time_str, "12:30:45");
	
	gtext_yaml_free(doc);
}

/**
 * Test plain scalar in sequence context
 */
TEST(YamlPlainScalars, InSequence) {
	const char *yaml = "- first item\n- second item\n- third item\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(item0, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "first item");
	
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "second item");
	
	const GTEXT_YAML_Node *item2 = gtext_yaml_sequence_get(root, 2);
	ASSERT_NE(item2, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item2), "third item");
	
	gtext_yaml_free(doc);
}

/**
 * Test mix of block and flow contexts
 */
TEST(YamlPlainScalars, MixedContext) {
	const char *yaml = "data:\n  items: [one, two, three]\n  note: this is a note\n";
	
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");
	
	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
