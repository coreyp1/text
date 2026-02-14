/**
 * @file test-yaml-dom-clone.cpp
 * @brief Tests for YAML DOM node cloning API
 */

#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml.h>
#include <stdlib.h>
#include <string.h>
}

TEST(YamlDomClone, ScalarClonePreservesMetadata) {
	GTEXT_YAML_Document *doc1 = gtext_yaml_document_new(nullptr, nullptr);
	ASSERT_NE(doc1, nullptr);
	GTEXT_YAML_Node *node = gtext_yaml_node_new_scalar(doc1, "hello", "!str", "a1");
	ASSERT_NE(node, nullptr);

	GTEXT_YAML_Document *doc2 = gtext_yaml_document_new(nullptr, nullptr);
	ASSERT_NE(doc2, nullptr);
	GTEXT_YAML_Node *clone = gtext_yaml_node_clone(doc2, node);
	ASSERT_NE(clone, nullptr);
	EXPECT_NE(clone, node);
	EXPECT_STREQ(gtext_yaml_node_as_string(clone), "hello");
	EXPECT_STREQ(gtext_yaml_node_tag(clone), "!str");
	EXPECT_STREQ(gtext_yaml_node_anchor(clone), "a1");

	gtext_yaml_free(doc1);
	gtext_yaml_free(doc2);
}

TEST(YamlDomClone, ClonesNestedStructures) {
	GTEXT_YAML_Document *doc1 = gtext_yaml_document_new(nullptr, nullptr);
	ASSERT_NE(doc1, nullptr);

	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc1, nullptr, nullptr);
	ASSERT_NE(seq, nullptr);
	GTEXT_YAML_Node *one = gtext_yaml_node_new_scalar(doc1, "one", nullptr, nullptr);
	GTEXT_YAML_Node *two = gtext_yaml_node_new_scalar(doc1, "two", nullptr, nullptr);
	seq = gtext_yaml_sequence_append(doc1, seq, one);
	ASSERT_NE(seq, nullptr);
	seq = gtext_yaml_sequence_append(doc1, seq, two);
	ASSERT_NE(seq, nullptr);

	GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc1, nullptr, nullptr);
	ASSERT_NE(map, nullptr);
	GTEXT_YAML_Node *key = gtext_yaml_node_new_scalar(doc1, "items", nullptr, nullptr);
	map = gtext_yaml_mapping_set(doc1, map, key, seq);
	ASSERT_NE(map, nullptr);

	GTEXT_YAML_Document *doc2 = gtext_yaml_document_new(nullptr, nullptr);
	ASSERT_NE(doc2, nullptr);
	GTEXT_YAML_Node *clone = gtext_yaml_node_clone(doc2, map);
	ASSERT_NE(clone, nullptr);

	const GTEXT_YAML_Node *value = gtext_yaml_mapping_get(clone, "items");
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(value), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(value), 2u);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(value, 0)), "one");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(value, 1)), "two");

	gtext_yaml_free(doc1);
	gtext_yaml_free(doc2);
}

TEST(YamlDomClone, ClonesAliasCycles) {
	const char *yaml = "---\na: &a [*a]\n";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	GTEXT_YAML_Document *doc1 = gtext_yaml_parse(yaml, strlen(yaml), nullptr, &err);
	ASSERT_NE(doc1, nullptr);

	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc1);
	ASSERT_NE(root, nullptr);
	const GTEXT_YAML_Node *seq = gtext_yaml_mapping_get(root, "a");
	ASSERT_NE(seq, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(seq), GTEXT_YAML_SEQUENCE);

	GTEXT_YAML_Document *doc2 = gtext_yaml_document_new(nullptr, nullptr);
	ASSERT_NE(doc2, nullptr);
	GTEXT_YAML_Node *clone_root = gtext_yaml_node_clone(doc2, root);
	ASSERT_NE(clone_root, nullptr);

	const GTEXT_YAML_Node *clone_seq = gtext_yaml_mapping_get(clone_root, "a");
	ASSERT_NE(clone_seq, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(clone_seq), GTEXT_YAML_SEQUENCE);
	const GTEXT_YAML_Node *alias_node = gtext_yaml_sequence_get(clone_seq, 0);
	ASSERT_NE(alias_node, nullptr);
	const GTEXT_YAML_Node *alias_target = gtext_yaml_alias_target(alias_node);
	ASSERT_EQ(alias_target, clone_seq);

	gtext_yaml_free(doc1);
	gtext_yaml_free(doc2);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
