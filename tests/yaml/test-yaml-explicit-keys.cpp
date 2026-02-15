/**
 * @file test-yaml-explicit-keys.cpp
 * @brief Tests for explicit key indicator handling
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

TEST(YamlExplicitKeys, ExplicitScalarKey) {
	const char *yaml = "? foo\n: bar\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *value = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(key), GTEXT_YAML_STRING);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(key), "foo");
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "bar");

	gtext_yaml_free(doc);
}

TEST(YamlExplicitKeys, ExplicitSequenceKey) {
	const char *yaml = "? - a\n  - b\n: value\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *value = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(key), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "value");

	size_t count = gtext_yaml_sequence_length(key);
	ASSERT_EQ(count, 2u);
	const GTEXT_YAML_Node *item0 = gtext_yaml_sequence_get(key, 0);
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(key, 1);
	ASSERT_NE(item0, nullptr);
	ASSERT_NE(item1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(item0), "a");
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "b");

	gtext_yaml_free(doc);
}

TEST(YamlExplicitKeys, ExplicitMappingKey) {
	const char *yaml = "? {a: 1, b: 2}\n: ok\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);

	const GTEXT_YAML_Node *key = nullptr;
	const GTEXT_YAML_Node *value = nullptr;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key, &value));
	ASSERT_NE(key, nullptr);
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(key), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(value), "ok");

	const GTEXT_YAML_Node *key_a = gtext_yaml_mapping_get(key, "a");
	const GTEXT_YAML_Node *key_b = gtext_yaml_mapping_get(key, "b");
	ASSERT_NE(key_a, nullptr);
	ASSERT_NE(key_b, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(key_a), "1");
	EXPECT_STREQ(gtext_yaml_node_as_string(key_b), "2");

	gtext_yaml_free(doc);
}
