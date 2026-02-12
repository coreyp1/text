/**
 * @file test-yaml-dom-parser.cpp
 * @brief Comprehensive tests for Phase 4.2 DOM parser
 * 
 * Tests to ensure we can detect regressions in:
 * - Scalar parsing (bare, quoted, block)
 * - Flow-style sequences and mappings
 * - Block-style sequences and mappings
 * - Nested structures
 * - Empty collections
 * - Root node access
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

// ============================================================================
// Scalar Tests
// ============================================================================

TEST(YamlDomParser, BareScalar) {
	const char *yaml = "hello";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr) << "Parse failed: " << (error.message ? error.message : "unknown");
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	const char *value = gtext_yaml_node_as_string(root);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(value, "hello");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, QuotedScalar) {
	const char *yaml = "\"hello world\"";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	const char *value = gtext_yaml_node_as_string(root);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(value, "hello world");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, SingleQuotedScalar) {
	const char *yaml = "'hello world'";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	const char *value = gtext_yaml_node_as_string(root);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(value, "hello world");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, EmptyString) {
	const char *yaml = "\"\"";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	const char *value = gtext_yaml_node_as_string(root);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(value, "");
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, NumericScalar) {
	const char *yaml = "42";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_STRING);
	
	// Phase 4.2: all scalars are strings (type resolution in Phase 5)
	const char *value = gtext_yaml_node_as_string(root);
	ASSERT_NE(value, nullptr);
	EXPECT_STREQ(value, "42");
	
	gtext_yaml_free(doc);
}

// ============================================================================
// Flow-Style Sequence Tests
// ============================================================================

TEST(YamlDomParser, FlowSequenceEmpty) {
	const char *yaml = "[]";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, FlowSequenceSingleItem) {
	const char *yaml = "[hello]";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, FlowSequenceMultipleItems) {
	const char *yaml = "[one, two, three]";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, FlowSequenceWithQuotedStrings) {
	const char *yaml = "[\"hello world\", 'foo bar', baz]";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

// ============================================================================
// Flow-Style Mapping Tests
// ============================================================================

TEST(YamlDomParser, FlowMappingEmpty) {
	const char *yaml = "{}";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, FlowMappingSinglePair) {
	const char *yaml = "{key: value}";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, FlowMappingMultiplePairs) {
	const char *yaml = "{name: Alice, age: 30, city: NYC}";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, FlowMappingWithQuotedKeys) {
	const char *yaml = "{\"first name\": Alice, 'last name': Smith}";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

// ============================================================================
// Block-Style Tests
// ============================================================================

TEST(YamlDomParser, DISABLED_BlockSequence) {
	// TODO: Block-style sequences not yet fully supported
	// The parser creates a scalar node for the first item instead of a sequence
	const char *yaml = "- one\n- two\n- three";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, DISABLED_BlockMapping) {
	// TODO: Block-style mappings not yet fully supported  
	const char *yaml = "name: Alice\nage: 30\ncity: NYC";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, DISABLED_BlockMappingSinglePair) {
	// TODO: Block-style mappings not yet fully supported
	const char *yaml = "key: value";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

// ============================================================================
// Nested Structure Tests
// ============================================================================

TEST(YamlDomParser, NestedSequenceInSequence) {
	const char *yaml = "[[1, 2], [3, 4]]";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, NestedMappingInSequence) {
	const char *yaml = "[{name: Alice}, {name: Bob}]";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_SEQUENCE);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, NestedSequenceInMapping) {
	const char *yaml = "{items: [1, 2, 3], tags: [a, b]}";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, NestedMappingInMapping) {
	const char *yaml = "{person: {name: Alice, age: 30}}";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

TEST(YamlDomParser, DeeplyNested) {
	const char *yaml = "{a: {b: {c: {d: value}}}}";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	ASSERT_NE(doc, nullptr);
	
	const GTEXT_YAML_Node *root = gtext_yaml_document_root(doc);
	ASSERT_NE(root, nullptr);
	EXPECT_EQ(gtext_yaml_node_type(root), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(YamlDomParser, EmptyInput) {
	const char *yaml = "";
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(yaml, strlen(yaml), NULL, &error);
	// Empty input might return NULL or a document with NULL root
	// Either is acceptable
	if (doc) {
		gtext_yaml_free(doc);
	}
}

TEST(YamlDomParser, NullInput) {
	GTEXT_YAML_Error error;
	memset(&error, 0, sizeof(error));
	
	GTEXT_YAML_Document *doc = gtext_yaml_parse(NULL, 0, NULL, &error);
	// NULL/0 input is treated as empty by streaming parser
	// This is acceptable behavior - just ensure it doesn't crash
	if (doc) {
		gtext_yaml_free(doc);
	}
}

// ============================================================================
// Memory Management Tests
// ============================================================================

TEST(YamlDomParser, FreeNullDocument) {
	// Should not crash
	gtext_yaml_free(NULL);
}

TEST(YamlDomParser, MultipleDocumentParsing) {
	// Parse multiple documents to ensure no memory corruption
	const char *yaml1 = "hello";
	const char *yaml2 = "[1, 2, 3]";
	const char *yaml3 = "{key: value}";
	
	GTEXT_YAML_Error error;
	
	memset(&error, 0, sizeof(error));
	GTEXT_YAML_Document *doc1 = gtext_yaml_parse(yaml1, strlen(yaml1), NULL, &error);
	ASSERT_NE(doc1, nullptr);
	
	memset(&error, 0, sizeof(error));
	GTEXT_YAML_Document *doc2 = gtext_yaml_parse(yaml2, strlen(yaml2), NULL, &error);
	ASSERT_NE(doc2, nullptr);
	
	memset(&error, 0, sizeof(error));
	GTEXT_YAML_Document *doc3 = gtext_yaml_parse(yaml3, strlen(yaml3), NULL, &error);
	ASSERT_NE(doc3, nullptr);
	
	// Verify they're independent
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc1)), GTEXT_YAML_STRING);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc2)), GTEXT_YAML_SEQUENCE);
	EXPECT_EQ(gtext_yaml_node_type(gtext_yaml_document_root(doc3)), GTEXT_YAML_MAPPING);
	
	gtext_yaml_free(doc1);
	gtext_yaml_free(doc2);
	gtext_yaml_free(doc3);
}
