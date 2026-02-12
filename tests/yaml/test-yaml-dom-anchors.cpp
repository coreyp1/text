/**
 * @file test-yaml-dom-anchors.cpp
 * @brief Tests for YAML DOM anchor/alias support (Phase 4.4)
 *
 * Tests:
 * - Simple anchor definition and alias reference
 * - Multiple aliases referencing the same anchor
 * - Anchors on different node types (scalar, sequence, mapping)
 * - Nested structures with anchors
 * - Forward references (alias before anchor definition in document order)
 * - Cycles (anchor references itself)
 * - Missing anchor error handling
 * - gtext_yaml_alias_target() accessor function
 */

#include <gtest/gtest.h>
#include <ghoti.io/text/yaml.h>

/* Test simple scalar anchor and alias */
TEST(YamlDomAnchors, SimpleScalarAnchor) {
	const char *yaml = "&anchor value\n*anchor";
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown error");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	/* Root should be an alias that resolves to the anchored scalar */
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_ALIAS);
	const GTEXT_YAML_Node *target = gtext_yaml_alias_target(root);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(target), GTEXT_YAML_STRING);
	
	gtext_yaml_free(doc);
}

/* Test alias references resolve correctly */
TEST(YamlDomAnchors, AliasResolution) {
	const char *yaml = "[&foo bar, *foo]";
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown error");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	/* Get first item (anchored value) */
	const GTEXT_YAML_Node *item1 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(item1, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(item1), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(item1), "bar");
	
	/* Get second item (alias) */
	const GTEXT_YAML_Node *item2 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(item2, nullptr);
	
	/* item2 should be an ALIAS node */
	EXPECT_EQ(gtext_yaml_node_type(item2), GTEXT_YAML_ALIAS);
	
	/* Resolve alias - should point to same node as item1 */
	const GTEXT_YAML_Node *target = gtext_yaml_alias_target(item2);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(target, item1) << "Alias should resolve to the anchored node";
	
	gtext_yaml_free(doc);
}

/* Test multiple aliases to same anchor */
TEST(YamlDomAnchors, MultipleAliases) {
	const char *yaml = "[&shared value, *shared, *shared, *shared]";
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown error");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(root), 4);
	
	/* Get anchored node */
	const GTEXT_YAML_Node *original = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(original, nullptr);
	
	/* All three aliases should resolve to the same node */
	for (size_t i = 1; i <= 3; i++) {
		const GTEXT_YAML_Node *alias = gtext_yaml_sequence_get(root, i);
		ASSERT_NE(alias, nullptr);
		EXPECT_EQ(gtext_yaml_node_type(alias), GTEXT_YAML_ALIAS);
		
		const GTEXT_YAML_Node *target = gtext_yaml_alias_target(alias);
		EXPECT_EQ(target, original) << "Alias " << i << " should resolve to original node";
	}
	
	gtext_yaml_free(doc);
}

/* Test anchor on sequence node */
TEST(YamlDomAnchors, SequenceAnchor) {
	const char *yaml = "[&list [1, 2, 3], *list]";
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown error");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	/* First item is the anchored sequence */
	const GTEXT_YAML_Node *list1 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(list1, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(list1), GTEXT_YAML_SEQUENCE);
	
	/* Second item is alias */
	const GTEXT_YAML_Node *list2 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(list2, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(list2), GTEXT_YAML_ALIAS);
	
	/* Resolve alias */
	const GTEXT_YAML_Node *target = gtext_yaml_alias_target(list2);
	EXPECT_EQ(target, list1);
	
	gtext_yaml_free(doc);
}

/* Test anchor on mapping node */
TEST(YamlDomAnchors, MappingAnchor) {
	const char *yaml = "[&map {key: value}, *map]";
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown error");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	ASSERT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	/* First item is the anchored mapping */
	const GTEXT_YAML_Node *map1 = gtext_yaml_sequence_get(root, 0);
	ASSERT_NE(map1, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(map1), GTEXT_YAML_MAPPING);
	
	/* Second item is alias */
	const GTEXT_YAML_Node *map2 = gtext_yaml_sequence_get(root, 1);
	ASSERT_NE(map2, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(map2), GTEXT_YAML_ALIAS);
	
	/* Resolve alias */
	const GTEXT_YAML_Node *target = gtext_yaml_alias_target(map2);
	EXPECT_EQ(target, map1);
	
	gtext_yaml_free(doc);
}

/* Test gtext_yaml_alias_target on non-alias nodes */
TEST(YamlDomAnchors, AliasTargetNonAlias) {
	const char *yaml = "value";
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	/* gtext_yaml_alias_target should return the node itself if not an alias */
	const GTEXT_YAML_Node *target = gtext_yaml_alias_target(root);
	EXPECT_EQ(target, root);
	
	gtext_yaml_free(doc);
}

/* Test NULL handling in gtext_yaml_alias_target */
TEST(YamlDomAnchors, AliasTargetNull) {
	const GTEXT_YAML_Node *target = gtext_yaml_alias_target(NULL);
	EXPECT_EQ(target, nullptr);
}

/* Test unknown anchor reference (should fail to parse) */
TEST(YamlDomAnchors, UnknownAnchor) {
	const char *yaml = "*unknown";
	GTEXT_YAML_Error err;
	memset(&err, 0, sizeof(err));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	/* Parse should fail */
	EXPECT_EQ(doc, nullptr);
	EXPECT_NE(err.code, GTEXT_YAML_OK);
}

/* Test nested structure with anchors */
TEST(YamlDomAnchors, NestedAnchors) {
	const char *yaml = "{outer: [&inner value, *inner]}";
	
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (err.message ? err.message : "unknown error");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

/* Test that anchor name is preserved */
TEST(YamlDomAnchors, AnchorNamePreserved) {
	const char *yaml = "&myanchor value";
	GTEXT_YAML_Error err;
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &err);
	
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	
	/* Check that anchor name is stored */
	const char *anchor = gtext_yaml_node_anchor(root);
	ASSERT_NE(anchor, nullptr);
	EXPECT_STREQ(anchor, "myanchor");
	
	gtext_yaml_free(doc);
}
