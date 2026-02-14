/**
 * @file test-yaml-dom-manipulation.cpp
 * @brief Tests for DOM manipulation API (Phase 4.7)
 *
 * Tests programmatic document building and modification without parsing.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <gtest/gtest.h>
#include <ghoti.io/text/yaml.h>
#include <string.h>

/**
 * Test creating an empty document
 */
TEST(YamlDomManipulation, CreateEmptyDocument) {
	GTEXT_YAML_Error error = {};
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, &error);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(gtext_yaml_document_root(doc), nullptr);
	gtext_yaml_free(doc);
}

/**
 * Test creating a scalar node
 */
TEST(YamlDomManipulation, CreateScalarNode) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *node = gtext_yaml_node_new_scalar(doc, "hello world", NULL, NULL);
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(node), GTEXT_YAML_STRING);
	EXPECT_STREQ(gtext_yaml_node_as_string(node), "hello world");
	
	gtext_yaml_free(doc);
}

/**
 * Test creating a scalar node with tag and anchor
 */
TEST(YamlDomManipulation, CreateScalarWithMetadata) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *node = gtext_yaml_node_new_scalar(doc, "test", "!!str", "myanchor");
	ASSERT_NE(node, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(node), "test");
	EXPECT_STREQ(gtext_yaml_node_tag(node), "!!str");
	EXPECT_STREQ(gtext_yaml_node_anchor(node), "myanchor");
	
	gtext_yaml_free(doc);
}

/**
 * Test setting document root
 */
TEST(YamlDomManipulation, SetDocumentRoot) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *root = gtext_yaml_node_new_scalar(doc, "root value", NULL, NULL);
	ASSERT_NE(root, nullptr);
	
	EXPECT_TRUE(gtext_yaml_document_set_root(doc, root));
	EXPECT_EQ(gtext_yaml_document_root(doc), root);
	
	gtext_yaml_free(doc);
}

/**
 * Test creating an empty sequence
 */
TEST(YamlDomManipulation, CreateEmptySequence) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	ASSERT_NE(seq, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(seq), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(seq), 0);
	
	gtext_yaml_free(doc);
}

/**
 * Test creating an empty mapping
 */
TEST(YamlDomManipulation, CreateEmptyMapping) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
	ASSERT_NE(map, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(map), GTEXT_YAML_MAPPING);
	EXPECT_EQ(gtext_yaml_mapping_size(map), 0);
	
	gtext_yaml_free(doc);
}

/**
 * Test appending to a sequence
 */
TEST(YamlDomManipulation, SequenceAppend) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	ASSERT_NE(seq, nullptr);
	
	// Append first item
	GTEXT_YAML_Node *item1 = gtext_yaml_node_new_scalar(doc, "first", NULL, NULL);
	ASSERT_NE(item1, nullptr);
	seq = gtext_yaml_sequence_append(doc, seq, item1);
	ASSERT_NE(seq, nullptr);
	EXPECT_EQ(gtext_yaml_sequence_length(seq), 1);
	
	// Append second item
	GTEXT_YAML_Node *item2 = gtext_yaml_node_new_scalar(doc, "second", NULL, NULL);
	ASSERT_NE(item2, nullptr);
	seq = gtext_yaml_sequence_append(doc, seq, item2);
	ASSERT_NE(seq, nullptr);
	EXPECT_EQ(gtext_yaml_sequence_length(seq), 2);
	
	// Verify contents
	const GTEXT_YAML_Node *child0 = gtext_yaml_sequence_get(seq, 0);
	const GTEXT_YAML_Node *child1 = gtext_yaml_sequence_get(seq, 1);
	ASSERT_NE(child0, nullptr);
	ASSERT_NE(child1, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(child0), "first");
	EXPECT_STREQ(gtext_yaml_node_as_string(child1), "second");
	
	gtext_yaml_free(doc);
}

/**
 * Test inserting into a sequence
 */
TEST(YamlDomManipulation, SequenceInsert) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	// Create sequence with two items
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	GTEXT_YAML_Node *item1 = gtext_yaml_node_new_scalar(doc, "first", NULL, NULL);
	GTEXT_YAML_Node *item2 = gtext_yaml_node_new_scalar(doc, "third", NULL, NULL);
	seq = gtext_yaml_sequence_append(doc, seq, item1);
	seq = gtext_yaml_sequence_append(doc, seq, item2);
	ASSERT_NE(seq, nullptr);
	
	// Insert in the middle
	GTEXT_YAML_Node *item_mid = gtext_yaml_node_new_scalar(doc, "second", NULL, NULL);
	seq = gtext_yaml_sequence_insert(doc, seq, 1, item_mid);
	ASSERT_NE(seq, nullptr);
	EXPECT_EQ(gtext_yaml_sequence_length(seq), 3);
	
	// Verify order
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 0)), "first");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 1)), "second");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 2)), "third");
	
	gtext_yaml_free(doc);
}

/**
 * Test inserting at beginning of sequence
 */
TEST(YamlDomManipulation, SequenceInsertAtBeginning) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	GTEXT_YAML_Node *item1 = gtext_yaml_node_new_scalar(doc, "second", NULL, NULL);
	seq = gtext_yaml_sequence_append(doc, seq, item1);
	
	GTEXT_YAML_Node *item0 = gtext_yaml_node_new_scalar(doc, "first", NULL, NULL);
	seq = gtext_yaml_sequence_insert(doc, seq, 0, item0);
	ASSERT_NE(seq, nullptr);
	
	EXPECT_EQ(gtext_yaml_sequence_length(seq), 2);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 0)), "first");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 1)), "second");
	
	gtext_yaml_free(doc);
}

/**
 * Test inserting at end of sequence (equivalent to append)
 */
TEST(YamlDomManipulation, SequenceInsertAtEnd) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	GTEXT_YAML_Node *item1 = gtext_yaml_node_new_scalar(doc, "first", NULL, NULL);
	seq = gtext_yaml_sequence_append(doc, seq, item1);
	
	GTEXT_YAML_Node *item2 = gtext_yaml_node_new_scalar(doc, "second", NULL, NULL);
	seq = gtext_yaml_sequence_insert(doc, seq, 1, item2);
	ASSERT_NE(seq, nullptr);
	
	EXPECT_EQ(gtext_yaml_sequence_length(seq), 2);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 1)), "second");
	
	gtext_yaml_free(doc);
}

/**
 * Test removing from a sequence
 */
TEST(YamlDomManipulation, SequenceRemove) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	// Create sequence with three items
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	seq = gtext_yaml_sequence_append(doc, seq, gtext_yaml_node_new_scalar(doc, "first", NULL, NULL));
	seq = gtext_yaml_sequence_append(doc, seq, gtext_yaml_node_new_scalar(doc, "second", NULL, NULL));
	seq = gtext_yaml_sequence_append(doc, seq, gtext_yaml_node_new_scalar(doc, "third", NULL, NULL));
	ASSERT_EQ(gtext_yaml_sequence_length(seq), 3);
	
	// Remove middle element
	EXPECT_TRUE(gtext_yaml_sequence_remove(seq, 1));
	EXPECT_EQ(gtext_yaml_sequence_length(seq), 2);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 0)), "first");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(seq, 1)), "third");
	
	gtext_yaml_free(doc);
}

/**
 * Test setting a mapping key-value pair
 */
TEST(YamlDomManipulation, MappingSet) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
	ASSERT_NE(map, nullptr);
	
	// Set first pair
	GTEXT_YAML_Node *key1 = gtext_yaml_node_new_scalar(doc, "name", NULL, NULL);
	GTEXT_YAML_Node *val1 = gtext_yaml_node_new_scalar(doc, "Alice", NULL, NULL);
	map = gtext_yaml_mapping_set(doc, map, key1, val1);
	ASSERT_NE(map, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(map), 1);
	
	// Set second pair
	GTEXT_YAML_Node *key2 = gtext_yaml_node_new_scalar(doc, "age", NULL, NULL);
	GTEXT_YAML_Node *val2 = gtext_yaml_node_new_scalar(doc, "30", NULL, NULL);
	map = gtext_yaml_mapping_set(doc, map, key2, val2);
	ASSERT_NE(map, nullptr);
	EXPECT_EQ(gtext_yaml_mapping_size(map), 2);
	
	// Verify contents
	const GTEXT_YAML_Node *retrieved = gtext_yaml_mapping_get(map, "name");
	ASSERT_NE(retrieved, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(retrieved), "Alice");
	
	retrieved = gtext_yaml_mapping_get(map, "age");
	ASSERT_NE(retrieved, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(retrieved), "30");
	
	gtext_yaml_free(doc);
}

/**
 * Test replacing a mapping value (last-wins)
 */
TEST(YamlDomManipulation, MappingReplace) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
	
	// Set initial value
	GTEXT_YAML_Node *key1 = gtext_yaml_node_new_scalar(doc, "key", NULL, NULL);
	GTEXT_YAML_Node *val1 = gtext_yaml_node_new_scalar(doc, "old", NULL, NULL);
	map = gtext_yaml_mapping_set(doc, map, key1, val1);
	
	// Replace with new value
	GTEXT_YAML_Node *key2 = gtext_yaml_node_new_scalar(doc, "key", NULL, NULL);
	GTEXT_YAML_Node *val2 = gtext_yaml_node_new_scalar(doc, "new", NULL, NULL);
	map = gtext_yaml_mapping_set(doc, map, key2, val2);
	
	// Should still have 1 pair (replaced, not added)
	EXPECT_EQ(gtext_yaml_mapping_size(map), 1);
	
	// Verify new value
	const GTEXT_YAML_Node *retrieved = gtext_yaml_mapping_get(map, "key");
	ASSERT_NE(retrieved, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(retrieved), "new");
	
	gtext_yaml_free(doc);
}

/**
 * Test mapping has_key check
 */
TEST(YamlDomManipulation, MappingHasKey) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
	map = gtext_yaml_mapping_set(doc, map,
		gtext_yaml_node_new_scalar(doc, "exists", NULL, NULL),
		gtext_yaml_node_new_scalar(doc, "yes", NULL, NULL));
	
	EXPECT_TRUE(gtext_yaml_mapping_has_key(map, "exists"));
	EXPECT_FALSE(gtext_yaml_mapping_has_key(map, "missing"));
	
	gtext_yaml_free(doc);
}

/**
 * Test deleting from a mapping
 */
TEST(YamlDomManipulation, MappingDelete) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *map = gtext_yaml_node_new_mapping(doc, NULL, NULL);
	map = gtext_yaml_mapping_set(doc, map,
		gtext_yaml_node_new_scalar(doc, "keep", NULL, NULL),
		gtext_yaml_node_new_scalar(doc, "this", NULL, NULL));
	map = gtext_yaml_mapping_set(doc, map,
		gtext_yaml_node_new_scalar(doc, "remove", NULL, NULL),
		gtext_yaml_node_new_scalar(doc, "that", NULL, NULL));
	
	EXPECT_EQ(gtext_yaml_mapping_size(map), 2);
	
	// Delete one key
	EXPECT_TRUE(gtext_yaml_mapping_delete(map, "remove"));
	EXPECT_EQ(gtext_yaml_mapping_size(map), 1);
	EXPECT_TRUE(gtext_yaml_mapping_has_key(map, "keep"));
	EXPECT_FALSE(gtext_yaml_mapping_has_key(map, "remove"));
	
	// Try deleting non-existent key
	EXPECT_FALSE(gtext_yaml_mapping_delete(map, "missing"));
	EXPECT_EQ(gtext_yaml_mapping_size(map), 1);
	
	gtext_yaml_free(doc);
}

/**
 * Test building a nested structure programmatically
 */
TEST(YamlDomManipulation, BuildNestedStructure) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	// Build: {name: "Alice", hobbies: ["reading", "coding"]}
	GTEXT_YAML_Node *root = gtext_yaml_node_new_mapping(doc, NULL, NULL);
	
	// Add name
	root = gtext_yaml_mapping_set(doc, root,
		gtext_yaml_node_new_scalar(doc, "name", NULL, NULL),
		gtext_yaml_node_new_scalar(doc, "Alice", NULL, NULL));
	
	// Build hobbies sequence
	GTEXT_YAML_Node *hobbies = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	hobbies = gtext_yaml_sequence_append(doc, hobbies,
		gtext_yaml_node_new_scalar(doc, "reading", NULL, NULL));
	hobbies = gtext_yaml_sequence_append(doc, hobbies,
		gtext_yaml_node_new_scalar(doc, "coding", NULL, NULL));
	
	// Add hobbies to root
	root = gtext_yaml_mapping_set(doc, root,
		gtext_yaml_node_new_scalar(doc, "hobbies", NULL, NULL),
		hobbies);
	
	gtext_yaml_document_set_root(doc, root);
	
	// Verify structure
	const GTEXT_YAML_Node *doc_root = gtext_yaml_document_root(doc);
	ASSERT_NE(doc_root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(doc_root), GTEXT_YAML_MAPPING);
	
	const GTEXT_YAML_Node *name = gtext_yaml_mapping_get(doc_root, "name");
	ASSERT_NE(name, nullptr);
	EXPECT_STREQ(gtext_yaml_node_as_string(name), "Alice");
	
	const GTEXT_YAML_Node *hobbies_node = gtext_yaml_mapping_get(doc_root, "hobbies");
	ASSERT_NE(hobbies_node, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(hobbies_node), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_sequence_length(hobbies_node), 2);
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(hobbies_node, 0)), "reading");
	EXPECT_STREQ(gtext_yaml_node_as_string(gtext_yaml_sequence_get(hobbies_node, 1)), "coding");
	
	gtext_yaml_free(doc);
}

/**
 * Test error cases - null document
 */
TEST(YamlDomManipulation, ErrorNullDocument) {
	EXPECT_EQ(gtext_yaml_node_new_scalar(NULL, "test", NULL, NULL), nullptr);
	EXPECT_EQ(gtext_yaml_node_new_sequence(NULL, NULL, NULL), nullptr);
	EXPECT_EQ(gtext_yaml_node_new_mapping(NULL, NULL, NULL), nullptr);
}

/**
 * Test error cases - invalid operations
 */
TEST(YamlDomManipulation, ErrorInvalidOperations) {
	GTEXT_YAML_Document *doc = gtext_yaml_document_new(NULL, NULL);
	ASSERT_NE(doc, nullptr);
	
	GTEXT_YAML_Node *scalar = gtext_yaml_node_new_scalar(doc, "test", NULL, NULL);
	GTEXT_YAML_Node *seq = gtext_yaml_node_new_sequence(doc, NULL, NULL);
	
	// Can't append to a scalar
	EXPECT_EQ(gtext_yaml_sequence_append(doc, scalar, seq), nullptr);
	
	// Can't insert into a scalar
	EXPECT_EQ(gtext_yaml_sequence_insert(doc, scalar, 0, seq), nullptr);
	
	// Can't remove from empty sequence
	EXPECT_FALSE(gtext_yaml_sequence_remove(seq, 0));
	
	// Can't remove out of bounds
	seq = gtext_yaml_sequence_append(doc, seq, scalar);
	EXPECT_FALSE(gtext_yaml_sequence_remove(seq, 1));
	
	gtext_yaml_free(doc);
}
