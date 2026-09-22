/**
 * @file test-yaml-dom-clone.cpp
 * @brief Tests for YAML DOM node cloning API
 */

#include <gtest/gtest.h>

#include <set>
#include <string>

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

/* The reason the clone keeps a source-to-clone table at all.
   
   A node reachable by two paths has to clone to *one* node, or the copy is a
   different shape from the original. The table was a linear scan, which made
   cloning quadratic in the node count - 100000 nested sequences took 5.6
   seconds - and is open-addressed on the node pointer now. This is the
   property that change had to preserve, stated without reference to how the
   table is built. */
TEST(YamlDomClone, ANodeReachedTwiceClonesOnce) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
	ASSERT_NE(doc, nullptr);

	GTEXT_YAML_Node *shared =
		gtext_yaml_node_new_scalar(doc, "shared", nullptr, nullptr);
	ASSERT_NE(shared, nullptr);
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, nullptr, nullptr);
	ASSERT_NE(seq, nullptr);
	seq = gtext_yaml_sequence_append(doc, seq, shared);
	ASSERT_NE(seq, nullptr);
	seq = gtext_yaml_sequence_append(doc, seq, shared);
	ASSERT_NE(seq, nullptr);

	/* The control: it really is one node in two slots. */
	ASSERT_EQ(gtext_yaml_sequence_get(seq, 0), gtext_yaml_sequence_get(seq, 1));

	GTEXT_YAML_Node *copy = gtext_yaml_node_clone(doc, seq);
	ASSERT_NE(copy, nullptr);
	ASSERT_EQ(gtext_yaml_sequence_length(copy), 2u);
	const GTEXT_YAML_Node *a = gtext_yaml_sequence_get(copy, 0);
	const GTEXT_YAML_Node *b = gtext_yaml_sequence_get(copy, 1);
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(a, b) << "the shared node was copied twice";
	EXPECT_NE(a, shared) << "the clone points back into the original";

	gtext_yaml_free(doc);
}

/* And the table holds many distinct nodes without losing or conflating any,
   which is what a hash table can get wrong where a scan could not. Enough
   entries to force it to grow several times. */
TEST(YamlDomClone, ManyDistinctNodesEachCloneToTheirOwn) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(nullptr, nullptr);
	ASSERT_NE(doc, nullptr);

	const size_t n = 5000;
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, nullptr, nullptr);
	ASSERT_NE(seq, nullptr);
	for (size_t i = 0; i < n; i++) {
		const std::string text = "item" + std::to_string(i);
		GTEXT_YAML_Node *item =
			gtext_yaml_node_new_scalar(doc, text.c_str(), nullptr, nullptr);
		ASSERT_NE(item, nullptr);
		seq = gtext_yaml_sequence_append(doc, seq, item);
		ASSERT_NE(seq, nullptr);
	}
	ASSERT_EQ(gtext_yaml_sequence_length(seq), n);

	GTEXT_YAML_Node *copy = gtext_yaml_node_clone(doc, seq);
	ASSERT_NE(copy, nullptr);
	ASSERT_EQ(gtext_yaml_sequence_length(copy), n);

	std::set<const GTEXT_YAML_Node *> seen;
	for (size_t i = 0; i < n; i++) {
		const GTEXT_YAML_Node *item = gtext_yaml_sequence_get(copy, i);
		ASSERT_NE(item, nullptr) << "slot " << i;
		EXPECT_TRUE(seen.insert(item).second)
			<< "slot " << i << " shares a clone with an earlier slot";
		EXPECT_STREQ(gtext_yaml_node_as_string(item),
			("item" + std::to_string(i)).c_str()) << "slot " << i;
	}

	gtext_yaml_free(doc);
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
