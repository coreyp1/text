/**
 * @file test-yaml-source-location.cpp
 * @brief Tests for source location metadata on YAML DOM nodes.
 */

#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

static void expect_location(
	const GTEXT_YAML_Node *node,
	size_t offset,
	int line,
	int col
) {
	GTEXT_YAML_Source_Location loc;
	ASSERT_TRUE(gtext_yaml_node_source_location(node, &loc));
	EXPECT_EQ(loc.offset, offset);
	EXPECT_EQ(loc.line, line);
	EXPECT_EQ(loc.col, col);
}

TEST(YamlSourceLocation, BlockMappingAndSequence) {
	const char *yaml =
		"key: value\n"
		"list:\n"
		"  - item\n";

	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));

	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (error.message ? error.message : "unknown");

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	expect_location(root, 0, 1, 1);

	const GTEXT_YAML_Node *key0 = NULL;
	const GTEXT_YAML_Node *value0 = NULL;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 0, &key0, &value0));
	ASSERT_NE(key0, nullptr);
	ASSERT_NE(value0, nullptr);
	expect_location(key0, 0, 1, 1);
	expect_location(value0, 5, 1, 6);

	const GTEXT_YAML_Node *key1 = NULL;
	const GTEXT_YAML_Node *value1 = NULL;
	ASSERT_TRUE(gtext_yaml_mapping_get_at(root, 1, &key1, &value1));
	ASSERT_NE(key1, nullptr);
	ASSERT_NE(value1, nullptr);
	expect_location(key1, 11, 2, 1);
	EXPECT_EQ(gtext_yaml_node_type(value1), GTEXT_YAML_SEQUENCE);
	expect_location(value1, 19, 3, 3);

	const GTEXT_YAML_Node *item = gtext_yaml_sequence_get(value1, 0);
	ASSERT_NE(item, nullptr);
	expect_location(item, 21, 3, 5);

	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
